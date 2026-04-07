#include "Processor.h"

using std::cout;

Processor::Processor(ProcessorConfig &config)
{
  cout << "[DEBUG][Processor] initialize" << '\n';
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
  std::cout << "[DEBUG][loadProgram] reading " << filename << '\n';
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
  cout << "[DEBUG][loadProgram] loaded " << inst_memory.size() << " instructions" << '\n';
}

void Processor::stageFetch()
{
  cout << "[DEBUG][Fetch] pc=" << pc
       << " rob_start=" << rob_start
       << " rob_end=" << rob_end
       << " inst_count=" << inst_memory.size() << '\n';
  if (pc < 0 || pc / 4 >= static_cast<int>(inst_memory.size()))
  {
    cout << "[DEBUG][Fetch] no instruction fetched" << '\n';
    fetched_instr.fetched = false;
    return;
  }

  fetched_instr = inst_memory[pc / 4];
  fetched_instr.pc = pc; // Ensure PC is stored in instruction
  fetched_instr.fetched = true;
  cout << "[DEBUG][Fetch] fetched opcode=" << static_cast<int>(fetched_instr.op)
       << " imm=" << fetched_instr.imm
       << " pc=" << fetched_instr.pc << '\n';

  bool is_branch = (fetched_instr.op == OpCode::BEQ || fetched_instr.op == OpCode::BNE ||
                    fetched_instr.op == OpCode::BLT || fetched_instr.op == OpCode::BLE);

  if (is_branch)
  {
    if (bp.predict(pc, fetched_instr.imm, fetched_instr.op))
    {
      cout << "[DEBUG][Fetch] branch predicted taken" << '\n';
      pc = pc + fetched_instr.imm * 4;
    }
    else
    {
      cout << "[DEBUG][Fetch] branch predicted not taken" << '\n';
      pc = pc + 4;
    }
  }
  else if (fetched_instr.op == OpCode::J)
  {
    cout << "[DEBUG][Fetch] jump taken" << '\n';
    pc = pc + fetched_instr.imm * 4;
  }
  else
  {
    pc = pc + 4;
  }
  cout << "[DEBUG][Fetch] next pc=" << pc << '\n';
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
  cout << "[DEBUG][Decode] fetched=" << fetched_instr.fetched << '\n';
  if (!fetched_instr.fetched)
    return;

  bool is_lsq = false;
  int unit_idx = selectUnitForOpcode(fetched_instr, is_lsq);

  if (is_lsq)
  {
    if (!lsq->has_space())
    {
      cout << "[DEBUG][Decode] LSQ full" << '\n';
      return;
    }
  }
  else if (unit_idx != -1)
  {
    if (!units[unit_idx].has_space())
    {
      cout << "[DEBUG][Decode] unit " << unit_idx << " full" << '\n';
      return;
    }
  }
  else
  {
    cout << "[DEBUG][Decode] unsupported opcode" << '\n';
    return;
  }

  if ((rob_end + 1) % rob_capacity == rob_start)
  {
    cout << "[DEBUG][Decode] ROB full" << '\n';
    return;
  }

  int rob_index = rob_end;
  cout << "[DEBUG][Decode] allocating ROB index=" << rob_index << '\n';

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
  cout << "[DEBUG][Decode] rob_end advanced to " << rob_end << '\n';

  if (dest > 0)
  { // Do not rename x0
    RATEntry rat_entry(0, rob_index, true);
    RAT[dest] = rat_entry;
  }

  fetched_instr.fetched = false;
}

void Processor::flush()
{
  cout << "[DEBUG][Flush] called" << '\n';
}

