#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include "Basics.h"
#include "BranchPredictor.h"
#include "ExecutionUnit.h"
#include "LoadStoreQueue.h"

#include "compiler/compiler.h"

class Processor
{
private:
public:
    int pc;
    int clock_cycle;
    int curr_tag;
    // pipeline registers

    std::vector<Instruction> inst_memory;
    Instruction fetched_instr;

    // architectural state (do not change)
    std::vector<int> ARF;    // regFile
    std::vector<int> Memory; // Memory
    bool exception = false;  // exception bit

    // register alias table / reorder buffer

    std::vector<ROBEntry> ROB;
    int rob_start;
    int rob_end;
    int rob_capacity;
    std::vector<RATEntry> RAT;
    std::vector<ExecutionUnit> units;

    LoadStoreQueue *lsq;
    BranchPredictor bp;

    Processor(ProcessorConfig &config);

    void loadProgram(const std::string &filename);

    void flush();

    void broadcastOnCDB();

    void stageFetch();

    void stageDecode();

    void stageExecuteAndBroadcast();

    void stageCommit();

    bool step();

    void dumpArchitecturalState();
};