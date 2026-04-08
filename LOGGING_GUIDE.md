# Pipeline Instruction Logging Guide

## Overview
Logging functions have been added to help you track which instruction is in which pipeline stage for each cycle.

## Available Functions

### 1. `logInstructionStages()` - Basic Logging (Default)
**Status:** Currently called automatically at the end of each cycle
**Output:** Compact format showing one instruction per stage

**Example Output:**
```
========== CYCLE 5 ==========
[FETCH]  PC=  0 | ADD
[DECODE] (empty)
[EXECUTE] PC=  4 | ROB[1] | ADDER | ADD | 
[COMMIT] (empty)
===============================
```

**What it shows:**
- **FETCH:** Instruction being fetched (PC and opcode)
- **DECODE:** Instructions waiting to be dispatched to execution units
- **EXECUTE:** Instructions in execution units or LoadStoreQueue (shows PC, ROB tag, execution unit, opcode)
- **COMMIT:** Instructions ready to commit (shows PC, ROB tag, opcode)

### 2. `logInstructionStagesDetailed()` - Detailed Logging
**Status:** Available for manual calls
**Output:** Detailed format showing ALL instructions in each stage

**How to use:**
Add this line in your code where you want detailed logging:
```cpp
cpu.logInstructionStagesDetailed();
```

**Example Output:**
```
========== DETAILED CYCLE 5 ==========
[FETCH STAGE]
  PC=   0 | ADD | rd=1 rs1=1 rs2=1
[DECODE STAGE]
  (empty)
[EXECUTE STAGE]
  PC=   4 | ROB[ 1] | ADDER | ADD
  PC=   8 | ROB[ 2] | ADDER | ADD
[COMMIT STAGE] Ready to commit:
  (empty)
==================================
```

**What it shows:**
- Full instruction details (register numbers, values)
- ALL instructions in each stage (not just the first one)
- Better formatted output for detailed analysis

## Helper Functions

### `opcodeToString(OpCode op)`
Converts opcode enum to human-readable string
- Input: OpCode enum value
- Output: String like "ADD", "MUL", "LW", etc.

### `unitTypeToString(UnitType unit)`
Converts execution unit type to human-readable string
- Input: UnitType enum value
- Output: String like "ADDER", "MULTIPLIER", "LSQ", etc.

## Pipeline Stages Explained

1. **FETCH:** Instruction is being read from instruction memory
2. **DECODE:** Instruction is in ROB, register renaming done, waiting for operands
3. **EXECUTE:** Instruction is actively executing in an execution unit or LoadStoreQueue
4. **COMMIT:** Instruction has completed execution and is waiting to commit results

## Example Usage

### To disable automatic logging:
Comment out the line in `Processor::step()`:
```cpp
// logInstructionStages();  // Commented out for quiet mode
```

### To use detailed logging for specific cycles:
Modify `Processor::step()`:
```cpp
void Processor::step() {
    // ... existing code ...
    logInstructionStages();
    
    // Add detailed logging for cycle 10-15
    if (clock_cycle >= 10 && clock_cycle <= 15) {
        logInstructionStagesDetailed();
    }
    
    return more_work && !exception;
}
```

### To log only when there's activity:
```cpp
void Processor::step() {
    // ... existing code ...
    
    // Only log if ROB is not empty
    if (rob_cnt > 0 || fetched_instr.fetched) {
        logInstructionStages();
    }
    
    return more_work && !exception;
}
```

## Tips for Analysis

- **PC (Program Counter):** Shows memory address of the instruction (multiply by 4 to get byte offset)
- **ROB[N]:** Reorder Buffer entry number for that instruction
- **Unit Name:** Which execution unit is executing the instruction (ADDER, MULTIPLIER, etc.)
- **Opcode:** The operation being performed (ADD, MUL, LOAD, etc.)

## Matching Instructions Across Cycles

By tracking PC values, you can follow a single instruction through the pipeline:
```
Cycle 1: Instruction at PC=0 is in FETCH
Cycle 2: Instruction at PC=0 is in DECODE (implicit)
Cycle 3: Instruction at PC=0 is in EXECUTE
Cycle 5: Instruction at PC=0 is in COMMIT
```

This makes it easy to verify pipeline behavior and identify stalls or bottlenecks!
