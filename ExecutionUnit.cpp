#include "Basics.h"
#include "ExecutionUnit.h"

ExecutionUnit::ExecutionUnit(UnitType tname, int lat, int rs_capacity)
{
  name = tname;
  latency = lat;
  rs_size = rs_capacity;
  capacity = rs_capacity;
  RS.resize(rs_capacity);
  head = -1;
  tail = -1;
}

int ExecutionUnit::get_rs_filled()
{
  // Advance head past any entries that are no longer busy (marked as done)
  while (head != -1 && !RS[head].busy) {
    if (head == tail) {
      // Buffer is completely empty now
      head = -1;
      tail = -1;
      break;
    }
    head = (head + 1) % capacity;
  }

  if (head == -1) return 0;
  return (tail - head + capacity) % capacity + 1;
}

void resolveOperand(int reg, const std::vector<RATEntry> &RAT, const std::vector<int> &ARF, int &out_val, int &out_tag, bool &out_ready)
{
  if (reg < 0 || reg >= static_cast<int>(RAT.size()))
  {
    out_val = 0;
    out_tag = -1;
    out_ready = true;
    return;
  }

  const RATEntry &r = RAT[reg];
  if (r.isValid)
  {
    out_val = 0;
    out_tag = r.tag;
    out_ready = false;
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
  return get_rs_filled() < capacity;
}

void ExecutionUnit::createRSEntry(Instruction &instr, int rob_index, const std::vector<RATEntry> &RAT, const std::vector<int> &ARF)
{
  int v1, t1, v2, t2;
  bool r1, r2;

  resolveOperand(instr.src1, RAT, ARF, v1, t1, r1);

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
    resolveOperand(instr.src2, RAT, ARF, v2, t2, r2);
  }

  if (get_rs_filled() >= capacity) {
    // Should not happen if has_space() checked
    return;
  }
  if (head == -1) {
    head = 0;
    tail = 0;
  } else {
    tail = (tail + 1) % capacity;
  }

  RS[tail] = RSEntry(instr.op, rob_index, v1, t1, r1, v2, t2, r2);
  RS[tail].busy = true;
}

void ExecutionUnit::capture(int tag, int val)
{
}

void ExecutionUnit::executeCycle()
{
  if (name == UnitType::ADDER)
  {
  }
}

void ExecutionUnit::flush()
{
  for (int i = 0; i < capacity; ++i) {
    RS[i].busy = false;
  }
  head = -1;
  tail = -1;
  has_result = false;
  has_exception = false;
  result_tag = 0;
  result_val = 0;
}