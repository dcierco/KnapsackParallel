# Compiler and flags
CC = /opt/homebrew/bin/gcc-14
MPICC = mpicc
CFLAGS = -Wall -g -O2 -std=c99 # Using C99 for compatibility, e.g. for variable declarations in loops

# Number of processes for MPI run, can be overridden e.g., make run_mpi NP=8
NP ?= 4

# Executables
TARGET_SEQ = knapsack_sequential
TARGET_MPI = knapsack_mpi
TARGET_GENERATE = generate_input
TARGET_CHALLENGING = generate_challenging

# Source files
SRC_SEQ = knapsack_sequential.c
SRC_MPI = knapsack_mpi.c
SRC_GENERATE = generate_input.c
SRC_CHALLENGING = generate_challenging.c
# Common header (dependency for knapsack solvers)
COMMON_HEADER = knapsack_common.h

# Data file
DATA_FILE ?= knapsack_data.txt
# Parameters for data generation (can be overridden on the command line for make generate_data)
# Example: make generate_data NUM_ITEMS=50 CAPACITY=100
# Default values are much larger and harder to showcase MPI performance
NUM_ITEMS ?= 50000
CAPACITY ?= 1000000
MAX_WEIGHT ?= 2000
MAX_VALUE ?= 3000


.PHONY: all clean run_sequential run_mpi generate_data generate_easy generate_medium generate_hard generate_extreme generate_challenging_data benchmark

all: $(TARGET_SEQ) $(TARGET_MPI) $(TARGET_GENERATE) $(TARGET_CHALLENGING)

$(TARGET_SEQ): $(SRC_SEQ) $(COMMON_HEADER)
	$(CC) $(CFLAGS) -o $@ $(SRC_SEQ)

$(TARGET_MPI): $(SRC_MPI) $(COMMON_HEADER)
	$(MPICC) $(CFLAGS) -o $@ $(SRC_MPI)

$(TARGET_GENERATE): $(SRC_GENERATE)
	$(CC) $(CFLAGS) -o $@ $(SRC_GENERATE)

$(TARGET_CHALLENGING): $(SRC_CHALLENGING)
	$(CC) $(CFLAGS) -o $@ $(SRC_CHALLENGING)

# Rule to generate the data file. Depends on the generator executable.
# This target is explicitly called or used as a dependency.
generate_data: $(TARGET_GENERATE)
	@echo "Generating knapsack data file: $(DATA_FILE) with N=$(NUM_ITEMS), W=$(CAPACITY)..."
	./$(TARGET_GENERATE) $(NUM_ITEMS) $(CAPACITY) $(MAX_WEIGHT) $(MAX_VALUE) $(DATA_FILE)

# Generate different difficulty levels for testing MPI performance
generate_easy: $(TARGET_GENERATE)
	@echo "Generating EASY dataset (small for testing)..."
	./$(TARGET_GENERATE) 1000 50000 1000 1500 knapsack_easy.txt

generate_medium: $(TARGET_GENERATE)
	@echo "Generating MEDIUM dataset (moderate challenge)..."
	./$(TARGET_GENERATE) 10000 500000 1500 2500 knapsack_medium.txt

generate_hard: $(TARGET_GENERATE)
	@echo "Generating HARD dataset (high challenge)..."
	./$(TARGET_GENERATE) 25000 750000 2000 3000 knapsack_hard.txt

generate_extreme: $(TARGET_GENERATE)
	@echo "Generating EXTREME dataset (maximum challenge)..."
	./$(TARGET_GENERATE) 50000 1000000 2000 3000 knapsack_extreme.txt

generate_challenging_data: $(TARGET_CHALLENGING)
	@echo "Generating CHALLENGING dataset (forces branch-and-bound exploration)..."
	./$(TARGET_CHALLENGING) 5000 150000 knapsack_challenging.txt

# Comprehensive benchmark across different dataset sizes and process counts
benchmark: $(TARGET_SEQ) $(TARGET_MPI) generate_easy generate_medium generate_hard generate_challenging_data
	@echo "=== MPI KNAPSACK PERFORMANCE BENCHMARK ==="
	@echo ""
	@echo "Dataset: EASY (1K items)"
	@./$(TARGET_SEQ) knapsack_easy.txt
	@mpirun -np 2 --oversubscribe ./$(TARGET_MPI) knapsack_easy.txt
	@mpirun -np 4 --oversubscribe ./$(TARGET_MPI) knapsack_easy.txt
	@echo ""
	@echo "Dataset: MEDIUM (10K items)" 
	@./$(TARGET_SEQ) knapsack_medium.txt
	@mpirun -np 4 --oversubscribe ./$(TARGET_MPI) knapsack_medium.txt
	@mpirun -np 8 --oversubscribe ./$(TARGET_MPI) knapsack_medium.txt
	@echo ""
	@echo "Dataset: HARD (25K items)"
	@mpirun -np 4 --oversubscribe ./$(TARGET_MPI) knapsack_hard.txt
	@mpirun -np 8 --oversubscribe ./$(TARGET_MPI) knapsack_hard.txt
	@echo ""
	@echo "Dataset: CHALLENGING (5K items, forces exploration)"
	@./$(TARGET_SEQ) knapsack_challenging.txt
	@mpirun -np 4 --oversubscribe ./$(TARGET_MPI) knapsack_challenging.txt
	@mpirun -np 8 --oversubscribe ./$(TARGET_MPI) knapsack_challenging.txt
	@echo ""
	@echo "=== BENCHMARK COMPLETE ==="

# Rule to build the data file if it's missing or if the generator program is newer.
# This is implicitly used when $(DATA_FILE) is a dependency of another target (e.g., run_sequential).
# It uses the NUM_ITEMS, CAPACITY settings defined in the Makefile or overridden globally for the 'make' command
# that triggers this rule.
$(DATA_FILE): $(TARGET_GENERATE)
	@echo "Ensuring $(DATA_FILE) is up to date (N=$(NUM_ITEMS), W=$(CAPACITY), MaxItemW=$(MAX_WEIGHT), MaxItemV=$(MAX_VALUE))..."
	./$(TARGET_GENERATE) $(NUM_ITEMS) $(CAPACITY) $(MAX_WEIGHT) $(MAX_VALUE) $(DATA_FILE)

run_sequential: $(TARGET_SEQ) $(DATA_FILE)
	@echo "Running sequential version with $(DATA_FILE)..."
	./$(TARGET_SEQ) $(DATA_FILE)

run_mpi: $(TARGET_MPI) $(DATA_FILE)
	@echo "Running MPI version with $(NP) processes using $(DATA_FILE)..."
	@echo "Note: If problem size in $(DATA_FILE) is small, performance may vary or slaves might have no work."
	mpirun -np $(NP) --oversubscribe ./$(TARGET_MPI) $(DATA_FILE)

clean:
	@echo "Cleaning up..."
	rm -rf $(TARGET_SEQ) $(TARGET_MPI) $(TARGET_GENERATE) $(TARGET_CHALLENGING) $(DATA_FILE) knapsack_*.txt *.o core.* *.dSYM*/
	@echo "Done."
