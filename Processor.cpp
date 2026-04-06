#include "Processor.h"

Processor::Processor(ProcessorConfig &config)
{
  pc = 0;
  clock_cycle = 0;
  ARF.resize(config.num_regs, 0);
  Memory.resize(config.mem_size);
  ROB.resize(config.rob_size);
  RAT.resize(config.num_regs);

  // Instantiate Hardware Units
  // Adder
  // Multiplier
  // Divider
  // Branch Computation
  // Bitwise Logic
  // Load-Store Unit
  ExecutionUnit Adder(UnitType::ADDER, config.add_lat);
  ExecutionUnit Multiplier(UnitType::MULTIPLIER, config.mul_lat);
  ExecutionUnit Divider(UnitType::DIVIDER, config.div_lat);
  ExecutionUnit BranchCmpr(UnitType::BRANCH, config.add_lat);
  // mentioned to take Branch Comparison latency equal to addition latency
  ExecutionUnit BitLogic(UnitType::LOGIC, config.logic_lat);

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

  ROBEntry rob_entry;
  RATEntry rat_entry;
  RSEntry rs_entry;
  // Suppress unused variable warnings - placeholders for future implementation
  (void)rob_entry;
  (void)rat_entry;
  (void)rs_entry;
}

void Processor::flush()
{
}

bool Processor::step()
{
  clock_cycle++;
  stageFetch();
  stageDecode();
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