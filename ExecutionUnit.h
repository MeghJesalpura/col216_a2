#pragma once
#include <iostream>
#include <vector>
#include <deque>
#include <string>
#include "Basics.h"

class ExecutionUnit
{
private:
  int helper(OpCode op, int val1, int val2);

public:
  std::vector<RSEntry> RS;
  int capacity;

  UnitType name;
  int latency;

  int findFreeEntry();

  bool has_result = false;
  bool has_exception = false;
  int result_tag = 0;
  int result_val = 0;
  std::vector<int> instr_list;
  ExecutionUnit(UnitType tname, int latency, int rs_capacity);
  bool has_space();
  void createRSEntry(Instruction &instr, int rob_index, const std::vector<RATEntry> &RAT, const std::vector<int> &ARF, const std::vector<ROBEntry> &ROB);
  void capture(int tag, int val);
  void executeCycle();
  void flush();
};