void Processor::stageExecuteAndBroadcast()
{
  cout << "[DEBUG][Execute] begin" << '\n';
  // calls for execution in all units and lsq and then broadcasts the ready results
  for (auto &unit : units)
  {
    cout << "[DEBUG] Broadcasting for unit=" << static_cast<int>(unit.name) << '\n';
    unit.executeCycle();
    cout << "[DEBUG] Cycle executed" << "\n";
    if (unit.has_result)
    {
      cout << "[DEBUG][Execute] unit=" << static_cast<int>(unit.name)
           << " result_tag=" << unit.result_tag
           << " value=" << unit.result_val
           << " exception=" << unit.has_exception << '\n';
      CDBEntry cdb_entry;
      cdb_entry.tag = unit.result_tag;
      cdb_entry.value = unit.result_val;
      cdb_entry.exception = unit.has_exception;
      cdb_entry.valid = true;
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
    cout << "[DEBUG][Execute] lsq result_tag=" << lsq->result_tag
         << " value=" << lsq->result_val
         << " exception=" << lsq->has_exception << '\n';
    CDBEntry cdb_entry;
    cdb_entry.tag = lsq->result_tag;
    cdb_entry.value = lsq->result_val;
    cdb_entry.exception = lsq->has_exception;
    cdb_entry.valid = true;
    CDB[5] = cdb_entry;
  }

  broadcastOnCDB();
}

void Processor::broadcastOnCDB()
{
  cout << "[DEBUG][CDB] broadcast begin" << '\n';
  for (auto &entry : CDB)
  {
    if (!entry.valid)
    {
      cout << "[DEBUG][CDB] entry invalid, skipping" << '\n';
      continue;
    }
    int tag = entry.tag;
    int value = entry.value;
    bool exception = entry.exception;
    cout << "[DEBUG][CDB] tag=" << tag << " value=" << value << " exception=" << exception << '\n';
    entry.valid = false; // Mark as consumed
    for (auto &unit : units)
    {
      unit.capture(tag, value);
    }
    lsq->capture(tag, value);

    cout << "[DEBUG][CDB] updating ROB tag=" << tag << " value=" << value << " exception=" << exception << '\n';
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
  cout << "[DEBUG][step] cycle " << clock_cycle << " start" << '\n';
  stageDecode();
  stageFetch();
  stageExecuteAndBroadcast();
  stageCommit();

  cout << "[DEBUG][step] cycle " << clock_cycle
       << " end pc=" << pc
       << " rob_start=" << rob_start
       << " rob_end=" << rob_end
       << " exception=" << exception << '\n';

  return (pc < static_cast<int>(inst_memory.size() * 4)) || (rob_end != rob_start);
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
  std::cout << "[DEBUG][Commit] rob_start=" << rob_start << " rob_end=" << rob_end << '\n';
  if (rob_start == rob_end)
  {
    std::cout << "[DEBUG][Commit] ROB empty" << '\n';
    return;
  }

  ROBEntry &entry = ROB[rob_start];
  if (!entry.ready_bit)
  {
    std::cout << "[DEBUG][Commit] head entry not ready" << '\n';
    return;
  }

  if (exception)
  {
    std::cout << "[DEBUG][Commit] exception detected, flushing" << '\n';
    flush();
    // need to go back to the last pc state before exception was encountered and
    // need to halt the program
    pc = pc_last_executed;
    return;
  }

  std::cout << "[DEBUG][Commit] committing tag=" << rob_start
            << " reg_id=" << entry.reg_id
            << " value=" << entry.value << '\n';

  if (entry.reg_id >= 0 && entry.reg_id < static_cast<int>(ARF.size()))
  {
    ARF[entry.reg_id] = entry.value;
    if (RAT[entry.reg_id].tag == rob_start)
    {
      RAT[entry.reg_id].isValid = false;
      RAT[entry.reg_id].tag = -1;
    }
  }

  lsq->commitEntry(rob_start, Memory);
  rob_start = (rob_start + 1) % rob_capacity;
  std::cout << "[DEBUG][Commit] rob_start advanced to " << rob_start << '\n';

  // update branch predictor if this instruction was a branch
  Instruction &committed_instr = inst_memory[entry.pc_entry / 4];
  if (committed_instr.op == OpCode::BEQ || committed_instr.op == OpCode::BNE || committed_instr.op == OpCode::BLT || committed_instr.op == OpCode::BLE)
  {
    bool taken = (entry.value != 0); // Assuming non-zero means taken for branches
    int actual_target = entry.pc_entry + committed_instr.imm * 4;
    bool was_correct = (taken && actual_target == pc) || (!taken && actual_target != pc);
    bp.update(entry.pc_entry, actual_target, taken, was_correct);
  }
}