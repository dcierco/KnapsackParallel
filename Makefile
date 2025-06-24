# Compiler and flags
CC = /opt/homebrew/bin/gcc-15
MPICC = mpicc
# MPI+OpenMP hybrid needs gcc instead of clang for OpenMP support
MPI_INCLUDE = -I/opt/homebrew/Cellar/open-mpi/5.0.7/include
MPI_LIBDIR = -L/opt/homebrew/Cellar/open-mpi/5.0.7/lib
MPI_LIBS = -lmpi
CFLAGS = -Wall -g -O2 -std=c99 -D_GNU_SOURCE # Using GNU_SOURCE for snprintf and other functions
OPENMP_FLAGS = -fopenmp

# Number of processes for MPI run, can be overridden e.g., make run_mpi NP=8
NP ?= 4

# Executables
TARGET_SEQ = knapsack_sequential
TARGET_MPI = knapsack_mpi
TARGET_OPENMP = knapsack_openmp
TARGET_MPI_DC = knapsack_mpi_divide_conquer
TARGET_HYBRID = knapsack_mpi_openmp_hybrid
TARGET_GENERATE = generate_input

# Source files
SRC_SEQ = knapsack_sequential.c
SRC_MPI = knapsack_mpi.c
SRC_OPENMP = knapsack_openmp.c
SRC_MPI_DC = knapsack_mpi_divide_conquer.c
SRC_HYBRID = knapsack_mpi_openmp_hybrid.c
SRC_GENERATE = generate_input.c
# Common header (dependency for knapsack solvers)
COMMON_HEADER = knapsack_common.h

# Default test size
N ?= 20


.PHONY: all clean run_sequential run_mpi run_openmp run_mpi_dc run_hybrid generate comprehensive_benchmark analyze_results

all: $(TARGET_SEQ) $(TARGET_MPI) $(TARGET_OPENMP) $(TARGET_MPI_DC) $(TARGET_HYBRID) $(TARGET_GENERATE)

$(TARGET_SEQ): $(SRC_SEQ) $(COMMON_HEADER)
	$(CC) $(CFLAGS) -o $@ $(SRC_SEQ)

$(TARGET_MPI): $(SRC_MPI) $(COMMON_HEADER)
	$(MPICC) $(CFLAGS) -o $@ $(SRC_MPI)

$(TARGET_OPENMP): $(SRC_OPENMP) $(COMMON_HEADER)
	$(CC) $(CFLAGS) $(OPENMP_FLAGS) -o $@ $(SRC_OPENMP)

$(TARGET_MPI_DC): $(SRC_MPI_DC) $(COMMON_HEADER)
	$(MPICC) $(CFLAGS) -o $@ $(SRC_MPI_DC)

$(TARGET_HYBRID): $(SRC_HYBRID) $(COMMON_HEADER)
	$(CC) $(CFLAGS) $(OPENMP_FLAGS) $(MPI_INCLUDE) $(MPI_LIBDIR) $(MPI_LIBS) -o $@ $(SRC_HYBRID)

$(TARGET_GENERATE): $(SRC_GENERATE)
	$(CC) $(CFLAGS) -o $@ $(SRC_GENERATE)

# Simple generator that takes N as parameter
# Usage: make generate N=20
generate: $(TARGET_GENERATE)
	@if [ -z "$(N)" ]; then \
		echo "Usage: make generate N=<number_of_items>"; \
		echo "Example: make generate N=20"; \
		exit 1; \
	fi
	@echo "Generating test file with $(N) items..."
	./$(TARGET_GENERATE) $(N) 1000 10 100 test_$(N).txt
	@echo "Generated: test_$(N).txt"

# Quick test with a small problem
test: $(TARGET_SEQ) $(TARGET_MPI) $(TARGET_OPENMP) $(TARGET_MPI_DC) $(TARGET_HYBRID)
	@$(MAKE) generate N=10
	@echo "=== Testing all implementations with 10 items ==="
	@./$(TARGET_SEQ) test_10.txt
	@mpirun -np 2 ./$(TARGET_MPI) test_10.txt
	@mpirun -np 2 ./$(TARGET_MPI_DC) test_10.txt
	@export OMP_NUM_THREADS=2 && mpirun -np 2 ./$(TARGET_HYBRID) test_10.txt
	@rm -f test_10.txt
	@echo "=== Test complete ==="

# Comprehensive benchmark with extensive scalability analysis
comprehensive_benchmark: all
	@echo "=== COMPREHENSIVE SCALABILITY BENCHMARK ==="
	@echo "This will test problem sizes 20-37 items with extensive parallelization"
	@echo "Expected runtime: 2-4 hours depending on system performance"
	@echo "Results will be saved in benchmark_results/ directory"
	@echo ""
	@read -p "Continue? (y/N): " confirm; \
	if [ "$$confirm" = "y" ] || [ "$$confirm" = "Y" ]; then \
		./run_comprehensive_benchmark.sh; \
	else \
		echo "Benchmark cancelled."; \
	fi

# Analyze benchmark results and generate graphs
analyze_results:
	@echo "=== ANALYZING BENCHMARK RESULTS ==="
	@if [ ! -f benchmark_results/benchmark_data.csv ]; then \
		echo "Error: No benchmark data found. Run 'make comprehensive_benchmark' first."; \
		exit 1; \
	fi
	@echo "Generating scalability graphs and analysis..."
	python3 analyze_benchmark.py
	@echo "Analysis complete! Check benchmark_results/graphs/ directory."

# No automatic data file generation - use make generate N=<size> instead

run_sequential: $(TARGET_SEQ)
	@$(MAKE) generate N=$(N)
	@echo "Running Sequential version with $(N) items..."
	./$(TARGET_SEQ) test_$(N).txt

run_mpi: $(TARGET_MPI)
	@$(MAKE) generate N=$(N)
	@echo "Running MPI version with $(NP) processes and $(N) items..."
	mpirun -np $(NP) --oversubscribe ./$(TARGET_MPI) test_$(N).txt

run_openmp: $(TARGET_OPENMP)
	@$(MAKE) generate N=$(N)
	@echo "Running OpenMP version with $(N) items..."
	./$(TARGET_OPENMP) test_$(N).txt

run_mpi_dc: $(TARGET_MPI_DC)
	@$(MAKE) generate N=$(N)
	@echo "Running MPI Divide and Conquer version with $(NP) processes and $(N) items..."
	mpirun -np $(NP) --oversubscribe ./$(TARGET_MPI_DC) test_$(N).txt

run_hybrid: $(TARGET_HYBRID)
	@$(MAKE) generate N=$(N)
	@echo "Running Hybrid MPI+OpenMP version with $(NP) processes and $(N) items..."
	mpirun -np $(NP) --oversubscribe ./$(TARGET_HYBRID) test_$(N).txt

clean:
	@echo "Cleaning up..."
	rm -rf $(TARGET_SEQ) $(TARGET_MPI) $(TARGET_OPENMP) $(TARGET_MPI_DC) $(TARGET_HYBRID) $(TARGET_GENERATE) $(DATA_FILE) *.o core.* *.dSYM*/
	rm -f *.txt benchmark_*.txt knapsack_*.txt test_*.txt
	rm -rf benchmark_results/
	rm -f *.png *.pdf *.log *.csv
	@echo "Done."
