#include "compiler.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>

using namespace std;

static inline string toLower(string s)
{
  for (char &c : s)
    c = tolower(c);
  return s;
}

RISCVCompiler::RISCVCompiler() : current_instr_idx(0), data_word_index(0)
{
  initRegAliases();
  initOpcodeToBasicOpCode();
}

void RISCVCompiler::initRegAliases()
{
  reg_alias = {
      {"zero", "x0"}, {"ra", "x1"}, {"sp", "x2"}, {"gp", "x3"}, {"tp", "x4"}, {"t0", "x5"}, {"t1", "x6"}, {"t2", "x7"}, {"s0", "x8"}, {"fp", "x8"}, {"s1", "x9"}, {"a0", "x10"}, {"a1", "x11"}, {"a2", "x12"}, {"a3", "x13"}, {"a4", "x14"}, {"a5", "x15"}, {"a6", "x16"}, {"a7", "x17"}, {"s2", "x18"}, {"s3", "x19"}, {"s4", "x20"}, {"s5", "x21"}, {"s6", "x22"}, {"s7", "x23"}, {"s8", "x24"}, {"s9", "x25"}, {"s10", "x26"}, {"s11", "x27"}, {"t3", "x28"}, {"t4", "x29"}, {"t5", "x30"}, {"t6", "x31"}};
  for (int i = 0; i < 32; ++i)
    reg_alias["x" + to_string(i)] = "x" + to_string(i);
}

string RISCVCompiler::stripComments(const string &s) const
{
  long p1 = s.find('#');
  long p2 = s.find("//");
  long p = -1;

  if (p1 != -1)
    p = p1;
  if (p2 != -1)
    p = (p == -1 ? p2 : min(p, p2));
  if (p == -1)
    return s;
  return s.substr(0, p);
}

string RISCVCompiler::trim(const string &s)
{
  size_t a = 0;
  while (a < s.size() && isspace(s[a]))
    ++a;
  if (a == s.size())
    return "";
  size_t b = s.size() - 1;
  while (b > a && isspace(s[b]))
    --b;
  return s.substr(a, b - a + 1);
}

vector<string> RISCVCompiler::tokenize(const string &s)
{
  string t;

  for (char c : s)
    t.push_back(c == ',' ? ' ' : c);

  vector<string> res;
  istringstream iss(t);
  string tok;
  while (iss >> tok)
    res.push_back(tok);
  return res;
}

bool RISCVCompiler::isNumber(const string &s)
{
  if (s.empty())
    return false;

  if (s.size() > 1 && (s[0] == '+' || s[0] == '-'))
  {
    for (size_t i = 1; i < s.size(); ++i)
      if (!isdigit(s[i]))
        return false;
    return true;
  }

  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
  {
    for (size_t i = 2; i < s.size(); ++i)
      if (!isxdigit(s[i]))
        return false;
    return true;
  }

  for (size_t i = 0; i < s.size(); ++i)
    if (!isdigit(s[i]))
      return false;
  return true;
}

int RISCVCompiler::parseNumber(const string &s)
{
  if (s.empty())
    throw runtime_error("empty number");
  try
  {
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
      return stoi(s, nullptr, 16);
    return stoi(s, nullptr, 10);
  }
  catch (...)
  {
    throw runtime_error("invalid number: " + s);
  }
}

void RISCVCompiler::scanDataLabel(const string &line)
{
  string s = trim(stripComments(line));
  if (s.empty())
    return;

  int colon = s.find(':');
  if (colon == -1)
    return;

  string label = s.substr(1, colon - 1);
  label = trim(label);
  string rest = trim(s.substr(colon + 1));

  if (label.empty())
    return;

  vector<string> toks = tokenize(rest);
  if (toks.empty())
  {
    data_labels[label] = data_word_index;
    data_symbols.push_back({label, data_word_index, 0});
    return;
  }

  int count = 0;
  for (auto &tok : toks)
  {
    if (isNumber(tok))
    {
      int v = parseNumber(tok);
      data_words.push_back(v);
      ++count;
    }
  }

  data_labels[label] = data_word_index;
  data_symbols.push_back({label, data_word_index, count});
  data_word_index += count;
}

