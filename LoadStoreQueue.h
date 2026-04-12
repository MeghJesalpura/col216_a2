#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include "Basics.h"

enum LSQEntryType
{
  LOAD,
  STORE
};

struct LSQEntry
{
  LSQEntryType type;

  int dest_tag;

  int addr_tag;
  int addr_val;
  bool addr_ready;

  int offset;

  int data_tag;
  int data_val;
  bool data_ready;

  bool dispatched;
  int cycles_left;
  bool done;
  bool broadcasted;
  int result;
  bool exception;
  int eff_addr;
};

class LoadStoreQueue
{
public:
  int latency;
  int lsq_capacity;
  int lsq_filled;
  int lsq_active;

  bool has_result = false;
  bool has_exception = false;
  int result_tag = 0;
  int result_val = 0;

  std::deque<LSQEntry> q;

  LoadStoreQueue(int latency, int lsq_capacity = 32);

  bool has_space();
  void capture(int tag, int val);
  void createLSQEntry(const Instruction &instr, int rob_index, const std::vector<RATEntry> &RAT, const std::vector<int> &ARF, std::vector<ROBEntry> &ROB);
  void executeCycle(std::vector<int> &Memory);
  void dispatchReady();

  void commitEntry(int tag, std::vector<int> &Memory);

  void flush();
};