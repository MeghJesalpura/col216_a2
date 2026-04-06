#pragma once
#include <iostream>
#include <vector>
#include <string>
#include "Basics.h"

class ExecutionUnit
{
public:
    // per-unit reservation station
    std::vector<RSEntry> RS;

    UnitType name;
    int latency;
    int rs_size;
    int rs_start;
    int rs_end;
    bool has_result = false;    // result flag
    bool has_exception = false; // exception flag
    ExecutionUnit(UnitType tname, int val, int size);
    void capture(int tag, int val);
    void executeCycle();
    bool has_space();
    void createRSEntry(Instruction &instr, int rob_index);
};