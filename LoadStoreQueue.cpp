#include "LoadStoreQueue.h"

LoadStoreQueue::LoadStoreQueue(int latency, int lsq_capacity) : latency(latency), lsq_capacity(lsq_capacity), lsq_filled(0) {}

bool LoadStoreQueue::has_space()
{
  return lsq_filled < lsq_capacity;
}

void LoadStoreQueue::capture(int tag, int val)
{
  for (auto &entry : q)
  {
    if (!entry.addr_ready && entry.addr_tag == tag)
    {
      entry.addr_val = val;
      entry.addr_ready = true;
    }

    if (entry.type == LSQEntryType::STORE && !entry.data_ready && entry.data_tag == tag)
    {
      entry.data_val = val;
      entry.data_ready = true;
    }
  }
}

void LoadStoreQueue::executeCycle(std::vector<int> &Memory)
{
  has_result = false;
  has_exception = false;

  if (q.empty())
    return;

  auto it = q.begin();
  while (it != q.end() && it->done)
  {
    it++;
  }

  if (it == q.end())
    return;

  LSQEntry &entry = *it;

  if (!entry.addr_ready)
    return;

  if (!entry.dispatched)
  {
    entry.dispatched = true;
    entry.cycles_left = latency;
  }

  if (entry.cycles_left > 0)
  {
    entry.cycles_left--;
  }

  if (entry.cycles_left > 0)
    return;

  entry.eff_addr = entry.addr_val + entry.offset;
  int eff_addr = entry.eff_addr;

  if (entry.type == LSQEntryType::LOAD)
  {
    if (eff_addr < 0 || eff_addr >= static_cast<int>(Memory.size()))
    {
      if (!entry.broadcasted)
      {
        has_result = true;
        has_exception = true;
        result_tag = entry.dest_tag;
        result_val = 0;
        entry.done = true;
        entry.exception = true;
        entry.broadcasted = true;
      }
      return;
    }

    int loaded_val = Memory[eff_addr];
    auto curr = it;
    while (curr != q.begin())
    {
      --curr;
      if (curr->type == LSQEntryType::STORE && curr->eff_addr == eff_addr)
      {
        loaded_val = curr->data_val;
        break;
      }
    }

    if (!entry.broadcasted)
    {
      has_result = true;
      has_exception = false;
      result_tag = entry.dest_tag;
      result_val = loaded_val;
      entry.done = true;
      entry.result = loaded_val;
      entry.broadcasted = true;
    }
  }
  else if (entry.type == LSQEntryType::STORE)
  {
    if (!entry.data_ready)
      return;

    if (eff_addr < 0 || eff_addr >= static_cast<int>(Memory.size()))
    {
      if (!entry.broadcasted)
      {
        has_result = true;
        has_exception = true;
        result_tag = entry.dest_tag;
        result_val = 0;
        entry.done = true;
        entry.exception = true;
        entry.broadcasted = true;
      }
      return;
    }

    if (!entry.broadcasted)
    {
      has_result = true;
      has_exception = false;
      result_tag = entry.dest_tag;
      result_val = entry.data_val;
      entry.done = true;
      entry.result = eff_addr;
      entry.broadcasted = true;
    }
  }
}

void LoadStoreQueue::commitEntry(int tag, std::vector<int> &Memory)
{
  if (!q.empty() && q.front().dest_tag == tag)
  {
    if (q.front().type == LSQEntryType::STORE && !q.front().exception)
    {
      if (q.front().eff_addr >= 0 && q.front().eff_addr < static_cast<int>(Memory.size()))
      {
        Memory[q.front().eff_addr] = q.front().data_val;
      }
    }
    q.pop_front();
    lsq_filled--;
  }
}

void LoadStoreQueue::flush()
{
  q.clear();
  lsq_filled = 0;
  has_result = false;
  has_exception = false;
}