void RISCVCompiler::scanCodeLabel(const string &line)
{
  string s = trim(stripComments(line));
  if (s.empty())
    return;

  int colon = s.find(':');
  if (colon == -1)
    return;

  string label = trim(s.substr(0, colon));
  code_labels[label] = current_instr_idx;
  string rest = trim(s.substr(colon + 1));
  if (!rest.empty())
    scanInstruction(rest);
}

void RISCVCompiler::scanInstruction(const string &line)
{
  string s = trim(stripComments(line));
  vector<string> toks = tokenize(s);
  if (toks.empty())
    return;

  CompilerInstruction inst;
  inst.opcode = toks[0];
  for (size_t i = 1; i < toks.size(); ++i)
  {
    inst.operands.push_back(toks[i]);
  }
  inst.instr_index = current_instr_idx++;
  inst.raw = s;
  instructions.push_back(inst);

  int idx = static_cast<int>(instructions.size()) - 1;
  string op_lower = toLower(inst.opcode);

  if (!inst.operands.empty())
  {
    string target = inst.operands.back();

    if (!isNumber(target))
    {
      bool is_branch = (op_lower == "beq" || op_lower == "bne" || op_lower == "blt" || op_lower == "ble");
      bool is_jump = (op_lower == "j");
      int last_op_index = static_cast<int>(inst.operands.size()) - 1;

      if (is_branch)
        relocations.push_back({Relocation::BRANCH, idx, last_op_index, target});
      else if (is_jump)
        relocations.push_back({Relocation::JUMP, idx, last_op_index, target});
    }
  }

  for (size_t i = 0; i < inst.operands.size(); ++i)
  {
    const string &op = inst.operands[i];
    int open_paren = op.find('(');
    int close_paren = op.find(')');

    if (open_paren != -1 && close_paren != -1 && close_paren > open_paren)
    {
      string label = trim(op.substr(0, open_paren));
      if (!label.empty() && !isNumber(label))
        relocations.push_back({Relocation::MEMORY, idx, static_cast<int>(i), label});
    }
  }
}

void RISCVCompiler::pre_process(const string &code)
{
  current_instr_idx = 0;
  data_word_index = 0;
  data_words.clear();
  data_symbols.clear();
  instructions.clear();
  code_labels.clear();
  data_labels.clear();
  relocations.clear();

  istringstream ss(code);
  string line;
  while (getline(ss, line))
  {
    string t = trim(stripComments(line));
    if (t.empty())
      continue;

    if (!t.empty() && t[0] == '.')
      scanDataLabel(t);
    else
    {
      vector<string> tokens = tokenize(t);
      if (tokens.empty())
        continue;

      if (tokens[0].back() == ':')
        scanCodeLabel(t);
      else
        scanInstruction(t);
    }
  }
}

void RISCVCompiler::link()
{
  for (const Relocation &r : relocations)
  {
    if (r.instr_index < 0 || r.instr_index >= static_cast<int>(instructions.size()))
      continue;

    CompilerInstruction &inst = instructions[r.instr_index];

    if (r.kind == Relocation::BRANCH || r.kind == Relocation::JUMP)
    {
      auto it = code_labels.find(r.label);
      if (it == code_labels.end())
        throw runtime_error("Undefined code label: " + r.label);

      int target_pc = it->second;
      int offset = target_pc - r.instr_index;
      inst.operands[r.operand_index] = to_string(offset);
    }
    else if (r.kind == Relocation::MEMORY)
    {
      auto it = data_labels.find(r.label);
      if (it == data_labels.end())
        throw runtime_error("Undefined data label: " + r.label);

      int base_address = it->second;
      string orig_op = inst.operands[r.operand_index];

      int open_paren = orig_op.find('(');
      int close_paren = orig_op.find(')');

      string inner_reg = "";
      if (open_paren != -1 && close_paren != -1 && close_paren > open_paren)
      {
        inner_reg = orig_op.substr(open_paren + 1, close_paren - open_paren - 1);
      }

      string resolved_op = to_string(base_address);
      if (!inner_reg.empty())
      {
        resolved_op += "(" + inner_reg + ")";
      }

      inst.operands[r.operand_index] = resolved_op;
    }
  }

  for (CompilerInstruction &inst : instructions)
  {
    for (string &op : inst.operands)
    {
      int open_paren = op.find('(');
      int close_paren = op.find(')');

      if (open_paren != -1 && close_paren != -1 && close_paren > open_paren)
      {
        string inner_reg = op.substr(open_paren + 1, close_paren - open_paren - 1);
        string norm_reg = normalizeRegister(inner_reg);
        op = op.substr(0, open_paren + 1) + norm_reg + op.substr(close_paren);
      }
      else
      {
        string norm_reg = normalizeRegister(op);
        if (!norm_reg.empty())
          op = norm_reg;
      }
    }
  }
}

