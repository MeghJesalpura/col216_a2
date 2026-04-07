#include "Processor.h"

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
    if (pc < 0 || pc / 4 >= static_cast<int>(inst_memory.size()))
    {
        fetched_instr.fetched = false;
        return;
    }

    fetched_instr = inst_memory[pc / 4];
    fetched_instr.pc = pc; // Ensure PC is stored in instruction
    fetched_instr.fetched = true;

    bool is_branch = (fetched_instr.op == OpCode::BEQ || fetched_instr.op == OpCode::BNE ||
                      fetched_instr.op == OpCode::BLT || fetched_instr.op == OpCode::BLE);

    if (is_branch)
    {
        if (bp.predict(pc, fetched_instr.imm, fetched_instr.op))
        {
            pc = pc + fetched_instr.imm * 4;
        }
        else
        {
            pc = pc + 4;
        }
    }
    else if (fetched_instr.op == OpCode::J)
    {
        pc = pc + fetched_instr.imm * 4;
    }
    else
    {
        pc = pc + 4;
    }
}

int Processor::selectUnitForOpcode(const Instruction &instr, bool &is_lsq)
{
    is_lsq = false;
    switch (instr.op)
    {
    case OpCode::ADD:
    case OpCode::SUB:
    case OpCode::ADDI:
        return 0;
    case OpCode::MUL:
        return 1;
    case OpCode::DIV:
    case OpCode::REM:
        return 2;
    case OpCode::BEQ:
    case OpCode::BNE:
    case OpCode::BLT:
    case OpCode::BLE:
    case OpCode::J:
        return 3;
    case OpCode::SLT:
    case OpCode::SLTI:
    case OpCode::AND:
    case OpCode::OR:
    case OpCode::XOR:
    case OpCode::ANDI:
    case OpCode::ORI:
    case OpCode::XORI:
        return 4;
    case OpCode::LW:
    case OpCode::SW:
        is_lsq = true;
        return -1;
    default:
        return -1;
    }
}

void Processor::stageDecode()
{
    if (!fetched_instr.fetched)
        return;

    bool is_lsq = false;
    int unit_idx = selectUnitForOpcode(fetched_instr, is_lsq);

    if (is_lsq)
    {
        if (!lsq->has_space())
            return;
    }
    else if (unit_idx != -1)
    {
        if (!units[unit_idx].has_space())
            return;
    }
    else
    {
        return;
    }

    if ((rob_end + 1) % rob_capacity == rob_start)
        return;

    int rob_index = rob_end;

    if (is_lsq)
    {
        lsq->createLSQEntry(fetched_instr, rob_index, RAT, ARF, ROB);
    }
    else
    {
        units[unit_idx].createRSEntry(fetched_instr, rob_index, RAT, ARF, ROB);
    }

    int dest = fetched_instr.dest;
    if (fetched_instr.op == OpCode::SW || fetched_instr.op == OpCode::BEQ ||
        fetched_instr.op == OpCode::BNE || fetched_instr.op == OpCode::BLT ||
        fetched_instr.op == OpCode::BLE || fetched_instr.op == OpCode::J)
    {
        dest = -1;
    }

    ROBEntry rob_entry(true, false, dest);
    ROB[rob_end] = rob_entry;
    rob_end = (rob_end + 1) % rob_capacity;

    if (dest > 0)
    { // Do not rename x0
        RATEntry rat_entry(0, rob_index, true);
        RAT[dest] = rat_entry;
    }

    fetched_instr.fetched = false;
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