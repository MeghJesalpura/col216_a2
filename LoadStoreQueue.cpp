#include "LoadStoreQueue.h"

LoadStoreQueue::LoadStoreQueue(int latency, int lsq_capacity) : latency(latency), lsq_capacity(lsq_capacity), lsq_filled(0) {}

bool LoadStoreQueue::has_space()
{
  return lsq_filled < lsq_capacity;
}

void LoadStoreQueue::capture(int tag, int val)
{
  for (auto &entry : q)
  {
    if (!entry.addr_ready && entry.addr_tag == tag)
    {
      entry.addr_val = val;
      entry.addr_ready = true;
    }

    if (entry.type == LSQEntryType::STORE && !entry.data_ready && entry.data_tag == tag)
    {
      entry.data_val = val;
      entry.data_ready = true;
    }
  }
}

void LoadStoreQueue::executeCycle(std::vector<int> &Memory)
{
  has_result = false;
  has_exception = false;

  if (q.empty())
    return;

  auto it = q.begin();
  while (it != q.end() && it->done)
  {
    it++;
  }

  if (it == q.end())
    return;

  LSQEntry &entry = *it;

  if (!entry.addr_ready)
    return;

  if (!entry.dispatched)
  {
    entry.dispatched = true;
    entry.cycles_left = latency;
  }

  if (entry.cycles_left > 0)
  {
    entry.cycles_left--;
  }

  if (entry.cycles_left > 0)
    return;

  entry.eff_addr = entry.addr_val + entry.offset;
  int eff_addr = entry.eff_addr;

  if (entry.type == LSQEntryType::LOAD)
  {
    if (eff_addr < 0 || eff_addr >= static_cast<int>(Memory.size()))
    {
      if (!entry.broadcasted)
      {
        has_result = true;
        has_exception = true;
        result_tag = entry.dest_tag;
        result_val = 0;
        entry.done = true;
        entry.exception = true;
        entry.broadcasted = true;
      }
      return;
    }

    int loaded_val = Memory[eff_addr];
    auto curr = it;
    while (curr != q.begin())
    {
      --curr;
      if (curr->type == LSQEntryType::STORE && curr->eff_addr == eff_addr)
      {
        loaded_val = curr->data_val;
        break;
      }
    }

    if (!entry.broadcasted)
    {
      has_result = true;
      has_exception = false;
      result_tag = entry.dest_tag;
      result_val = loaded_val;
      entry.done = true;
      entry.result = loaded_val;
      entry.broadcasted = true;
    }
  }
  else if (entry.type == LSQEntryType::STORE)
  {
    if (!entry.data_ready)
      return;

    if (eff_addr < 0 || eff_addr >= static_cast<int>(Memory.size()))
    {
      if (!entry.broadcasted)
      {
        has_result = true;
        has_exception = true;
        result_tag = entry.dest_tag;
        result_val = 0;
        entry.done = true;
        entry.exception = true;
        entry.broadcasted = true;
      }
      return;
    }

    if (!entry.broadcasted)
    {
      has_result = true;
      has_exception = false;
      result_tag = entry.dest_tag;
      result_val = entry.data_val;
      entry.done = true;
      entry.result = eff_addr;
      entry.broadcasted = true;
    }
  }
}

void LoadStoreQueue::commitEntry(int tag, std::vector<int> &Memory)
{
  if (!q.empty() && q.front().dest_tag == tag)
  {
    if (q.front().type == LSQEntryType::STORE && !q.front().exception)
    {
      if (q.front().eff_addr >= 0 && q.front().eff_addr < static_cast<int>(Memory.size()))
      {
        Memory[q.front().eff_addr] = q.front().data_val;
      }
    }
    q.pop_front();
    lsq_filled--;
  }
}

void LoadStoreQueue::createLSQEntry(const Instruction &instr, int rob_index, const std::vector<RATEntry> &RAT, const std::vector<int> &ARF, std::vector<ROBEntry> &ROB)
{
  LSQEntry entry;
  entry.type = (instr.op == OpCode::LW) ? LSQEntryType::LOAD : LSQEntryType::STORE;
  entry.dest_tag = rob_index;

  int addr_val = 0, addr_tag = -1;
  bool addr_ready = true;
  if (instr.src1 >= 0 && instr.src1 < static_cast<int>(RAT.size())) {
    if (RAT[instr.src1].isValid) {
      addr_tag = RAT[instr.src1].tag;
      addr_ready = false;
      if (ROB[addr_tag].ready_bit) { addr_ready = true; addr_val = ROB[addr_tag].value; }
    } else {
      addr_val = ARF[instr.src1];
    }
  }
  entry.addr_tag = addr_tag;
  entry.addr_val = addr_val;
  entry.addr_ready = addr_ready;
  entry.offset = instr.imm;

  int data_val = 0, data_tag = -1;
  bool data_ready = true;
  if (entry.type == LSQEntryType::STORE) {
    if (instr.src2 >= 0 && instr.src2 < static_cast<int>(RAT.size())) {
      if (RAT[instr.src2].isValid) {
        data_tag = RAT[instr.src2].tag;
        data_ready = false;
        if (ROB[data_tag].ready_bit) { data_ready = true; data_val = ROB[data_tag].value; }
      } else {
        data_val = ARF[instr.src2];
      }
    }
  }
  
  entry.data_tag = data_tag;
  entry.data_val = data_val;
  entry.data_ready = data_ready;

  entry.dispatched = false;
  entry.cycles_left = 0;
  entry.done = false;
  entry.broadcasted = false;
  entry.result = 0;
  entry.exception = false;
  entry.eff_addr = 0;

  q.push_back(entry);
  lsq_filled++;
}

void LoadStoreQueue::flush()
{
  q.clear();
  lsq_filled = 0;
  has_result = false;
  has_exception = false;
}
