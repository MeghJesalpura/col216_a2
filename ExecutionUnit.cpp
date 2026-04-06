#include "Basics.h"
#include "ExecutionUnit.h"

ExecutionUnit::ExecutionUnit(UnitType tname, int val, int size)
{
    name = tname;
    latency = val;
    rs_size = size;
    RS.resize(rs_size);
}

void ExecutionUnit::createRSEntry(Instruction &instr, int rob_index)
{
    // create reservation station entry for the instruction and add it to the reservation station
    RSEntry rs_entry;
    rs_entry.dest_tag = rob_index;
    // set val1, tag1, ready1 based on src1
    // set val2, tag2, ready2 based on src2 or imm
    RS[rs_end] = rs_entry;
    rs_end = (rs_end + 1) % rs_size;
}

bool ExecutionUnit::has_space()
{
    return (rs_size > (rs_end + 1 - rs_start + rs_size) % rs_size);
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