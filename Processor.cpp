#include "Processor.h"

using std::cout;

Processor::Processor(ProcessorConfig &config)
{
  pc = 0;
  my_pc = 0;
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
  rob_cnt = 0;

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

  lsq = new LoadStoreQueue(config.mem_lat, config.lsq_rs_size);

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
  compiler.compile(riscv_code);
  inst_memory = compiler.getInstructions();
  auto temp = compiler.getDataWords();
  for (size_t i = 0; i < temp.size() && i < Memory.size(); i++)
  {
    Memory[i] = temp[i];
  }
  // step();
}

void Processor::stageFetch()
{
  // Don't fetch if previous instruction hasn't been consumed by decode
  if (fetched_instr.fetched)
    return;

  // Don't fetch after exception
  if (exception)
    return;

  // Don't fetch in the same cycle as a flush (branch misprediction)
  if (flushed_this_cycle)
    return;

  if (my_pc / 4 >= static_cast<int>(inst_memory.size()))
  {
    fetched_instr.fetched = false;
    return;
  }

  fetched_instr = inst_memory[my_pc / 4];
  fetched_instr.pc = my_pc; // Ensure PC is stored in instruction
  fetched_instr.fetched = true;
  fetched_instr.sequence_num = next_sequence_num++;

  InstructionTrace trace;
  trace.sequence_num = fetched_instr.sequence_num;
  trace.raw = fetched_instr.raw;
  trace.cycle_to_stage[clock_cycle] = "IF";
  trace.first_cycle = clock_cycle;
  traces.push_back(trace);

  bool is_branch = (fetched_instr.op == OpCode::BEQ || fetched_instr.op == OpCode::BNE ||
                    fetched_instr.op == OpCode::BLT || fetched_instr.op == OpCode::BLE);

  if (is_branch)
  {
    if (bp.predict(my_pc, fetched_instr.imm, fetched_instr.op))
    {
      my_pc = my_pc + fetched_instr.imm * 4;
    }
    else
    {
      my_pc = my_pc + 4;
    }
  }
  else if (fetched_instr.op == OpCode::J)
  {
    my_pc = my_pc + fetched_instr.imm * 4;
  }
  else
  {
    my_pc = my_pc + 4;
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
  case OpCode::SLT:
  case OpCode::SLTI:
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
    return 3;
  case OpCode::J:
    return -1; // Unconditional jump is not dispatched to any unit
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

  // Don't decode after exception
  if (exception)
    return;

  bool is_j = (fetched_instr.op == OpCode::J);
  bool is_lsq = false;
  int unit_idx = selectUnitForOpcode(fetched_instr, is_lsq);

  if (is_j)
  {
    // Jump doesn't need execution unit space
  }
  else if (is_lsq)
  {
    if (!lsq->has_space())
    {
      return;
    }
  }
  else if (unit_idx != -1)
  {
    if (!units[unit_idx].has_space())
    {
      // cout << "Stalling at decode for unit " << unit_idx << " due to lack of reservation station space.\n";
      return;
    }
  }
  else
  {
    return;
  }

  if (rob_cnt == rob_capacity)
  {
    return;
  }

  int rob_index = rob_end;

  if (is_j)
  {
    // Do nothing for RS/LSQ creation
  }
  else if (is_lsq)
  {
    lsq->createLSQEntry(fetched_instr, rob_index, RAT, ARF, ROB);
  }
  else
  {
    // cout << "Dispatching to execution unit " << unit_idx << std::endl;
    units[unit_idx].createRSEntry(fetched_instr, rob_index, RAT, ARF, ROB);
  }

  int dest = fetched_instr.dest;
  if (fetched_instr.op == OpCode::SW || fetched_instr.op == OpCode::BEQ ||
      fetched_instr.op == OpCode::BNE || fetched_instr.op == OpCode::BLT ||
      fetched_instr.op == OpCode::BLE || fetched_instr.op == OpCode::J)
  {
    dest = -1;
  }

  // Record ID stage
  if (traces[fetched_instr.sequence_num].cycle_to_stage.find(clock_cycle) == traces[fetched_instr.sequence_num].cycle_to_stage.end())
    traces[fetched_instr.sequence_num].cycle_to_stage[clock_cycle] = "ID";

  ROBEntry rob_entry;
  if (is_j)
  {
    // Jump finishes immediately upon decode
    rob_entry = ROBEntry(true, true, dest, fetched_instr.pc, fetched_instr.sequence_num);
  }
  else
  {
    rob_entry = ROBEntry(true, false, dest, fetched_instr.pc, fetched_instr.sequence_num);
  }

  // For branches, store the predicted next PC so we can detect mispredictions at commit
  bool is_branch_instr = (fetched_instr.op == OpCode::BEQ || fetched_instr.op == OpCode::BNE ||
                          fetched_instr.op == OpCode::BLT || fetched_instr.op == OpCode::BLE);
  if (is_branch_instr)
  {
    rob_entry.predicted_next_pc = my_pc; // pc was already set by fetch's branch prediction
  }
  ROB[rob_end] = rob_entry;
  rob_end = (rob_end + 1) % rob_capacity;
  rob_cnt++;

  if (dest > 0)
  { // Do not rename x0
    RATEntry rat_entry(0, rob_index, true);
    RAT[dest] = rat_entry;
  }

  fetched_instr.fetched = false;
}

void Processor::flush()
{
  flushed_this_cycle = true;

  // Mark instructions in ROB as flushed
  int current = rob_start;
  for (int i = 0; i < rob_cnt; i++)
  {
    long long seq = ROB[current].sequence_num;
    if (seq != -1)
    {
      traces[seq].flushed = true;
      traces[seq].last_cycle = clock_cycle;
    }
    current = (current + 1) % rob_capacity;
  }

  // Mark fetched_instr as flushed if it exists
  if (fetched_instr.fetched)
  {
    traces[fetched_instr.sequence_num].flushed = true;
    traces[fetched_instr.sequence_num].last_cycle = clock_cycle;
  }

  // Clear all execution unit reservation stations and pipelines
  for (auto &unit : units)
  {
    unit.flush();
  }
  // Clear LSQ
  lsq->flush();

  // Reset ROB
  rob_end = rob_start;
  rob_cnt = 0;

  // Clear RAT (all registers point back to ARF)
  for (auto &rat_entry : RAT)
  {
    rat_entry.isValid = false;
    rat_entry.tag = -1;
  }

  // Clear CDB
  for (auto &cdb_entry : CDB)
  {
    cdb_entry.valid = false;
  }

  // Clear fetched instruction
  fetched_instr.fetched = false;
}

void Processor::stageExecuteAndBroadcast()
{
  // 1. Dispatch newly-ready instructions into pipelines
  for (auto &unit : units)
  {
    unit.dispatchReady();
  }
  lsq->dispatchReady();

  // 2. Record EX stage for all instructions currently in pipelines
  for (auto &unit : units)
  {
    for (int idx : unit.instr_list)
    {
      if (idx != -1)
      {
        int tag = unit.RS[idx].dest_tag;
        long long seq = ROB[tag].sequence_num;
        if (seq != -1)
          traces[seq].cycle_to_stage[clock_cycle] = "EX";
      }
    }
  }
  for (int idx : lsq->instr_list)
  {
    if (idx != -1)
    {
      int tag = lsq->RS[idx].dest_tag;
      long long seq = ROB[tag].sequence_num;
      if (seq != -1)
        traces[seq].cycle_to_stage[clock_cycle] = "EX";
    }
  }

  // 3. Advance pipelines and compute results
  for (auto &unit : units)
  {
    unit.executeCycle();
    if (unit.has_result)
    {
      CDBEntry cdb_entry;
      cdb_entry.tag = unit.result_tag;
      cdb_entry.value = unit.result_val;
      cdb_entry.exception = unit.has_exception;
      cdb_entry.valid = true;

      // Record WB stage: results computed this cycle are broadcast on the CDB
      // WB always overwrites EX if they happen in the same cycle (last cycle of execution)
      long long seq = ROB[unit.result_tag].sequence_num;
      if (seq != -1)
        traces[seq].cycle_to_stage[clock_cycle] = "WB";

      if (unit.name == UnitType::ADDER)
        CDB[0] = cdb_entry;
      else if (unit.name == UnitType::MULTIPLIER)
        CDB[1] = cdb_entry;
      else if (unit.name == UnitType::DIVIDER)
        CDB[2] = cdb_entry;
      else if (unit.name == UnitType::BRANCH)
        CDB[3] = cdb_entry;
      else if (unit.name == UnitType::LOGIC)
        CDB[4] = cdb_entry;
    }
  }

  lsq->executeCycle(Memory);
  if (lsq->has_result)
  {
    CDBEntry cdb_entry;
    cdb_entry.tag = lsq->result_tag;
    cdb_entry.value = lsq->result_val;
    cdb_entry.exception = lsq->has_exception;
    cdb_entry.valid = true;
    CDB[5] = cdb_entry;

    long long seq = ROB[lsq->result_tag].sequence_num;
    if (seq != -1)
      traces[seq].cycle_to_stage[clock_cycle] = "WB";
  }

  // 4. Snoop the CDB
  broadcastOnCDB();
}

void Processor::dumpPipelineTrace()
{
  std::ostringstream oss;
  oss << "\n=== PIPELINE TRACE ===\n";
  int max_cycle = clock_cycle;

  // Header
  oss << std::left << std::setw(17) << "Instruction" << " |";
  for (int i = 1; i <= max_cycle; i++)
  {
    oss << std::left << std::setw(4) << i;
  }
  oss << "\n-----------------+";
  for (int i = 1; i <= max_cycle; i++)
  {
    oss << "----";
  }
  oss << "\n";

  for (const auto &trace : traces)
  {
    std::string instr_str = trace.raw;
    if (trace.flushed)
    {
      instr_str += " [F]";
    }
    oss << std::left << std::setw(17) << instr_str << " |";
    for (int i = 1; i <= max_cycle; i++)
    {
      if (trace.cycle_to_stage.count(i))
      {
        oss << std::left << std::setw(4) << trace.cycle_to_stage.at(i);
      }
      else
      {
        oss << "    ";
      }
    }
    oss << "\n";
  }
  std::cout << oss.str();
}

void Processor::broadcastOnCDB()
{
  for (auto &entry : CDB)
  {
    if (!entry.valid)
    {
      continue;
    }
    int tag = entry.tag;
    int value = entry.value;
    bool exc = entry.exception;
    entry.valid = false; // Mark as consumed
    // cout << "Cycle " << clock_cycle << ": Broadcasting on CDB - Tag: " << tag << ", Value: " << value << ", Exception: " << exc << std::endl;
    for (auto &unit : units)
    {
      unit.capture(tag, value);
    }
    lsq->capture(tag, value);

    ROB[tag].ready_bit = true;
    ROB[tag].value = value;
    ROB[tag].exception = exc;
  }
}

bool Processor::step()
{
  flushed_this_cycle = false;
  clock_cycle++;
  stageCommit();
  stageExecuteAndBroadcast();
  // stageCommit();
  stageDecode();
  stageFetch();

  // Log instruction stages for debugging
  // logInstructionStagesDetailed();
  pc = my_pc / 4;
  bool more_work = (my_pc < static_cast<int>(inst_memory.size() * 4)) || (rob_cnt > 0) || fetched_instr.fetched;

  // Clear freed RS/LSQ entry flags so they can be reused in the NEXT cycle
  for (auto &unit : units)
  {
    unit.clearFreedFlags();
  }
  lsq->clearFreedFlags();

  if (exception || (!more_work))
  {
    // clock_cycle--;
    return false;
  }

  return true;
}

// bool Processor::step()
// {
//   // 1. If an exception has already halted the processor, stop immediately.
//   if (exception)
//     return false;

//   // 2. Determine if there is work to do for THIS cycle.
//   // Work exists if:
//   // - The PC is still within the program bounds (more to fetch)
//   // - OR there is an instruction currently sitting in the fetch latch (waiting for decode)
//   // - OR the ROB is not empty (instructions are still in-flight)
//   bool has_work_to_do = (pc / 4 < static_cast<int>(inst_memory.size())) ||
//                         fetched_instr.fetched ||
//                         (rob_cnt > 0);

//   if (!has_work_to_do)
//   {
//     return false; // No work to perform; do not increment cycle count.
//   }

//   // 3. Start the cycle
//   clock_cycle++;

//   // 4. Execute stages in reverse pipeline order to prevent
//   // instructions from skipping stages in a single cycle.
//   stageCommit();
//   stageExecuteAndBroadcast();
//   stageDecode();
//   stageFetch();

//   // 5. If a stage just raised an exception, we stop here.
//   if (exception)
//   {
//     return false;
//   }

//   // Return true to indicate cycle was completed and main loop should call step() again.
//   return true;
// }

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
    std::cout << "EXCEPTION raised by instruction " << exception_pc / 4 + 1 << std::endl;
  }
  std::cout << "Branch Predictor Stats: " << bp.correct_predictions << "/" << bp.total_branches << " correct.\n";
}

