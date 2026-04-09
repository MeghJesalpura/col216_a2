#pragma once
#include "Basics.h"
#include <iostream>
#include <vector>
#include <unordered_map>

class BranchPredictor
{
public:
    int total_branches = 0;
    int correct_predictions = 0;

    std::unordered_map<int, int> bht;

    int predict(int current_pc, int imm, OpCode op)
    {
        int state = 0;
        if (bht.count(current_pc))
        {
            state = bht[current_pc];
        }

        if (state <= 1)
            return 1;
        else
            return 0;
    }

    void update(int pc, int actual_target, bool taken, bool was_correct)
    {
        total_branches++;

        int state = 0;
        if (bht.count(pc))
        {
            state = bht[pc];
        }

        if (taken)
            state = std::max(state - 1, 0);
        else
            state = std::min(state + 1, 3);

        bht[pc] = state;

        if (was_correct)
            correct_predictions++;
    }
};