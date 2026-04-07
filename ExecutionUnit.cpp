#include "Basics.h"
#include "ExecutionUnit.h"

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
  if (!r.isValid)
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
    return val1 / val2;
  case OpCode::REM:
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
  default:
    return 0;
  }
}

void ExecutionUnit::executeCycle()
{
  if (instr_list[latency - 1] != -1)
  {
    int idx = instr_list[latency - 1];
    has_result = true;
    result_tag = RS[idx].dest_tag;
    // Compute result based on opcode and operand values
    result_val = helper(RS[idx].opcode, RS[idx].val1, RS[idx].val2);
    RS[idx].isValid = false; // Mark the entry as done
    // now sets the corresponding ROB entry to 1
    RS[idx].dispatched = true;
  }
  for (int i = latency - 2; i >= 0; i--)
  {
    instr_list[i + 1] = instr_list[i];
  }

  for (int i = 0; i < capacity; i++)
  {
    if (RS[i].isValid && RS[i].ready1 && RS[i].ready2)
    {
      instr_list[0] = i;
      break;
    }
  }
}

void ExecutionUnit::flush()
{
  for (int i = 0; i < capacity; ++i)
  {
    RS[i].isValid = false;
  }
  has_result = false;
  has_exception = false;
  result_tag = 0;
  result_val = 0;
}