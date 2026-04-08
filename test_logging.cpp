#include <iostream>
#include "Processor.h"

int main() {
    ProcessorConfig config;
    Processor cpu = Processor(config);
    
    try {
        cpu.loadProgram("programs/code1.txt");
    } catch (...) {
        std::cerr << "Failed to parse instruction file.\n";
        return 1;
    }
    
    std::cout << "\n=== RUNNING WITH BASIC LOGGING ===" << std::endl;
    
    // Run 3 cycles to show the logging
    for (int i = 0; i < 3; i++) {
        if (!cpu.step()) break;
    }
    
    std::cout << "\n\n=== DEMONSTRATION COMPLETE ===" << std::endl;
    std::cout << "You can now see how instructions move through the pipeline." << std::endl;
    std::cout << "Each cycle shows: FETCH -> DECODE -> EXECUTE -> COMMIT stages\n" << std::endl;
    
    return 0;
}
