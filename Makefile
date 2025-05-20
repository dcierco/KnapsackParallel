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

# Source files
SRC_SEQ = knapsack_sequential.c
SRC_MPI = knapsack_mpi.c
SRC_GENERATE = generate_input.c
# Common header (dependency for knapsack solvers)
COMMON_HEADER = knapsack_common.h

# Data file
DATA_FILE ?= knapsack_data.txt
# Parameters for data generation (can be overridden on the command line for make generate_data)
# Example: make generate_data NUM_ITEMS=50 CAPACITY=100
NUM_ITEMS ?= 1000
CAPACITY ?= 20000
MAX_WEIGHT ?= 500
MAX_VALUE ?= 750


.PHONY: all clean run_sequential run_mpi generate_data

all: $(TARGET_SEQ) $(TARGET_MPI) $(TARGET_GENERATE)

$(TARGET_SEQ): $(SRC_SEQ) $(COMMON_HEADER)
	$(CC) $(CFLAGS) -o $@ $(SRC_SEQ)

$(TARGET_MPI): $(SRC_MPI) $(COMMON_HEADER)
	$(MPICC) $(CFLAGS) -o $@ $(SRC_MPI)

$(TARGET_GENERATE): $(SRC_GENERATE)
	$(CC) $(CFLAGS) -o $@ $(SRC_GENERATE)

# Rule to generate the data file. Depends on the generator executable.
# This target is explicitly called or used as a dependency.
generate_data: $(TARGET_GENERATE)
	@echo "Generating knapsack data file: $(DATA_FILE) with N=$(NUM_ITEMS), W=$(CAPACITY)..."
	./$(TARGET_GENERATE) $(NUM_ITEMS) $(CAPACITY) $(MAX_WEIGHT) $(MAX_VALUE) $(DATA_FILE)

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
	rm -rf $(TARGET_SEQ) $(TARGET_MPI) $(TARGET_GENERATE) $(DATA_FILE) *.o core.* *.dSYM*/
	@echo "Done."
