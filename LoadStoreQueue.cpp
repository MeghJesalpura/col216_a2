#include "LoadStoreQueue.h"
#include "Basics.h"
#include <climits>

// ── helpers ────────────────────────────────────────────────────────────────

// Reuse the same operand-resolution logic as ExecutionUnit (copied here so
// LoadStoreQueue.cpp has no dependency on ExecutionUnit.cpp)
static void resolveOperand(int reg,
                           const std::vector<RATEntry> &RAT,
                           const std::vector<int> &ARF,
                           const std::vector<ROBEntry> &ROB,
                           int &out_val, int &out_tag, bool &out_ready)
{
  if (reg < 0 || reg >= static_cast<int>(RAT.size()))
  {
    out_val = 0;
    out_tag = -1;
    out_ready = true;
    return;
  }

  const RATEntry &r = RAT[reg];
  if (r.tag != -1)
  {
    out_tag = r.tag;
    out_ready = false;
    out_val = 0;
    if (ROB[r.tag].ready_bit)
    {
      out_ready = true;
      out_val = ROB[r.tag].value;
    }
  }
  else
  {
    out_val = ARF[reg];
    out_tag = -1;
    out_ready = true;
  }
}

// ── constructor ────────────────────────────────────────────────────────────

LoadStoreQueue::LoadStoreQueue(int lat, int cap)
    : latency(lat), capacity(cap)
{
  RS.resize(cap);
  instr_list.resize(lat, -1);
}

// ── private helpers ────────────────────────────────────────────────────────

int LoadStoreQueue::findFreeEntry() const
{
  for (int i = 0; i < capacity; i++)
    if (!RS[i].isValid && !RS[i].freed_this_cycle)
      return i;
  return -1;
}

// The LSQ executes in-order.  "Ready" here means:
//   • not yet dispatched into the pipeline
//   • has the oldest seq_num among all valid, undispatched entries
//   • that oldest entry also has all operands ready
int LoadStoreQueue::findInOrderReady() const
{
  // Find the oldest (smallest seq_num) valid, undispatched entry
  int oldest_idx = -1;
  unsigned long long oldest_seq = (unsigned long long)-1;

  for (int i = 0; i < capacity; i++)
  {
    if (RS[i].isValid && !RS[i].dispatched)
    {
      if (oldest_idx == -1 || RS[i].seq_num < oldest_seq)
      {
        oldest_idx = i;
        oldest_seq = RS[i].seq_num;
      }
    }
  }

  // The LSQ is in-order: we may only dispatch if the front entry is ready.
  if (oldest_idx == -1)
    return -1;

  const LSQEntry &e = RS[oldest_idx];
  bool src_ready = e.ready1 &&
                   (e.opcode == OpCode::LW || e.ready_store);
  // For LW: only need base register (val1) + imm
  // For SW: also need the value to store (val_store / ready_store)

  return src_ready ? oldest_idx : -1;
}

bool LoadStoreQueue::forwardFromStore(int load_addr, int load_seq,
                                      int &forwarded_val) const
{
  // Walk all LSQ entries AND pending_stores to find the *newest* SW older
  // than this LW that targets the same address.
  bool found = false;
  unsigned long long best_seq = 0;

  // 1. Check active LSQ entries
  for (int i = 0; i < capacity; i++)
  {
    const LSQEntry &e = RS[i];
    if (!e.isValid)
      continue;
    if (e.opcode != OpCode::SW)
      continue;
    if (e.seq_num >= static_cast<unsigned long long>(load_seq))
      continue; // must be older than the load

    if (!e.ready1)
      continue; // address not yet known
    int store_addr = e.val1 + e.imm;
    if (store_addr != load_addr)
      continue;
    if (!e.ready_store)
      continue; // value not yet known

    if (!found || e.seq_num > best_seq)
    {
      best_seq = e.seq_num;
      forwarded_val = e.val_store;
      found = true;
    }
  }

  // 2. Check pending_stores (SW entries that finished execution but haven't committed yet)
  for (const auto &[tag, ps] : pending_stores)
  {
    if (ps.address != load_addr)
      continue;
    if (ps.seq_num >= static_cast<unsigned long long>(load_seq))
      continue;

    if (!found || ps.seq_num > best_seq)
    {
      best_seq = ps.seq_num;
      forwarded_val = ps.value;
      found = true;
    }
  }

  return found;
}

// ── public interface ───────────────────────────────────────────────────────

bool LoadStoreQueue::has_space()
{
  return (findFreeEntry() != -1);
}

