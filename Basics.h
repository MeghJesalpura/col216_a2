#pragma once
#include <string>

enum class OpCode
{
  ADD,
  SUB,
  ADDI,
  MUL,
  DIV,
  REM,
  LW,
  SW,
  BEQ,
  BNE,
  BLT,
  BLE,
  J,
  SLT,
  SLTI,
  AND,
  OR,
  XOR,
  ANDI,
  ORI,
  XORI
};
enum class UnitType
{
  ADDER,
  MULTIPLIER,
  DIVIDER,
  LOADSTORE,
  BRANCH,
  LOGIC
};

struct Instruction
{
  bool fetched = false;

  OpCode op;
  int dest;
  int src1;
  int src2;
  int imm;
  int pc;
};

struct ProcessorConfig
{
  int num_regs = 32;
  int rob_size = 64;
  int mem_size = 1024;

  int logic_lat = 1;
  int add_lat = 2;
  int mul_lat = 4;
  int div_lat = 5;
  int mem_lat = 4;

  int logic_rs_size = 4;
  int adder_rs_size = 4;
  int mult_rs_size = 2;
  int div_rs_size = 2;
  int br_rs_size = 2;
  int lsq_rs_size = 32;
};

struct ROBEntry
{
  bool valid_bit = false;
  bool ready_bit = false;
  int reg_id = 0;
  int value = 0;
  int pc_entry = 0;

  ROBEntry() : valid_bit(false), ready_bit(false), reg_id(0), value(0), pc_entry(0) {}
  ROBEntry(bool tvalid, bool tready, int id, int pc = 0)
  {
    valid_bit = tvalid;
    ready_bit = tready;
    reg_id = id;
    value = 0;
    pc_entry = pc;
  }
};

struct RSEntry
{
  OpCode opcode; // what operation to perform
  int dest_tag;

  // value, tag, ready ... for both operands
  int val1;
  int tag1;
  bool ready1;

  int val2;
  int tag2;
  bool ready2;
  // other fields as required
  bool isValid = false;    // true when is filled with a valid instruction
  bool dispatched = false; // true when the instruction has been dispatched to execution (for tracking in-order completion)
  RSEntry() : opcode(OpCode::ADD), dest_tag(0),
              val1(0), tag1(0), ready1(false),
              val2(0), tag2(0), ready2(false), isValid(false), dispatched(false) {}

  RSEntry(OpCode op, int dest, int v1, int t1, bool r1, int v2, int t2, bool r2, bool valid, bool disp = false)
  {
    opcode = op;
    dest_tag = dest;
    val1 = v1;
    tag1 = t1;
    ready1 = r1;
    val2 = v2;
    tag2 = t2;
    ready2 = r2;
    isValid = valid;
    dispatched = disp;
  }
};

struct RATEntry
{
  int tag;      // ROB tag this register is waiting on
  int val;      // last known committed value
  bool isValid; // true => register is renamed (pending ROB result), false => val is current

  RATEntry() : tag(-1), val(0), isValid(false) {}

  RATEntry(int v, int t, bool valid)
  {
    tag = t;
    val = v;
    isValid = valid;
  }
};

struct CDBEntry
{
  int tag;
  int value;
  bool exception;
  bool valid;
  CDBEntry() : tag(-1), value(0), exception(false), valid(true) {}
};