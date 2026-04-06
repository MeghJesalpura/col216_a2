#include "Basics.h"
#include "ExecutionUnit.h"

ExecutionUnit::ExecutionUnit(UnitType tname, int val)
{
    name = tname;
    latency = val;
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