void LoadStoreQueue::createLSQEntry(Instruction &instr, int rob_index,
                                    const std::vector<RATEntry> &RAT,
                                    const std::vector<int> &ARF,
                                    const std::vector<ROBEntry> &ROB)
{
  int ind = findFreeEntry();
  if (ind == -1)
    return; // should not happen if has_space() was checked

  // --- resolve base register (src1) ---
  int v1;
  int t1;
  bool r1;
  resolveOperand(instr.src1, RAT, ARF, ROB, v1, t1, r1);

  // --- for SW resolve the value register (src2) ---
  int vs = 0;
  int ts = -1;
  bool rs = true;
  if (instr.op == OpCode::SW)
    resolveOperand(instr.src2, RAT, ARF, ROB, vs, ts, rs);

  RS[ind] = LSQEntry(instr.op, rob_index,
                     v1, t1, r1,
                     instr.imm,
                     vs, ts, rs,
                     /*valid=*/true);
  RS[ind].seq_num = ++seq_counter;
}

// CDB snoop — mirrors ExecutionUnit::capture exactly, extended for val_store
void LoadStoreQueue::capture(int tag, int val)
{
  for (int i = 0; i < capacity; i++)
  {
    if (!RS[i].isValid)
      continue;

    if (!RS[i].ready1 && RS[i].tag1 == tag)
    {
      RS[i].val1 = val;
      RS[i].ready1 = true;
    }
    // SW store-value operand
    if (!RS[i].ready_store && RS[i].tag_store == tag)
    {
      RS[i].val_store = val;
      RS[i].ready_store = true;
    }
  }
}

// Stage 1: advance the pipeline shift-register then attempt to dispatch the
// in-order head of the LSQ.  Mirrors ExecutionUnit::dispatchReady exactly.
void LoadStoreQueue::dispatchReady()
{
  // Shift pipeline register forward (same as ExecutionUnit)
  for (int i = latency - 2; i >= 0; i--)
    instr_list[i + 1] = instr_list[i];
  instr_list[0] = -1;

  int idx = findInOrderReady();
  if (idx != -1)
  {
    instr_list[0] = idx;
    RS[idx].dispatched = true;
  }
}

// Stage 2: produce result for instruction that exits the pipeline this cycle.
// memory is the processor's memory array (indexed by word address or byte
// address — match whatever convention your Processor uses).
void LoadStoreQueue::executeCycle(const std::vector<int> &memory)
{
  has_result = false;
  has_exception = false;
  is_store_result = false;
  result_tag = 0;
  result_val = 0;
  store_address = 0;
  store_value = 0;

  int idx = instr_list[latency - 1];
  if (idx == -1)
    return;

  LSQEntry &e = RS[idx];
  int addr = e.val1 + e.imm; // computed memory address

  has_result = true;
  result_tag = e.dest_tag;

  if (e.opcode == OpCode::LW)
  {
    // Check bounds
    if (addr < 0 || addr >= static_cast<int>(memory.size()))
    {
      has_exception = true;
      result_val = 0;
    }
    else
    {
      int forwarded;
      if (forwardFromStore(addr, e.seq_num, forwarded))
        result_val = forwarded; // store-to-load forwarding
      else
        result_val = memory[addr]; // normal load from memory
    }
  }
  else // SW
  {
    // Check bounds for store address
    if (addr < 0 || addr >= static_cast<int>(memory.size()))
    {
      has_exception = true;
      result_val = 0;
    }
    else
    {
      // Store the address+value in pending_stores; the actual memory write
      // happens at commit time (via commitEntry) to preserve precise exceptions
      // and correct ordering in the presence of branch mispredictions.
      pending_stores[e.dest_tag] = {addr, e.val_store, e.seq_num};

      is_store_result = true;
      store_address = addr;
      store_value = e.val_store;
    }

    // Broadcast on CDB so the ROB can mark this entry ready for commit.
    // SW has no register destination, so result_val is unused by the ARF.
    result_val = 0;
  }

  e.freed_this_cycle = true; // Mark the entry as being freed this cycle
}

void LoadStoreQueue::clearFreedFlags()
{
  for (int i = 0; i < capacity; i++)
  {
    if (RS[i].freed_this_cycle)
    {
      RS[i].isValid = false;
      RS[i].freed_this_cycle = false;
    }
  }
}

void LoadStoreQueue::commitEntry(int rob_tag, std::vector<int> &memory)
{
  auto it = pending_stores.find(rob_tag);
  if (it == pending_stores.end())
    return; // not a store — nothing to do

  int addr = it->second.address;
  int val = it->second.value;
  pending_stores.erase(it);

  if (addr >= 0 && addr < static_cast<int>(memory.size()))
    memory[addr] = val;
}

void LoadStoreQueue::flush()
{
  for (int i = 0; i < capacity; i++)
  {
    RS[i].isValid = false;
    RS[i].dispatched = false;
  }
  for (int i = 0; i < latency; i++)
    instr_list[i] = -1;

  has_result = false;
  has_exception = false;
  is_store_result = false;
  result_tag = 0;
  result_val = 0;
  store_address = 0;
  store_value = 0;
  pending_stores.clear();
}