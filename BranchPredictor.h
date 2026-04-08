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

    // Map from PC to the 2-bit state (0, 1, 2, or 3)
    std::unordered_map<int, int> bht;

    int predict(int current_pc, int imm, OpCode op)
    {
        // Here we will implement a 2-bit per-instruction branch predictor.
        // Default state is 0
        int state = 0;
        if (bht.count(current_pc)) {
            state = bht[current_pc];
        }

        if (state <= 1)
            return 1; // Predict taken (States 0, 1)
        else
            return 0; // Predict not taken (States 2, 3)
    }

    void update(int pc, int actual_target, bool taken, bool was_correct)
    {
        total_branches++;

        int state = 0;
        if (bht.count(pc)) {
            state = bht[pc];
        }

        if (taken)
            state = std::max(state - 1, 0); // Move towards predicting taken
        else
            state = std::min(state + 1, 3); // Move towards predicting not taken
            
        bht[pc] = state;

        if (was_correct)
            correct_predictions++;
    }
};