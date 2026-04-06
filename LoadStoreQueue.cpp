#include "LoadStoreQueue.h"

inline void LoadStoreQueue::capture(int tag, int val)
{
  // Capture the result of an instruction with the given tag and value
  // This function can be used to update the state of the LSQ entry when a result is produced
}

inline void LoadStoreQueue::executeCycle(std::vector<int> &Memory)
{
  // Execute one cycle of the LSQ
  // This function can be used to perform memory operations based on the captured data and update the state of the LSQ entry
}
