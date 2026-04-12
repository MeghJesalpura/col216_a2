#pragma once
#include "Basics.h"
#include <vector>
#include <unordered_map>

// A single entry in the Load/Store Queue.
// Mirrors RSEntry but carries the extra fields needed for memory ops.
struct LSQEntry
{
  bool isValid = false;
  bool dispatched = false; // In the pipeline already

  OpCode opcode;
  int dest_tag = -1; // ROB tag for result broadcast

  // Operand 1: base register value / tag
  int val1 = 0;
  int tag1 = -1;
  bool ready1 = false;

  // Operand 2: immediate (offset). Always ready at issue time.
  int imm = 0; // byte offset

  // For SW: the register whose value is being stored
  int val_store = 0;
  int tag_store = -1;
  bool ready_store = false;

  unsigned long long seq_num = 0; // program order

  LSQEntry() = default;
  LSQEntry(OpCode op, int dest, int v1, int t1, bool r1,
           int immediate,
           int vs, int ts, bool rs,
           bool valid)
      : isValid(valid), opcode(op), dest_tag(dest),
        val1(v1), tag1(t1), ready1(r1),
        imm(immediate),
        val_store(vs), tag_store(ts), ready_store(rs)
  {
  }
};

class LoadStoreQueue
{
public:
  // Public state read by the processor after executeCycle()
  bool has_result = false;
  bool has_exception = false;
  int result_tag = 0;
  int result_val = 0;

  // For SW: processor needs to know the computed address + value to write to memory at commit
  // These are filled when the SW reaches the front of the pipeline
  bool is_store_result = false;
  int store_address = 0;
  int store_value = 0;

  LoadStoreQueue() = default;
  LoadStoreQueue(int lat, int capacity);

  // --- Interface matching ExecutionUnit ---

  bool has_space();

  // Issue a new LW/SW instruction into the LSQ.
  // Named createLSQEntry to match the Processor's call-site; also aliased as createRSEntry.
  void createLSQEntry(Instruction &instr, int rob_index,
                      const std::vector<RATEntry> &RAT,
                      const std::vector<int> &ARF,
                      const std::vector<ROBEntry> &ROB);

  // Alias so ExecutionUnit-style call-sites also work
  inline void createRSEntry(Instruction &instr, int rob_index,
                            const std::vector<RATEntry> &RAT,
                            const std::vector<int> &ARF,
                            const std::vector<ROBEntry> &ROB)
  {
    createLSQEntry(instr, rob_index, RAT, ARF, ROB);
  }

  // CDB snoop: update waiting operands
  void capture(int tag, int val);

  // Stage 1 of each cycle: advance pipeline shift-register, dispatch oldest
  // in-order ready entry (if the front of the LSQ is ready).
  // Must be called BEFORE executeCycle() each cycle.
  void dispatchReady();

  // Stage 2 of each cycle: compute result for instruction that exits pipeline.
  // memory: the architectural memory array (read by LW; SW writes happen at commit).
  void executeCycle(const std::vector<int> &memory);

  // Called by stageCommit() for the ROB entry at rob_start.
  // For SW entries: performs the actual memory write.
  // For LW entries: no-op (memory was already read during execute).
  // Safe to call for non-LSQ ROB entries — it checks internally.
  void commitEntry(int rob_tag, std::vector<int> &memory);

  // Flush everything (branch misprediction / exception)
  void flush();

private:
  int latency = 1;
  int capacity = 4;

  std::vector<LSQEntry> RS;    // The reservation-station / queue entries
  std::vector<int> instr_list; // Pipeline shift-register of RS indices (same as EU)

  // Sequence counter shared across all LSQ entries (program order)
  unsigned long long seq_counter = 0;

  int findFreeEntry() const;

  // Returns the RS index of the oldest valid entry that has NOT been dispatched
  // AND is at the "head" of in-order execution (no older undispatched entry exists)
  int findInOrderReady() const;

  // Store-to-load forwarding: scan all older SW entries in the LSQ
  // for a matching address. Returns true and sets forwarded_val if found.
  bool forwardFromStore(int load_addr, int load_seq, int &forwarded_val) const;

  // Tracks SW instructions that have completed execution but not yet committed.
  // Keyed by ROB tag; value is {address, data}.
  struct PendingStore
  {
    int address;
    int value;
  };
  std::unordered_map<int, PendingStore> pending_stores;
};