#!/bin/bash

# SLURM Knapsack Benchmark Script for LAD Atlantica Cluster
# Uses srun instead of mpirun for cluster execution
# Focused on MPI Divide & Conquer and Hybrid MPI+OpenMP versions

# Configuration
STRONG_SCALABILITY_SIZE=28  # Fixed problem size for strong scalability
WEAK_SCALABILITY_BASE=20    # Base problem size for weak scalability
TIMEOUT=600  # 10 minutes in seconds
OUTPUT_DIR="benchmark_results_slurm"
CSV_FILE="$OUTPUT_DIR/benchmark_slurm.csv"
LOG_FILE="$OUTPUT_DIR/benchmark_slurm.log"

# SLURM configuration - adjust based on cluster specs
SLURM_PARTITION="cpu"  # Adjust based on your cluster partition

# Process configurations for MPI Divide & Conquer (2-16 processes)
MPI_PROCESSES=(2 4 6 8 10 12 14 16)

# Hybrid configurations (nodes:processes:threads_per_process) - max 2 nodes
HYBRID_CONFIGS=(
    "1:2:1"   # 1 node, 2 processes, 1 thread each
    "1:2:2"   # 1 node, 2 processes, 2 threads each
    "1:4:1"   # 1 node, 4 processes, 1 thread each
    "1:4:2"   # 1 node, 4 processes, 2 threads each
    "1:6:1"   # 1 node, 6 processes, 1 thread each
    "1:8:1"   # 1 node, 8 processes, 1 thread each
    "1:8:2"   # 1 node, 8 processes, 2 threads each
    "2:4:1"   # 2 nodes, 4 processes, 1 thread each
    "2:6:1"   # 2 nodes, 6 processes, 1 thread each
    "2:8:1"   # 2 nodes, 8 processes, 1 thread each
    "2:8:2"   # 2 nodes, 8 processes, 2 threads each
    "2:10:1"  # 2 nodes, 10 processes, 1 thread each
    "2:12:1"  # 2 nodes, 12 processes, 1 thread each
    "2:16:1"  # 2 nodes, 16 processes, 1 thread each
)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Initialize CSV with headers
echo "scalability_type,algorithm,implementation,problem_size,nodes,processes,threads_per_process,total_cores,execution_time_ms,speedup,efficiency,value,items_selected,weight,timeout_occurred" > "$CSV_FILE"

# Logging function
log() {
    echo -e "$(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_FILE"
}

# Function to extract execution time from output
extract_time() {
    local output="$1"
    echo "$output" | grep -o 'Tempo=[0-9]*\.[0-9]*' | sed 's/Tempo=//' | head -1
}

# Function to extract value from output
extract_value() {
    local output="$1"
    echo "$output" | grep -o 'Valor=[0-9]*' | sed 's/Valor=//' | head -1
}

# Function to extract items selected from output
extract_items() {
    local output="$1"
    echo "$output" | grep -o 'Itens=[0-9]*' | sed 's/Itens=//' | head -1
}

# Function to extract weight from output
extract_weight() {
    local output="$1"
    echo "$output" | grep -o 'Peso=[0-9]*' | sed 's/Peso=//' | head -1
}

# Function to run a SLURM benchmark test
run_slurm_benchmark() {
    local scalability_type="$1"
    local algorithm="$2"
    local implementation="$3"
    local executable="$4"
    local problem_size="$5"
    local nodes="$6"
    local processes="$7"
    local threads_per_process="$8"
    local data_file="$9"
    
    local total_cores=$((processes * threads_per_process))
    
    log "${BLUE}Running: $scalability_type - $algorithm ($implementation) - $problem_size items - $nodes nodes, $processes processes, $threads_per_process threads/proc (total: $total_cores cores)${NC}"
    
    # Construct srun command
    local srun_cmd="srun -N $nodes -n $processes"
    
    # Add partition if specified
    if [ ! -z "$SLURM_PARTITION" ]; then
        srun_cmd="$srun_cmd -p $SLURM_PARTITION"
    fi
    
    # Add exclusive flag for better performance isolation
    srun_cmd="$srun_cmd --exclusive"
    
    # Set OpenMP threads if hybrid version
    local env_vars=""
    if [ "$threads_per_process" != "1" ]; then
        env_vars="OMP_NUM_THREADS=$threads_per_process"
        srun_cmd="$srun_cmd --cpus-per-task=$threads_per_process"
    fi
    
    # Complete command
    local full_cmd="$env_vars $srun_cmd $executable \"$data_file\""
    
    # Run with timeout
    local start_time=$(date +%s)
    local output
    local exit_code
    local temp_output="/tmp/slurm_benchmark_output_$$.txt"
    
    if timeout $TIMEOUT bash -c "$full_cmd" > "$temp_output" 2>&1; then
        output=$(cat "$temp_output")
        exit_code=0
        local timeout_occurred="false"
    else
        output=$(cat "$temp_output" 2>/dev/null || echo "")
        exit_code=1
        local timeout_occurred="true"
        log "${RED}TIMEOUT: $algorithm ($implementation) - $problem_size items - $nodes nodes, $processes processes${NC}"
    fi
    
    local end_time=$(date +%s)
    local wall_time=$((end_time - start_time))
    local wall_time_ms=$((wall_time * 1000))
    
    # Extract metrics from output
    local exec_time=$(extract_time "$output")
    local value=$(extract_value "$output")
    local items=$(extract_items "$output")
    local weight=$(extract_weight "$output")
    
    # Handle timeout case
    if [ "$timeout_occurred" = "true" ]; then
        exec_time="$wall_time_ms"
        value="${value:-0}"
        items="${items:-0}"
        weight="${weight:-0}"
    fi
    
    # Use extracted time if available, otherwise use wall time
    exec_time="${exec_time:-$wall_time_ms}"
    
    # Calculate speedup and efficiency (will be computed later against sequential baseline)
    local speedup="0"
    local efficiency="0"
    
    # Write to CSV
    echo "$scalability_type,$algorithm,$implementation,$problem_size,$nodes,$processes,$threads_per_process,$total_cores,$exec_time,$speedup,$efficiency,$value,$items,$weight,$timeout_occurred" >> "$CSV_FILE"
    
    log "${GREEN}Completed: $exec_time ms${NC}"
    rm -f "$temp_output"
}