string RISCVCompiler::normalizeRegister(const string &r) const
{
  auto it = reg_alias.find(r);
  if (it != reg_alias.end())
    return it->second;
  string lower = toLower(r);

  it = reg_alias.find(lower);
  if (it != reg_alias.end())
    return it->second;
  return r;
}

std::vector<Instruction> RISCVCompiler::getInstructions()
{
  vector<Instruction> result;
  for (const CompilerInstruction &inst : instructions)
  {
    Instruction i;
    i.op = opcode_to_BasicOpCode[toLower(inst.opcode)];
    i.pc = inst.instr_index;
    i.dest = -1;
    i.src1 = -1;
    i.src2 = -1;
    i.imm = 0;

    if (i.op == OpCode::J)
    {
      i.imm = stoi(inst.operands[0]);
    }
    else if (i.op == OpCode::LW || i.op == OpCode::SW)
    {
      if (i.op == OpCode::LW)
        i.dest = reg(inst.operands[0]);
      else
        i.src2 = reg(inst.operands[0]);

      int op = inst.operands[1].find('(');
      int cl = inst.operands[1].find(')');
      i.imm = stoi(inst.operands[1].substr(0, op));
      i.src1 = reg(inst.operands[1].substr(op + 1, cl - op - 1));
    }
    else if (i.op == OpCode::BEQ || i.op == OpCode::BNE || i.op == OpCode::BLT || i.op == OpCode::BLE)
    {
      i.src1 = reg(inst.operands[0]);
      i.src2 = reg(inst.operands[1]);
      i.imm = stoi(inst.operands[2]);
    }
    else if (i.op == OpCode::ADDI || i.op == OpCode::SLTI || i.op == OpCode::ANDI || i.op == OpCode::ORI || i.op == OpCode::XORI)
    {
      i.dest = reg(inst.operands[0]);
      i.src1 = reg(inst.operands[1]);
      i.imm = stoi(inst.operands[2]);
    }
    else
    {
      i.dest = reg(inst.operands[0]);
      i.src1 = reg(inst.operands[1]);
      i.src2 = reg(inst.operands[2]);
    }

    result.push_back(i);
  }

  return result;
}

void RISCVCompiler::initOpcodeToBasicOpCode()
{
  opcode_to_BasicOpCode = {
      {"add", OpCode::ADD},
      {"sub", OpCode::SUB},
      {"addi", OpCode::ADDI},
      {"mul", OpCode::MUL},
      {"div", OpCode::DIV},
      {"rem", OpCode::REM},
      {"lw", OpCode::LW},
      {"sw", OpCode::SW},
      {"beq", OpCode::BEQ},
      {"bne", OpCode::BNE},
      {"blt", OpCode::BLT},
      {"ble", OpCode::BLE},
      {"j", OpCode::J},
      {"slt", OpCode::SLT},
      {"slti", OpCode::SLTI},
      {"and", OpCode::AND},
      {"or", OpCode::OR},
      {"xor", OpCode::XOR},
      {"andi", OpCode::ANDI},
      {"ori", OpCode::ORI},
      {"xori", OpCode::XORI},
  };
}

void RISCVCompiler::compile(const string &riscv_code)
{
  pre_process(riscv_code);
  link();
}
