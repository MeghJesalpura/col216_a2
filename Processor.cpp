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
  CDB.resize(6);

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
  if (pc >= static_cast<int>(inst_memory.size() * 4) && rob_end == rob_start)
  {
    terminated = true;
    return;
  }
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

  ROBEntry rob_entry(true, false, dest, fetched_instr.pc);
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

void Processor::stageExecuteAndBroadcast()
{
  // calls for execution in all units and lsq and then broadcasts the ready results
  for (auto &unit : units)
  {
    unit.executeCycle();
    if (unit.has_result)
    {
      CDBEntry cdb_entry;
      cdb_entry.tag = unit.result_tag;
      cdb_entry.value = unit.result_val;
      cdb_entry.exception = unit.has_exception;
      if (unit.name == UnitType::ADDER)
      {
        CDB[0] = cdb_entry;
      }
      else if (unit.name == UnitType::MULTIPLIER)
      {
        CDB[1] = cdb_entry;
      }
      else if (unit.name == UnitType::DIVIDER)
      {
        CDB[2] = cdb_entry;
      }
      else if (unit.name == UnitType::BRANCH)
      {
        CDB[3] = cdb_entry;
      }
      else if (unit.name == UnitType::LOGIC)
      {
        CDB[4] = cdb_entry;
      }
    }
  }
  lsq->executeCycle(Memory);
  if (lsq->has_result)
  {
    CDBEntry cdb_entry;
    cdb_entry.tag = lsq->result_tag;
    cdb_entry.value = lsq->result_val;
    cdb_entry.exception = lsq->has_exception;
    CDB[5] = cdb_entry;
  }

  broadcastOnCDB();
}

void Processor::broadcastOnCDB()
{
  for (auto &entry : CDB)
  {
    if (!entry.valid)
      continue;
    int tag = entry.tag;
    int value = entry.value;
    bool exception = entry.exception;
    entry.valid = false; // Mark as consumed
    for (auto &unit : units)
    {
      unit.capture(tag, value);
    }
    lsq->capture(tag, value);

    ROB[tag].ready_bit = true;
    ROB[tag].value = value;

    // If there was an exception, set the processor's exception bit
    if (exception)
    {
      this->exception = true;
    }
  }
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

/*Commit stage - checks exceptions, branch mispredictions,
commit changes from oldest valid ROB entry and
make changes to ARF and RAT(if required - need to check)*/
void Processor::stageCommit()
{
  if (rob_start == rob_end)
    return;

  ROBEntry &entry = ROB[rob_start];
  if (!entry.ready_bit)
    return;

  if (exception)
  {
    flush();
    // need to go back to the last pc state before exception was encountered and
    // need to halt the program
    pc = pc_last_executed;
    return;
  }

  ARF[entry.reg_id] = entry.value;
  if (RAT[entry.reg_id].tag == rob_start)
  {
    RAT[entry.reg_id].isValid = true;
    RAT[entry.reg_id].tag = -1;
  }
}