# Function to calculate problem size for weak scalability
calculate_weak_size() {
    local processes="$1"
    local base_size="$2"
    # Scale problem size proportionally with processes (base_size + extra per process)
    local extra_per_process=2
    echo $((base_size + (processes - 1) * extra_per_process))
}

# Function to calculate nodes needed for given processes
calculate_nodes() {
    local processes="$1"
    local max_procs_per_node=8  # Adjust based on cluster specs
    echo $(((processes + max_procs_per_node - 1) / max_procs_per_node))
}

# Check if modules are needed (uncomment and adjust if required)
# log "Loading required modules..."
# module purge
# module load gcc/11.2.0 || module load gcc
# module load openmpi/4.1.1 || module load openmpi
# log "Loaded modules: $(module list 2>&1)"

log "Using system compilers: gcc and mpicc"
log "GCC version: $(gcc --version | head -1)"
log "MPI version: $(mpicc --version | head -1)"

# Main benchmark execution
log "${YELLOW}Starting SLURM Knapsack Benchmark for LAD Atlantica${NC}"
log "Strong scalability: Fixed problem size $STRONG_SCALABILITY_SIZE items"
log "Weak scalability: Base problem size $WEAK_SCALABILITY_BASE items (scales with processes)"
log "Process counts: 2-16"
log "Timeout: $TIMEOUT seconds per test"
log "SLURM Partition: ${SLURM_PARTITION:-default}"

# Clean and rebuild to ensure proper linking
log "Cleaning and building all implementations..."
make clean >> "$LOG_FILE" 2>&1
make all >> "$LOG_FILE" 2>&1

# Check if build was successful
if [ $? -ne 0 ]; then
    log "${RED}Build failed! Check if modules are loaded correctly${NC}"
    log "Available modules:"
    module avail 2>> "$LOG_FILE"
    exit 1
fi

# Generate baseline test data for strong scalability
log "${YELLOW}=== STRONG SCALABILITY TESTS ===${NC}"
strong_capacity=$((STRONG_SCALABILITY_SIZE * 100))
strong_data_file="$OUTPUT_DIR/test_strong_${STRONG_SCALABILITY_SIZE}.txt"
log "Generating strong scalability test data: $STRONG_SCALABILITY_SIZE items, capacity $strong_capacity"
./generate_input $STRONG_SCALABILITY_SIZE $strong_capacity 50 100 "$strong_data_file" >> "$LOG_FILE" 2>&1

if [ ! -f "$strong_data_file" ]; then
    log "${RED}ERROR: Failed to generate $strong_data_file${NC}"
    exit 1
fi

# Sequential baseline for strong scalability
run_slurm_benchmark "Strong" "Brute Force" "Sequential" "./knapsack_sequential" $STRONG_SCALABILITY_SIZE 1 1 1 "$strong_data_file"

# MPI Divide & Conquer - Strong scalability
for processes in "${MPI_PROCESSES[@]}"; do
    nodes=$(calculate_nodes $processes)
    run_slurm_benchmark "Strong" "Brute Force" "MPI_Divide_Conquer" "./knapsack_mpi_divide_conquer" $STRONG_SCALABILITY_SIZE $nodes $processes 1 "$strong_data_file"
done

