#!/bin/bash

# Comprehensive Knapsack Brute Force Benchmark Script
# Tests problem sizes from 20 to 37 items with extensive parallelization configurations
# Generates CSV data for scalability analysis

# Configuration
MIN_ITEMS=20
MAX_ITEMS=37
TIMEOUT=600  # 10 minutes in seconds
OUTPUT_DIR="benchmark_results"
CSV_FILE="$OUTPUT_DIR/benchmark_data.csv"
LOG_FILE="$OUTPUT_DIR/benchmark.log"

# Parallel configuration
MIN_THREADS=2
MAX_THREADS=16
MPI_PROCESSES=(2 4 6 8 12 16)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Initialize CSV with headers
echo "algorithm,implementation,problem_size,processes_threads,execution_time_ms,speedup,efficiency,value,items_selected,weight,nodes_explored,timeout_occurred" > "$CSV_FILE"

# Logging function
log() {
    echo -e "$(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_FILE"
}

# Function to extract execution time from output (BSD grep compatible)
extract_time() {
    local output="$1"
    echo "$output" | grep -o 'Tempo=[0-9]*\.[0-9]*' | sed 's/Tempo=//' | head -1
}

# Function to extract value from output (BSD grep compatible)
extract_value() {
    local output="$1"
    echo "$output" | grep -o 'Valor=[0-9]*' | sed 's/Valor=//' | head -1
}

# Function to extract items selected from output (BSD grep compatible)
extract_items() {
    local output="$1"
    echo "$output" | grep -o 'Itens=[0-9]*' | sed 's/Itens=//' | head -1
}

# Function to extract weight from output (BSD grep compatible)
extract_weight() {
    local output="$1"
    echo "$output" | grep -o 'Peso=[0-9]*' | sed 's/Peso=//' | head -1
}

# Function to extract nodes explored from output (BSD grep compatible)
extract_nodes() {
    local output="$1"
    echo "$output" | grep -o 'Nós=[0-9]*' | sed 's/Nós=//' | head -1 2>/dev/null || echo "0"
}

# Function to run a single benchmark test
run_benchmark() {
    local algorithm="$1"
    local implementation="$2"
    local command="$3"
    local problem_size="$4"
    local proc_threads="$5"
    local data_file="$6"
    
    log "${BLUE}Running: $algorithm ($implementation) - $problem_size items - $proc_threads proc/threads${NC}"
    
    # Run with timeout
    local start_time=$(date +%s)
    local output
    local exit_code
    local temp_output="/tmp/benchmark_output_$$.txt"
    
    if timeout $TIMEOUT bash -c "$command" > "$temp_output" 2>&1; then
        output=$(cat "$temp_output")
        exit_code=0
        local timeout_occurred="false"
    else
        output=$(cat "$temp_output" 2>/dev/null || echo "")
        exit_code=1
        local timeout_occurred="true"
        log "${RED}TIMEOUT: $algorithm ($implementation) - $problem_size items - $proc_threads proc/threads${NC}"
    fi
    
    local end_time=$(date +%s)
    local wall_time=$((end_time - start_time))
    local wall_time_ms=$((wall_time * 1000))
    
    # Extract metrics from output
    local exec_time=$(extract_time "$output")
    local value=$(extract_value "$output")
    local items=$(extract_items "$output")
    local weight=$(extract_weight "$output")
    local nodes=$(extract_nodes "$output")
    
    # Handle timeout case
    if [ "$timeout_occurred" = "true" ]; then
        exec_time="$wall_time_ms"
        value="${value:-0}"
        items="${items:-0}"
        weight="${weight:-0}"
        nodes="${nodes:-0}"
    fi
    
    # Use extracted time if available, otherwise use wall time
    exec_time="${exec_time:-$wall_time_ms}"
    
    # Calculate speedup and efficiency (will be computed later against sequential baseline)
    local speedup="0"
    local efficiency="0"
    
    # Write to CSV
    echo "$algorithm,$implementation,$problem_size,$proc_threads,$exec_time,$speedup,$efficiency,$value,$items,$weight,$nodes,$timeout_occurred" >> "$CSV_FILE"
    
    log "${GREEN}Completed: $exec_time ms${NC}"
    rm -f "$temp_output"
}

# Main benchmark execution
log "${YELLOW}Starting Comprehensive Knapsack Benchmark${NC}"
log "Problem sizes: $MIN_ITEMS to $MAX_ITEMS items"
log "Timeout: $TIMEOUT seconds per test"
log "Parallel configurations: $MIN_THREADS to $MAX_THREADS threads/processes"

# Ensure all executables are built
log "Building all implementations..."
make all >> "$LOG_FILE" 2>&1

for items in $(seq $MIN_ITEMS $MAX_ITEMS); do
    # Generate test data
    capacity=$((items * 100))
    data_file="$OUTPUT_DIR/test_${items}.txt"
    
    log "${YELLOW}Generating test data: $items items, capacity $capacity${NC}"
    ./generate_input $items $capacity 50 100 "$data_file" >> "$LOG_FILE" 2>&1
    
    if [ ! -f "$data_file" ]; then
        log "${RED}ERROR: Failed to generate $data_file${NC}"
        continue
    fi
    
    # Sequential baseline
    run_benchmark "Brute Force" "Sequential" "./knapsack_sequential \"$data_file\"" $items 1 "$data_file"
    
    # OpenMP with different thread counts
    for threads in $(seq $MIN_THREADS $MAX_THREADS); do
        export OMP_NUM_THREADS=$threads
        run_benchmark "Brute Force" "OpenMP" "OMP_NUM_THREADS=$threads ./knapsack_openmp \"$data_file\"" $items $threads "$data_file"
    done
    
    # MPI Master-Slave with different process counts
    for procs in "${MPI_PROCESSES[@]}"; do
        run_benchmark "Brute Force" "MPI_Master_Slave" "mpirun -np $procs --oversubscribe ./knapsack_mpi \"$data_file\"" $items $procs "$data_file"
    done
    
    # MPI Divide & Conquer with different process counts
    for procs in "${MPI_PROCESSES[@]}"; do
        run_benchmark "Brute Force" "MPI_Divide_Conquer" "mpirun -np $procs --oversubscribe ./knapsack_mpi_divide_conquer \"$data_file\"" $items $procs "$data_file"
    done
    
    # Hybrid MPI+OpenMP with different configurations
    for procs in 2 4 6 8; do
        for threads_per_proc in 2 4; do
            total_threads=$((procs * threads_per_proc))
            if [ $total_threads -le $MAX_THREADS ]; then
                export OMP_NUM_THREADS=$threads_per_proc
                run_benchmark "Brute Force" "MPI_OpenMP_Hybrid" "OMP_NUM_THREADS=$threads_per_proc mpirun -np $procs --oversubscribe ./knapsack_mpi_openmp_hybrid \"$data_file\"" $items "${procs}x${threads_per_proc}" "$data_file"
            fi
        done
    done
    
    log "${GREEN}Completed problem size: $items items${NC}"
done

log "${GREEN}Benchmark completed! Results saved to: $CSV_FILE${NC}"
log "Log file: $LOG_FILE"

# Generate summary statistics
log "Generating summary..."
python3 -c "
import pandas as pd
import sys

try:
    df = pd.read_csv('$CSV_FILE')
    print('\n=== BENCHMARK SUMMARY ===')
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

log "${YELLOW}Run 'python3 analyze_benchmark.py' to generate graphs and detailed analysis${NC}"