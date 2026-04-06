#pragma once
#include "Basics.h"
#include <iostream>
#include <vector>

class BranchPredictor
{
public:
    int total_branches = 0;
    int correct_predictions = 0;

    int counter = 0;

    int predict(int current_pc, int imm, OpCode op)
    {
        // Here we will implement a 2-bit branch predictor.
        if (counter >= 2)
            return 1; // Predict taken
        else
            return 0; // Predict not taken
    }

    void update(int pc, int actual_target, bool taken, bool was_correct)
    {
        total_branches++;

        if (taken)
            counter = std::min(counter + 1, 3); // Move towards strongly taken
        else
            counter = std::max(counter - 1, 0); // Move towards strongly not taken
            
        if (was_correct)
            correct_predictions++;
    }
};