# Hybrid MPI+OpenMP - Strong scalability
for config in "${HYBRID_CONFIGS[@]}"; do
    IFS=':' read -r nodes processes threads <<< "$config"
    run_slurm_benchmark "Strong" "Brute Force" "MPI_OpenMP_Hybrid" "./knapsack_mpi_openmp_hybrid" $STRONG_SCALABILITY_SIZE $nodes $processes $threads "$strong_data_file"
done

log "${GREEN}Completed strong scalability tests${NC}"

# Weak scalability tests
log "${YELLOW}=== WEAK SCALABILITY TESTS ===${NC}"

# Sequential baseline for weak scalability (smallest problem size)
weak_seq_size=$WEAK_SCALABILITY_BASE
weak_seq_capacity=$((weak_seq_size * 100))
weak_seq_data_file="$OUTPUT_DIR/test_weak_seq_${weak_seq_size}.txt"
log "Generating weak scalability sequential baseline: $weak_seq_size items, capacity $weak_seq_capacity"
./generate_input $weak_seq_size $weak_seq_capacity 50 100 "$weak_seq_data_file" >> "$LOG_FILE" 2>&1

if [ ! -f "$weak_seq_data_file" ]; then
    log "${RED}ERROR: Failed to generate $weak_seq_data_file${NC}"
    exit 1
fi

run_slurm_benchmark "Weak" "Brute Force" "Sequential" "./knapsack_sequential" $weak_seq_size 1 1 1 "$weak_seq_data_file"

# MPI Divide & Conquer - Weak scalability
for processes in "${MPI_PROCESSES[@]}"; do
    weak_size=$(calculate_weak_size $processes $WEAK_SCALABILITY_BASE)
    weak_capacity=$((weak_size * 100))
    weak_data_file="$OUTPUT_DIR/test_weak_mpi_${weak_size}.txt"
    
    log "Generating weak scalability test data: $weak_size items (for $processes processes), capacity $weak_capacity"
    ./generate_input $weak_size $weak_capacity 50 100 "$weak_data_file" >> "$LOG_FILE" 2>&1
    
    if [ ! -f "$weak_data_file" ]; then
        log "${RED}ERROR: Failed to generate $weak_data_file${NC}"
        continue
    fi
    
    nodes=$(calculate_nodes $processes)
    run_slurm_benchmark "Weak" "Brute Force" "MPI_Divide_Conquer" "./knapsack_mpi_divide_conquer" $weak_size $nodes $processes 1 "$weak_data_file"
done

# Hybrid MPI+OpenMP - Weak scalability
for config in "${HYBRID_CONFIGS[@]}"; do
    IFS=':' read -r nodes processes threads <<< "$config"
    total_cores=$((processes * threads))
    weak_size=$(calculate_weak_size $total_cores $WEAK_SCALABILITY_BASE)
    weak_capacity=$((weak_size * 100))
    weak_data_file="$OUTPUT_DIR/test_weak_hybrid_${weak_size}.txt"
    
    log "Generating weak scalability test data: $weak_size items (for $total_cores total cores), capacity $weak_capacity"
    ./generate_input $weak_size $weak_capacity 50 100 "$weak_data_file" >> "$LOG_FILE" 2>&1
    
    if [ ! -f "$weak_data_file" ]; then
        log "${RED}ERROR: Failed to generate $weak_data_file${NC}"
        continue
    fi
    
    run_slurm_benchmark "Weak" "Brute Force" "MPI_OpenMP_Hybrid" "./knapsack_mpi_openmp_hybrid" $weak_size $nodes $processes $threads "$weak_data_file"
done

log "${GREEN}Completed weak scalability tests${NC}"

log "${GREEN}SLURM Benchmark completed! Results saved to: $CSV_FILE${NC}"
log "Log file: $LOG_FILE"

# Generate summary statistics
log "Generating summary..."
python3 -c "
import pandas as pd
import sys

try:
    df = pd.read_csv('$CSV_FILE')
    print('\n=== SLURM BENCHMARK SUMMARY ===')
    print(f'Total tests executed: {len(df)}')
    print(f'Successful tests: {len(df[df[\"timeout_occurred\"] == False])}')
    print(f'Timeout tests: {len(df[df[\"timeout_occurred\"] == True])}')
    print('\nImplementations tested:')
    for impl in df['implementation'].unique():
        count = len(df[df['implementation'] == impl])
        success = len(df[(df['implementation'] == impl) & (df['timeout_occurred'] == False)])
        print(f'  {impl}: {success}/{count} successful')
    print('\nData saved to: $CSV_FILE')
except Exception as e:
    print(f'Error generating summary: {e}')
" 2>/dev/null || log "Python summary generation failed (pandas not available)"

log "${YELLOW}To submit this as a SLURM job, create a job script with appropriate resource requests${NC}"
log "${YELLOW}Example: sbatch --nodes=8 --ntasks=32 --time=2:00:00 run_slurm_benchmark.sh${NC}"