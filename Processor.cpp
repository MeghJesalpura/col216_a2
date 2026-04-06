#include "Processor.h"

Processor::createRSEntry(Instruction &instr, int rob_index)
{
    if (instr.op == OpCode::ADD || instr.op == OpCode::SUB || instr.op == OpCode::ADDI)
    {
        if (!units[0].has_space())
            return false;
        // create entry in adder reservation station
    }
    else if (instr.op == OpCode::MUL)
    {
        if (!units[1].has_space())
            return false;
        // create entry in multiplier reservation station
    }
    else if (instr.op == OpCode::DIV || instr.op == OpCode::REM)
    {
        if (!units[2].has_space())
            return false;
        // create entry in divider reservation station
    }
    else if (instr.op == OpCode::BEQ || instr.op == OpCode::BNE || instr.op == OpCode::BLT || instr.op == OpCode::BLE)
    {
        if (!units[3].has_space())
            return false;
        // create entry in branch comparison reservation station
    }
    else if (instr.op == OpCode::AND || instr.op == OpCode::OR || instr.op == OpCode::XOR || instr.op == OpCode::ANDI || instr.op == OpCode::ORI || instr.op == OpCode::XORI)
    {
        if (!units[4].has_space())
            return false;
    }
    else if (instr.op == OpCode::LW || instr.op == OpCode::SW)
    {
        if (!lsq->has_space())
            return false;
    }
}

Processor::Processor(ProcessorConfig &config)
{
    pc = 0;
    clock_cycle = 0;
    curr_tag = 0;
    rob_start = 0;
    rob_end = 0;
    ARF.resize(config.num_regs, 0);
    Memory.resize(config.mem_size);
    ROB.resize(config.rob_size);
    rob_capacity = config.rob_size;
    RAT.resize(config.num_regs);

    // Instantiate Hardware Units
    // Adder
    // Multiplier
    // Divider
    // Branch Computation
    // Bitwise Logic
    // Load-Store Unit
    ExecutionUnit Adder(UnitType::ADDER, config.add_lat, config.adder_rs_size);
    ExecutionUnit Multiplier(UnitType::MULTIPLIER, config.mul_lat, config.mult_rs_size);
    ExecutionUnit Divider(UnitType::DIVIDER, config.div_lat, config.div_rs_size);
    ExecutionUnit BranchCmpr(UnitType::BRANCH, config.add_lat, config.br_rs_size);
    // mentioned to take Branch Comparison latency equal to addition latency
    ExecutionUnit BitLogic(UnitType::LOGIC, config.logic_lat, config.logic_rs_size);

    lsq = new LoadStoreQueue(config.mem_lat);

    units.push_back(Adder);
    units.push_back(Multiplier);
    units.push_back(Divider);
    units.push_back(BranchCmpr);
    units.push_back(BitLogic);
}

void Processor::loadProgram(const std::string &filename)
{
    std::ifstream file(filename);
    RISCVCompiler compiler;
    if (!file.is_open())
    {
        std::cerr << "Error: failed to open file: " << filename << "\n";
        return;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string riscv_code = buffer.str();
    compiler.compile(riscv_code, "temp.pre");
    inst_memory = compiler.getInstructions();
}

void Processor::stageFetch()
{
    fetched_instr = inst_memory[pc / 4];
    fetched_instr.fetched = true;
}

void Processor::stageDecode()
{
    if (!fetched_instr.fetched)
        return;

    if (rob_capacity == (rob_end + rob_capacity - rob_start) % rob_capacity)
        return;

    // need to also check if the corresponding reservation station has space or not
    // if ()

    ROBEntry rob_entry(true, false, fetched_instr.dest);
    curr_tag = (curr_tag + 1) % rob_capacity;
    ROB[rob_end] = rob_entry;
    rob_end = (rob_end + 1) % rob_capacity;

    RATEntry rat_entry(-1, curr_tag, false);
    RAT[fetched_instr.dest] = rat_entry;
}

void Processor::flush()
{
}

bool Processor::step()
{
    clock_cycle++;
    stageDecode();
    stageFetch();
    stageExecuteAndBroadcast();
    stageCommit();

    return true; // return false if CPU has no more to do after this cycle
}

void Processor::dumpArchitecturalState()
{
    std::cout << "\n=== ARCHITECTURAL STATE (CYCLE " << clock_cycle << ") ===\n";
    for (int i = 0; i < static_cast<int>(ARF.size()); i++)
    {
        std::cout << "x" << i << ": " << std::setw(4) << ARF[i] << " | ";
        if ((i + 1) % 8 == 0)
            std::cout << std::endl;
    }
    if (exception)
    {
        std::cout << "EXCEPTION raised by instruction " << pc + 1 << std::endl;
    }
    std::cout << "Branch Predictor Stats: " << bp.correct_predictions << "/" << bp.total_branches << " correct.\n";
}

void Processor::stageExecuteAndBroadcast()
{
    // TODO: Implement execute and broadcast stage
}

void Processor::stageCommit()
{
    // TODO: Implement commit stage
}