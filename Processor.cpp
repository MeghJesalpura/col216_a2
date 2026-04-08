#include "Processor.h"

using std::cout;

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
  auto temp = compiler.getDataWords();
  for (size_t i = 0; i < temp.size() && i < Memory.size(); i++)
  {
    Memory[i] = temp[i];
  }
}

void Processor::stageFetch()
{
  // Don't fetch if previous instruction hasn't been consumed by decode
  if (fetched_instr.fetched)
    return;

  // Don't fetch after exception
  if (exception)
    return;

  if (pc / 4 >= static_cast<int>(inst_memory.size()))
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
    units[unit_idx].createRSEntry(fetched_instr, rob_index, RAT, ARF, ROB);
  }

  int dest = fetched_instr.dest;
  if (fetched_instr.op == OpCode::SW || fetched_instr.op == OpCode::BEQ ||
      fetched_instr.op == OpCode::BNE || fetched_instr.op == OpCode::BLT ||
      fetched_instr.op == OpCode::BLE || fetched_instr.op == OpCode::J)
  {
    dest = -1;
  }

  ROBEntry rob_entry;
  if (is_j)
  {
    // Jump finishes immediately upon decode
    rob_entry = ROBEntry(true, true, dest, fetched_instr.pc);
  }
  else
  {
    rob_entry = ROBEntry(true, false, dest, fetched_instr.pc);
  }

  // For branches, store the predicted next PC so we can detect mispredictions at commit
  bool is_branch_instr = (fetched_instr.op == OpCode::BEQ || fetched_instr.op == OpCode::BNE ||
                          fetched_instr.op == OpCode::BLT || fetched_instr.op == OpCode::BLE);
  if (is_branch_instr)
  {
    rob_entry.predicted_next_pc = pc; // pc was already set by fetch's branch prediction
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
    CDBEntry cdb_entry;
    cdb_entry.tag = lsq->result_tag;
    cdb_entry.value = lsq->result_val;
    cdb_entry.exception = lsq->has_exception;
    cdb_entry.valid = true;
    CDB[5] = cdb_entry;
  }

  broadcastOnCDB();

  // Dispatch newly-ready instructions AFTER broadcast so captured values are available
  for (auto &unit : units)
  {
    unit.dispatchReady();
  }
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
  if (exception)
    return false;

  clock_cycle++;
  bool exception_before = exception;

  stageCommit();
  stageExecuteAndBroadcast();
  stageDecode();
  stageFetch();

  // Log instruction stages for debugging
  logInstructionStages();

  bool more_work = (pc < static_cast<int>(inst_memory.size() * 4)) || (rob_cnt > 0) || fetched_instr.fetched;

  if ((exception && !exception_before) || (!more_work))
  {
    clock_cycle--;
  }

  return more_work && !exception;
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
    std::cout << "EXCEPTION raised by instruction " << exception_pc / 4 + 1 << std::endl;
  }
  std::cout << "Branch Predictor Stats: " << bp.correct_predictions << "/" << bp.total_branches << " correct.\n";
}

/*Commit stage - checks exceptions, branch mispredictions,
commit changes from oldest valid ROB entry and
make changes to ARF and RAT(if required - need to check)*/
void Processor::stageCommit()
{
  std::cout << "[DEBUG][Commit] rob_start=" << rob_start << " rob_end=" << rob_end << '\n';
  if (rob_cnt == 0)
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

  // Check if this ROB entry has an exception
  if (entry.exception)
  {
    std::cout << "[DEBUG][Commit] exception detected at tag=" << rob_start << '\n';
    this->exception = true;
    this->exception_pc = entry.pc_entry;
    // Do NOT commit this entry — just halt
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
  rob_cnt--;

  std::cout << "[DEBUG][Commit] rob_start advanced to " << rob_start << '\n';

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
      std::cout << "[DEBUG][Commit] branch misprediction at PC=" << branch_pc
                << " predicted=" << entry.predicted_next_pc
                << " actual=" << actual_next_pc << '\n';
      // Flush pipeline and redirect PC
      flush();
      pc = actual_next_pc;
    }
  }
}

std::string Processor::opcodeToString(OpCode op)
{
  switch (op)
  {
  case OpCode::ADD:
    return "ADD";
  case OpCode::SUB:
    return "SUB";
  case OpCode::ADDI:
    return "ADDI";
  case OpCode::MUL:
    return "MUL";
  case OpCode::DIV:
    return "DIV";
  case OpCode::REM:
    return "REM";
  case OpCode::LW:
    return "LW";
  case OpCode::SW:
    return "SW";
  case OpCode::BEQ:
    return "BEQ";
  case OpCode::BNE:
    return "BNE";
  case OpCode::BLT:
    return "BLT";
  case OpCode::BLE:
    return "BLE";
  case OpCode::J:
    return "J";
  case OpCode::SLT:
    return "SLT";
  case OpCode::SLTI:
    return "SLTI";
  case OpCode::AND:
    return "AND";
  case OpCode::OR:
    return "OR";
  case OpCode::XOR:
    return "XOR";
  case OpCode::ANDI:
    return "ANDI";
  case OpCode::ORI:
    return "ORI";
  case OpCode::XORI:
    return "XORI";
  default:
    return "UNKNOWN";
  }
}

