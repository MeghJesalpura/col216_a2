# Build & Run Instructions

This file explains how to build the simulator and how to preprocess / run RISC‑V input files.

## Build the simulator
Compile the simulator executable `main`:
- `make compile FILE=main.cpp`

## Preprocess / assemble RISC‑V input (optional)
Build and run the preprocessing tool `compiler`:
- `make run FILE=programs/code3.txt`

# Compiler
## What the Compiler Does
The compiler acts as a **Two-Pass Assembler** that translates high-level RISC‑V assembly into a raw, simulator-ready format (`.pre` file).

- **Pass 1: Pre-processing & Symbol Collection**
  - **Comment & Whitespace Management**: Removes all comments (`#` or `//`) and trims leading/trailing whitespace.
  - **Label Discovery**: Scans for code labels (e.g., `LOOP:`) and data labels (e.g., `.A:`) to map them to memory offsets (`current_instr_idx` or `data_word_index`).
  - **Relocation Logging**: Any instruction that refers to a label (like `j LOOP` or `lw x5, A(x4)`) is marked for patching in the next pass.
  - **Instruction Tokenization**: Breaks each line into an opcode and a clean list of operands (stored with an `instr_index`), removing commas for easier simulator parsing.

- **Pass 2: Linking & Normalization**
  - **Label Resolution (Linking)**: Replaces textual labels with numeric values. 
    - Jump and Branch targets are converted to **PC-relative offsets** (e.g., `j -4`).
    - Data labels are replaced with their **base memory addresses** (e.g., `A(x4)` → `0(x4)`).
  - **Register Normalization**: Converts all human-readable register aliases (like `a0`, `ra`, `zero`) into their standard `xN` hardware counterparts (like `x10`, `x1`, `x0`).
  - **Output Generation**: Writes a clean file where each line is an instruction or a data definition, stripped of all metadata, ready for the `Processor` to load directly into memory.

## Pre Processing
- **Comment Stripping**: Removes everything after `#` or `//` on each line.
- **Whitespace Handling**: Trims leading and trailing spaces for clean tokenization.
- **Register Aliasing**: Automatically maps friendly names (e.g., `sp`, `a0`, `ra`) to hardware register names (`x2`, `x10`, `x1`).
- **Tokenization**: Standardizes instruction format by removing commas and splitting by spaces, producing clean opcode/operand pairs.

## Parsing
The parsing logic is implemented across several specialized methods:
- `stripComments()` & `trim()`: Sanitizes raw strings into processable lines.
- `tokenize()`: Splits strings into discrete components, treating commas as separators.
- `scanDataLabel()`: Processes `.A: 1 2 3` style lines, storing values in `data_words` and tracking base indices.
- `scanCodeLabel()`: Detects `LABEL:` markers and records their instruction index for branch resolution.
- `scanInstruction()`: Extracts opcodes and operands, and identifies instructions requiring relative or absolute relocation (Branches, Jumps, Memory).
- `isNumber()` & `parseNumber()`: Robustly handles decimal and hexadecimal constants.

## Usage in Processor
To use the compiled `.pre` output files in the processor simulator:

1. **Compile the Assembly**: Run the compiler on your `.txt` source file.
   - `make run FILE=programs/code3.txt`
   - This creates `programs/code3.txt.pre`.

2. **Run the Simulator**: Provide the generated `.pre` file as the input to the main processor.
   - `./main programs/code3.txt.pre`

The processor's `loadProgram` method reads the `.pre` file line by line to populate its instruction and data memory.

# Some explainations: 
In compiler.h, there are three primary structures used to bridge the gap between Pre Processing and Linking. Here is an explanation of each:

### 1. `struct Instruction`
This is the core container for every assembly line that isn't a data definition.
*   **`opcode`**: Stores the operation (e.g., `addi`, `beq`).
*   **`operands`**: A list of strings for registers, immediates, or labels (e.g., `{"x5", "x6", "LOOP"}`).
*   **`instr_index`**: The zero-based position of this instruction in the code. This is crucial for calculating branch offsets later.
*   **`raw`**: The original string from the file (useful for debugging/logging).

### 2. `struct DataSymbol`
This records metadata for the data section (lines starting with `.`).
*   **`name`**: The name of the data array or variable (e.g., `A` from `.A:`).
*   **`base_index`**: The starting index in the global `data_words` vector. This represents the "memory address" of the start of the array.
*   **`size`**: How many integers are contained in this specific data entry.

### 3. `struct Reloc` (Relocation)
This is arguably the most important struct for a two-pass assembler. It marks an "unresolved" value that needs to be fixed later.
*   **`kind`**: An enum (`BRANCH`, `JUMP`, or `MEMORY`) that tells the linker *how* to calculate the fix.
*   **`instr_index`**: Which instruction in the `instructions` vector needs patching.
*   **`operand_index`**: Which specific operand inside that instruction is the label (e.g., the 3rd operand in `beq x1, x2, LOOP`).
*   **`label`**: The name of the label we need to look up (e.g., `"LOOP"` or `"A"`).

---

### How they work together:
1.  **Pass 1**: The compiler fills a `vector<Instruction>` and a `vector<DataSymbol>`. If it sees a label it doesn't know yet, it pushes a `Reloc` entry into a "to-do list."
2.  **Pass 2**: The compiler iterates through the `Reloc` list. It uses the `label` to find the correct address from the label maps, and updates the `Instruction` at `instr_index` at the correct `operand_index` with a numeric value.

-------------------------------------------------------------------------------------------------

When the processor executes the instruction `blt x10 x1 15` in your simulator, it performs a **Branch if Less Than** operation. Here is the step-by-step breakdown:

### 1. Comparison Phase
The processor reads the values currently stored in the physical registers `x10` and `x1`:
- If the value in **`x10` < `x1`**, the condition is **true**.
- Otherwise, the condition is **false**.

### 2. The Offset (15)
The value `15` is a **PC-relative offset** that was calculated by your compiler during Pass 2. 
- In code3.txt.pre, this instruction sits at **instruction index 2** (since the `.A:` line at index 0 is data, and the first `addi` is at index 0, the second `addi` at index 1, and the `blt` at index 2).
- The offset `15` tells the processor how many instructions forward to jump if the condition is met.

### 3. Execution Outcome
Depending on the result of the comparison:

*   **If condition is TRUE (`x10 < x1`):**
    The processor updates its hardware Program Counter (`pc`) to:
    $$New\ PC = Current\ PC + 15$$
    Looking at the file indexing, it will skip the next 14 instructions and land on the instruction located 15 lines below it (likely an exit or a skip-ahead point).

*   **If condition is FALSE (`x10 >= x1`):**
    The branch is "not taken." The processor simply increments the `pc` by 1:
    $$New\ PC = Current\ PC + 1$$
    It will proceed to execute the very next line: `add x2 x0 x0`.

### In the Context of your Out-of-Order Simulator:
Because your simulator uses a **Branch Predictor** and **ROB (Reorder Buffer)**:
1.  **Fetch:** The `BranchPredictor` will guess if this branch is taken.
2.  **Execute:** The `ExecutionUnit` (Branch Unit) will eventually calculate `x10 < x1` to verify the guess.
3.  **Commit:** If the branch was mispredicted, the processor will **flush** the pipeline and restart fetching from the correct PC (`Current PC + 15` or `Current PC + 1`).