/*Commit stage - checks exceptions, branch mispredictions,
commit changes from oldest valid ROB entry and
make changes to ARF and RAT(if required - need to check)*/
void Processor::stageCommit()
{
  if (rob_cnt == 0)
  {
    return;
  }

  ROBEntry &entry = ROB[rob_start];
  if (!entry.ready_bit)
  {
    return;
  }

  // Check if this ROB entry has an exception
  if (entry.exception)
  {
    this->exception = true;
    this->exception_pc = entry.pc_entry;
    // Do NOT commit this entry — just halt
    return;
  }

  // Record CM stage
  traces[entry.sequence_num].cycle_to_stage[clock_cycle] = "CM";
  traces[entry.sequence_num].last_cycle = clock_cycle;

  if (entry.reg_id > 0 && entry.reg_id < static_cast<int>(ARF.size()))
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
  rob_cnt--;
  // cout << "Cycle " << clock_cycle << ": PC=" << pc * 4 << " | ROB entries=" << rob_cnt << " | FetchedInstr=" << fetched_instr.fetched << std::endl;
  // update branch predictor if this instruction was a branch
  Instruction &committed_instr = inst_memory[entry.pc_entry / 4];
  if (committed_instr.op == OpCode::BEQ || committed_instr.op == OpCode::BNE || committed_instr.op == OpCode::BLT || committed_instr.op == OpCode::BLE)
  {
    bool taken = (entry.value != 0);
    int branch_pc = entry.pc_entry;
    int actual_next_pc;
    if (taken)
    {
      actual_next_pc = branch_pc + committed_instr.imm * 4;
    }
    else
    {
      actual_next_pc = branch_pc + 4;
    }

    bool was_correct = (actual_next_pc == entry.predicted_next_pc);
    bp.update(branch_pc, actual_next_pc, taken, was_correct);

    if (!was_correct)
    {
      // Flush pipeline and redirect PC
      flush();
      my_pc = actual_next_pc;
    }
  }
}