std::string Processor::unitTypeToString(UnitType unit)
{
  switch (unit)
  {
  case UnitType::ADDER:
    return "ADDER";
  case UnitType::MULTIPLIER:
    return "MULTIPLIER";
  case UnitType::DIVIDER:
    return "DIVIDER";
  case UnitType::LOADSTORE:
    return "LSQ";
  case UnitType::BRANCH:
    return "BRANCH";
  case UnitType::LOGIC:
    return "LOGIC";
  default:
    return "UNKNOWN";
  }
}

void Processor::logInstructionStages()
{
  std::cout << "\n========== CYCLE " << clock_cycle << " ==========\n";

  // FETCH stage
  std::cout << "[FETCH]  ";
  if (fetched_instr.fetched)
  {
    std::cout << "PC=" << std::setw(3) << fetched_instr.pc << " | " << opcodeToString(fetched_instr.op) << "\n";
  }
  else
  {
    std::cout << "(empty)\n";
  }

  // DECODE, EXECUTE, and COMMIT stages (track through ROB)
  std::cout << "[DECODE] ";
  bool found_decode = false;

  // Find instructions in DECODE stage (recently added to ROB but not yet dispatched)
  for (int i = 0; i < rob_cnt; i++)
  {
    int rob_idx = (rob_start + i) % rob_capacity;
    if (ROB[rob_idx].valid_bit)
    {
      // Check if instruction is in execution phase
      bool in_execute = false;

      // Check in all execution units
      for (const auto &unit : units)
      {
        for (const auto &rs_entry : unit.RS)
        {
          if (rs_entry.isValid && (rs_entry.dest_tag == rob_idx ||
                                   (rs_entry.dispatched && !ROB[rob_idx].ready_bit)))
          {
            in_execute = true;
            break;
          }
        }
        if (in_execute)
          break;
      }

      // Check in LSQ
      if (!in_execute)
      {
        for (const auto &lsq_entry : lsq->q)
        {
          if (lsq_entry.dest_tag == rob_idx)
          {
            in_execute = true;
            break;
          }
        }
      }

      // If in execute, skip (will be shown in EXECUTE section)
      // Otherwise it's in DECODE
      if (!in_execute && !ROB[rob_idx].ready_bit)
      {
        std::cout << "PC=" << std::setw(3) << ROB[rob_idx].pc_entry << " | ROB[" << rob_idx << "] | ";
        // Try to find the instruction in memory to get opcode
        int inst_idx = ROB[rob_idx].pc_entry / 4;
        if (inst_idx >= 0 && inst_idx < static_cast<int>(inst_memory.size()))
        {
          std::cout << opcodeToString(inst_memory[inst_idx].op) << " ";
        }
        found_decode = true;
        break; // Show one per stage for clarity
      }
    }
  }
  if (!found_decode)
    std::cout << "(empty)";
  std::cout << "\n";

  // EXECUTE stage (instructions in RS or LSQ)
  std::cout << "[EXECUTE] ";
  bool found_execute = false;

  for (const auto &unit : units)
  {
    for (const auto &rs_entry : unit.RS)
    {
      if (rs_entry.isValid && rs_entry.dispatched)
      {
        int rob_idx = rs_entry.dest_tag;
        std::cout << "PC=" << std::setw(3) << ROB[rob_idx].pc_entry << " | ROB[" << rob_idx << "] | "
                  << unitTypeToString(unit.name) << " | " << opcodeToString(rs_entry.opcode);
        found_execute = true;
        std::cout << " | ";
      }
    }
  }

  // LSQ entries
  for (const auto &lsq_entry : lsq->q)
  {
    int rob_idx = lsq_entry.dest_tag;
    if (rob_idx >= 0 && rob_idx < rob_capacity && ROB[rob_idx].valid_bit)
    {
      std::cout << "PC=" << std::setw(3) << ROB[rob_idx].pc_entry << " | ROB[" << rob_idx << "] | LSQ";
      if (lsq_entry.type == LSQEntryType::LOAD)
        std::cout << " [LOAD]";
      else
        std::cout << " [STORE]";
      found_execute = true;
      std::cout << " | ";
    }
  }

  if (!found_execute)
    std::cout << "(empty)";
  std::cout << "\n";

  // COMMIT stage (ready instructions in ROB)
  std::cout << "[COMMIT] ";
  bool found_commit = false;

  for (int i = 0; i < rob_cnt; i++)
  {
    int rob_idx = (rob_start + i) % rob_capacity;
    if (ROB[rob_idx].valid_bit && ROB[rob_idx].ready_bit)
    {
      std::cout << "PC=" << std::setw(3) << ROB[rob_idx].pc_entry << " | ROB[" << rob_idx << "] | ";
      int inst_idx = ROB[rob_idx].pc_entry / 4;
      if (inst_idx >= 0 && inst_idx < static_cast<int>(inst_memory.size()))
      {
        std::cout << opcodeToString(inst_memory[inst_idx].op) << " ";
      }
      found_commit = true;
      break;
    }
  }
  if (!found_commit)
    std::cout << "(empty)";
  std::cout << "\n";

  std::cout << "===============================\n";
}

