#include "Basics.h"
#include "ExecutionUnit.h"

using std::cout;

ExecutionUnit::ExecutionUnit(UnitType tname, int lat, int rs_capacity)
{
  name = tname;
  latency = lat;
  capacity = rs_capacity;
  RS.resize(rs_capacity);
  instr_list.resize(lat, -1);
}

int ExecutionUnit::findFreeEntry()
{
  // Advance head past any entries that are no longer busy (marked as done)
  for (int i = 0; i < capacity; i++)
  {
    if (!RS[i].isValid)
      return i;
  }
  return -1;
}

void resolveOperand(int reg, const std::vector<RATEntry> &RAT, const std::vector<int> &ARF, const std::vector<ROBEntry> &ROB, int &out_val, int &out_tag, bool &out_ready)
{
  if (reg < 0 || reg >= static_cast<int>(RAT.size()))
  {
    out_val = 0;
    out_tag = -1;
    out_ready = true;
    return;
  }

  const RATEntry &r = RAT[reg];
  if (!(r.tag == -1))
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

bool ExecutionUnit::has_space()
{
  return (findFreeEntry() != -1);
}

void ExecutionUnit::createRSEntry(Instruction &instr, int rob_index, const std::vector<RATEntry> &RAT, const std::vector<int> &ARF, const std::vector<ROBEntry> &ROB)
{
  int v1, t1, v2, t2;
  bool r1, r2;

  resolveOperand(instr.src1, RAT, ARF, ROB, v1, t1, r1);

  bool is_immediate = (instr.op == OpCode::ADDI ||
                       instr.op == OpCode::SLTI ||
                       instr.op == OpCode::ANDI ||
                       instr.op == OpCode::ORI ||
                       instr.op == OpCode::XORI ||
                       instr.op == OpCode::LW ||
                       instr.op == OpCode::SW);

  if (is_immediate)
  {
    v2 = instr.imm;
    t2 = -1;
    r2 = true;
  }
  else
  {
    resolveOperand(instr.src2, RAT, ARF, ROB, v2, t2, r2);
  }

  if (!has_space())
  {
    // Should not happen if has_space() checked
    return;
  }
  int ind = findFreeEntry();
  RS[ind] = RSEntry(instr.op, rob_index, v1, t1, r1, v2, t2, r2, true);
  static unsigned long long seq_counter = 0;
  RS[ind].seq_num = ++seq_counter;
}

void ExecutionUnit::capture(int tag, int val)
{
  for (int i = 0; i < capacity; i++)
  {
    if (RS[i].isValid)
    {
      if (!RS[i].ready1 && RS[i].tag1 == tag)
      {
        RS[i].val1 = val;
        RS[i].ready1 = true;
      }
      if (!RS[i].ready2 && RS[i].tag2 == tag)
      {
        RS[i].val2 = val;
        RS[i].ready2 = true;
      }
    }
  }
}

int ExecutionUnit::helper(OpCode op, int val1, int val2)
{
  switch (op)
  {
  case OpCode::ADD:
  case OpCode::ADDI:
    return val1 + val2;
  case OpCode::SUB:
    return val1 - val2;
  case OpCode::MUL:
    return val1 * val2;
  case OpCode::DIV:
    if (val2 == 0)
    {
      has_exception = true;
      return 0;
    }
    return val1 / val2;
  case OpCode::REM:
    if (val2 == 0)
    {
      has_exception = true;
      return 0;
    }
    return val1 % val2;
  case OpCode::SLT:
  case OpCode::SLTI:
    return (val1 < val2);
  case OpCode::AND:
  case OpCode::ANDI:
    return val1 & val2;
  case OpCode::OR:
  case OpCode::ORI:
    return val1 | val2;
  case OpCode::XOR:
  case OpCode::XORI:
    return val1 ^ val2;
  case OpCode::BEQ:
    return (val1 == val2) ? 1 : 0;
  case OpCode::BNE:
    return (val1 != val2) ? 1 : 0;
  case OpCode::BLT:
    return (val1 < val2) ? 1 : 0;
  case OpCode::BLE:
    return (val1 <= val2) ? 1 : 0;
  case OpCode::J:
    return 1; // unconditional jump is always "taken"
  default:
    return 0;
  }
}

void ExecutionUnit::executeCycle()
{
  has_result = false;
  has_exception = false;
  if (instr_list[latency - 1] != -1)
  {
    int idx = instr_list[latency - 1];
    has_result = true;
    result_tag = RS[idx].dest_tag;
    // Compute result based on opcode and operand values
    result_val = helper(RS[idx].opcode, RS[idx].val1, RS[idx].val2);
    RS[idx].isValid = false; // Mark the entry as done
  }

  for (int i = latency - 2; i >= 0; i--)
  {
    instr_list[i + 1] = instr_list[i];
  }

  instr_list[0] = -1;
}

void ExecutionUnit::dispatchReady()
{
  if (instr_list[0] != -1)
    return; // Pipeline slot 0 already occupied

  int oldest_idx = -1;
  unsigned long long oldest_seq = (unsigned long long)-1;

  for (int i = 0; i < capacity; i++)
  {
    if (RS[i].isValid && RS[i].ready1 && RS[i].ready2 && !RS[i].dispatched)
    {
      if (oldest_idx == -1 || RS[i].seq_num < oldest_seq)
      {
        oldest_idx = i;
        oldest_seq = RS[i].seq_num;
      }
    }
  }

  if (oldest_idx != -1)
  {
    instr_list[0] = oldest_idx;
    RS[oldest_idx].dispatched = true; // Mark as dispatched to prevent re-dispatch
  }
}

void ExecutionUnit::flush()
{
  for (int i = 0; i < capacity; ++i)
  {
    RS[i].isValid = false;
    RS[i].dispatched = false;
  }
  for (int i = 0; i < latency; ++i)
  {
    instr_list[i] = -1;
  }
  has_result = false;
  has_exception = false;
  result_tag = 0;
  result_val = 0;
}