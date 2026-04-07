# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall

# ==========================================
# make compile FILE=<filename.cpp>
# ==========================================
# This target should compile your files with the provided 
# main.cpp. The main.cpp will always #include "Processor.h" 
# and will have its own main() function.
compile:
	@echo "Compiling simulator:"
	$(CXX) $(CXXFLAGS) -DSIMULATOR -g -O0 main.cpp Processor.cpp ExecutionUnit.cpp LoadStoreQueue.cpp compiler/compiler.cpp -o main
	@echo "Build successful, 'main' created."
	./main programs/code3.txt


# ==========================================
# make run FILE=<filename.s>
# ==========================================
# Update this target to run whatever script or 
# program you wrote to preprocess the assembly labels. 
# Example below assumes a Python script named 'compiler.py'.
run:
	@echo "Preprocessing $(FILE)..."
	g++ compiler/compiler.cpp -o compiler.out
	./compiler.out $(FILE)
	@echo "Preprocessing complete."