void Processor::logInstructionStagesDetailed()
{
  std::cout << "\n========== DETAILED CYCLE " << clock_cycle << " ==========\n";

  // FETCH stage
  std::cout << "[FETCH STAGE]\n";
  if (fetched_instr.fetched)
  {
    std::cout << "  PC=" << std::setw(4) << fetched_instr.pc << " | " << opcodeToString(fetched_instr.op)
              << " | rd=" << fetched_instr.dest << " rs1=" << fetched_instr.src1
              << " rs2=" << fetched_instr.src2 << "\n";
  }
  else
  {
    std::cout << "  (empty)\n";
  }

  // DECODE stage - instructions in ROB not yet dispatched
  std::cout << "[DECODE STAGE]\n";
  bool found_decode = false;
  for (int i = 0; i < rob_cnt; i++)
  {
    int rob_idx = (rob_start + i) % rob_capacity;
    if (ROB[rob_idx].valid_bit)
    {
      // Check if in execute
      bool in_execute = false;
      for (const auto &unit : units)
      {
        for (const auto &rs_entry : unit.RS)
        {
          if (rs_entry.isValid && rs_entry.dest_tag == rob_idx && rs_entry.dispatched)
          {
            in_execute = true;
            break;
          }
        }
        if (in_execute)
          break;
      }

      if (!in_execute)
      {
        for (const auto &lsq_entry : lsq->q)
        {
          if (lsq_entry.dest_tag == rob_idx)
          {
            in_execute = true;
            break;
          }
        }
      }

      // In DECODE if not in EXECUTE and not ready
      if (!in_execute && !ROB[rob_idx].ready_bit)
      {
        int inst_idx = ROB[rob_idx].pc_entry / 4;
        if (inst_idx >= 0 && inst_idx < static_cast<int>(inst_memory.size()))
        {
          std::cout << "  PC=" << std::setw(4) << ROB[rob_idx].pc_entry << " | ROB[" << std::setw(2) << rob_idx << "] | "
                    << opcodeToString(inst_memory[inst_idx].op) << "\n";
          found_decode = true;
        }
      }
    }
  }
  if (!found_decode)
    std::cout << "  (empty)\n";

  // EXECUTE stage
  std::cout << "[EXECUTE STAGE]\n";
  bool found_execute = false;

  // Check all execution units
  for (const auto &unit : units)
  {
    for (const auto &rs_entry : unit.RS)
    {
      if (rs_entry.isValid && rs_entry.dispatched)
      {
        int rob_idx = rs_entry.dest_tag;
        if (rob_idx >= 0 && rob_idx < rob_capacity && ROB[rob_idx].valid_bit)
        {
          int inst_idx = ROB[rob_idx].pc_entry / 4;
          if (inst_idx >= 0 && inst_idx < static_cast<int>(inst_memory.size()))
          {
            std::cout << "  PC=" << std::setw(4) << ROB[rob_idx].pc_entry << " | ROB[" << std::setw(2) << rob_idx
                      << "] | " << unitTypeToString(unit.name) << " | " << opcodeToString(rs_entry.opcode) << "\n";
            found_execute = true;
          }
        }
      }
    }
  }

  // LSQ entries
  for (const auto &lsq_entry : lsq->q)
  {
    int rob_idx = lsq_entry.dest_tag;
    if (rob_idx >= 0 && rob_idx < rob_capacity && ROB[rob_idx].valid_bit)
    {
      int inst_idx = ROB[rob_idx].pc_entry / 4;
      if (inst_idx >= 0 && inst_idx < static_cast<int>(inst_memory.size()))
      {
        std::cout << "  PC=" << std::setw(4) << ROB[rob_idx].pc_entry << " | ROB[" << std::setw(2) << rob_idx
                  << "] | LSQ [" << (lsq_entry.type == LSQEntryType::LOAD ? "LOAD" : "STORE")
                  << "] | " << opcodeToString(inst_memory[inst_idx].op) << "\n";
        found_execute = true;
      }
    }
  }

  if (!found_execute)
    std::cout << "  (empty)\n";

  // COMMIT stage
  std::cout << "[COMMIT STAGE] Ready to commit:\n";
  bool found_commit = false;
  for (int i = 0; i < rob_cnt; i++)
  {
    int rob_idx = (rob_start + i) % rob_capacity;
    if (ROB[rob_idx].valid_bit && ROB[rob_idx].ready_bit)
    {
      int inst_idx = ROB[rob_idx].pc_entry / 4;
      if (inst_idx >= 0 && inst_idx < static_cast<int>(inst_memory.size()))
      {
        std::cout << "  PC=" << std::setw(4) << ROB[rob_idx].pc_entry << " | ROB[" << std::setw(2) << rob_idx
                  << "] | " << opcodeToString(inst_memory[inst_idx].op) << " | Value=" << ROB[rob_idx].value << "\n";
        found_commit = true;
      }
    }
  }
  if (!found_commit)
    std::cout << "  (empty)\n";

  std::cout << "==================================\n";
}