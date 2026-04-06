#ifndef COMPILER_COMPILER_H
#define COMPILER_COMPILER_H

#include <string>
#include <vector>
#include <unordered_map>

#include "../Basics.h"

struct CompilerInstruction
{
  std::string opcode;
  std::vector<std::string> operands;
  int instr_index = -1;
  std::string raw;
};

struct DataSymbol
{
  std::string name;
  int base_index = 0;
  int size = 0;
};

struct Relocation
{
  enum Kind
  {
    BRANCH,
    JUMP,
    MEMORY
  } kind;

  int instr_index = -1;
  int operand_index = -1;
  std::string label;
};

class RISCVCompiler
{
public:
  RISCVCompiler();
  void compile(const std::string &riscv_code, const std::string &out_filename);
  std::vector<Instruction> getInstructions();

private:
  std::vector<int> data_words;
  std::vector<DataSymbol> data_symbols;
  std::vector<CompilerInstruction> instructions;
  std::unordered_map<std::string, int> code_labels;
  std::unordered_map<std::string, int> data_labels;
  std::vector<Relocation> relocations;
  std::unordered_map<std::string, OpCode> opcode_to_BasicOpCode;

  int reg(const std::string &s)
  {
    return stoi(s.substr(1));
  };

  std::unordered_map<std::string, std::string> reg_alias;
  int current_instr_idx;
  int data_word_index;

  void initRegAliases();
  void initOpcodeToBasicOpCode();
  std::string stripComments(const std::string &s) const;
  static std::string trim(const std::string &s);
  static std::vector<std::string> tokenize(const std::string &s);
  void pre_process(const std::string &code);
  void link();
  void scanDataLabel(const std::string &line);
  void scanCodeLabel(const std::string &line);
  void scanInstruction(const std::string &line);
  void writeOutput(const std::string &out_filename) const;
  static bool isNumber(const std::string &s);
  static int parseNumber(const std::string &s);
  std::string normalizeRegister(const std::string &r) const;
};

#endif
