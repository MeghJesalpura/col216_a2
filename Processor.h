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
#include <map>

struct InstructionTrace
{
    long long sequence_num;
    std::string raw;
    std::map<int, std::string> cycle_to_stage;
    bool flushed = false;
    int first_cycle = -1;
    int last_cycle = -1;
};

class Processor
{
private:
    int selectUnitForOpcode(const Instruction &instr, bool &is_lsq);

public:
    int pc;
    int my_pc;
    int clock_cycle;
    int curr_tag;

    long long next_sequence_num = 0;
    std::vector<InstructionTrace> traces;

    // pipeline registers

    std::vector<Instruction> inst_memory;
    Instruction fetched_instr;

    // architectural state (do not change)
    std::vector<int> ARF;    // regFile
    std::vector<int> Memory; // Memory
    bool exception = false;
    bool flushed_this_cycle = false; // exception bit

    // register alias table / reorder buffer

    std::vector<ROBEntry> ROB;
    int rob_start;
    int rob_end;
    int rob_capacity;
    std::vector<RATEntry> RAT;
    int rob_cnt;
    std::vector<ExecutionUnit> units;

    LoadStoreQueue *lsq;
    BranchPredictor bp;

    std::vector<CDBEntry> CDB; // Common Data Bus for broadcasting results from execution units and LSQ

    int pc_last_executed = -1; // To track the last executed instruction's PC for exception handling
    int exception_pc = -1;     // PC of the instruction that caused an exception

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

    void dumpPipelineTrace();
};