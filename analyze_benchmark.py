#!/usr/bin/env python3
"""
Comprehensive Knapsack Benchmark Analysis Tool
Generates weak and strong scalability graphs with dual Y-axes (speedup and efficiency)
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns
from pathlib import Path
import sys
import warnings
warnings.filterwarnings('ignore')

# Configuration
plt.style.use('seaborn-v0_8')
sns.set_palette("husl")

class BenchmarkAnalyzer:
    def __init__(self, csv_path="benchmark_results/benchmark_data.csv"):
        self.csv_path = csv_path
        self.output_dir = Path("benchmark_results/graphs")
        self.output_dir.mkdir(exist_ok=True)
        self.df = None
        self.load_data()
    
    def load_data(self):
        """Load and preprocess benchmark data"""
        try:
            self.df = pd.read_csv(self.csv_path)
            print(f"Loaded {len(self.df)} benchmark results")
            
            # Calculate speedup and efficiency
            self.calculate_metrics()
            
            # Filter out timeout cases for main analysis
            self.df_success = self.df[self.df['timeout_occurred'] == False].copy()
            print(f"Successful tests: {len(self.df_success)}")
            
        except FileNotFoundError:
            print(f"Error: Benchmark data file not found: {self.csv_path}")
            print("Please run './run_comprehensive_benchmark.sh' first")
            sys.exit(1)
        except Exception as e:
            print(f"Error loading data: {e}")
            sys.exit(1)
    
    def calculate_metrics(self):
        """Calculate speedup and efficiency metrics"""
        # Get sequential baseline for each problem size
        sequential_times = {}
        seq_data = self.df[(self.df['implementation'] == 'Sequential') & 
                          (self.df['timeout_occurred'] == False)]
        
        for _, row in seq_data.iterrows():
            sequential_times[row['problem_size']] = row['execution_time_ms']
        
        # Calculate speedup and efficiency
        def calc_speedup_efficiency(row):
            if row['timeout_occurred']:
                return 0, 0
            
            seq_time = sequential_times.get(row['problem_size'])
            if seq_time is None:
                return 0, 0
            
            speedup = seq_time / row['execution_time_ms']
            
            # Extract number of cores/processes
            proc_threads = row['processes_threads']
            if 'x' in str(proc_threads):  # Hybrid format like "2x4"
                parts = str(proc_threads).split('x')
                cores = int(parts[0]) * int(parts[1])
            else:
                cores = int(proc_threads)
            
            efficiency = speedup / cores if cores > 0 else 0
            
            return speedup, efficiency
        
        self.df[['speedup', 'efficiency']] = self.df.apply(
            lambda row: pd.Series(calc_speedup_efficiency(row)), axis=1)
    
    def create_strong_scalability_graph(self, problem_size=34):
        """Create strong scalability graph for a specific problem size"""
        data = self.df_success[self.df_success['problem_size'] == problem_size].copy()
        
        if len(data) == 0:
            print(f"No data available for problem size {problem_size}")
            return
        
        # Create figure with dual y-axes
        fig, ax1 = plt.subplots(figsize=(14, 8))
        ax2 = ax1.twinx()
        
        implementations = data['implementation'].unique()
        x_pos = np.arange(len(implementations))
        width = 0.35
        
        # Colors for different metrics
        speedup_color = '#2E86AB'
        efficiency_color = '#A23B72'
        ideal_color = '#F18F01'
        
        # Prepare data for plotting
        speedups = []
        efficiencies = []
        thread_counts = []
        impl_labels = []
        
        for impl in implementations:
            impl_data = data[data['implementation'] == impl]
            if len(impl_data) > 0:
                # Get data for different thread/process counts
                threads = []
                impl_speedups = []
                impl_efficiencies = []
                
                for _, row in impl_data.iterrows():
                    proc_threads = row['processes_threads']
                    if 'x' in str(proc_threads):
                        parts = str(proc_threads).split('x')
                        cores = int(parts[0]) * int(parts[1])
                        label = f"{impl}\\n{proc_threads}"
                    else:
                        cores = int(proc_threads)
                        label = f"{impl}\\n{cores}T" if impl == "OpenMP" else f"{impl}\\n{cores}P"
                    
                    threads.append(cores)
                    impl_speedups.append(row['speedup'])
                    impl_efficiencies.append(row['efficiency'])
                    impl_labels.append(label)
                
                speedups.extend(impl_speedups)
                efficiencies.extend(impl_efficiencies)
                thread_counts.extend(threads)
        
        if not speedups:
            print(f"No valid data for problem size {problem_size}")
            return
        
        # Sort by thread count
        sorted_data = sorted(zip(thread_counts, speedups, efficiencies, impl_labels))
        thread_counts, speedups, efficiencies, impl_labels = zip(*sorted_data)
        
        x_pos = np.arange(len(speedups))
        
        # Plot speedup bars
        bars1 = ax1.bar(x_pos - width/2, speedups, width, 
                       label='Speedup', color=speedup_color, alpha=0.8)
        
        # Plot ideal speedup line
        ideal_speedup = thread_counts
        ax1.plot(x_pos, ideal_speedup, 'o-', color=ideal_color, 
                linewidth=2, markersize=6, label='Ideal Speedup')
        
        # Plot efficiency line on second y-axis
        line2 = ax2.plot(x_pos, efficiencies, 's-', color=efficiency_color, 
                        linewidth=2, markersize=6, label='Efficiency')
        
        # Plot ideal efficiency line (100%)
        ax2.axhline(y=1.0, color=efficiency_color, linestyle='--', 
                   alpha=0.7, label='Ideal Efficiency (100%)')
        
        # Formatting
        ax1.set_xlabel('Implementation and Thread/Process Count', fontsize=12, fontweight='bold')
        ax1.set_ylabel('Speedup', fontsize=12, fontweight='bold', color=speedup_color)
        ax2.set_ylabel('Efficiency', fontsize=12, fontweight='bold', color=efficiency_color)
        
        ax1.set_title(f'Strong Scalability Analysis\\nProblem Size: {problem_size} items', 
                     fontsize=14, fontweight='bold', pad=20)
        
        # Set x-axis labels
        ax1.set_xticks(x_pos)
        ax1.set_xticklabels(impl_labels, rotation=45, ha='right', fontsize=10)
        
        # Color y-axis labels
        ax1.tick_params(axis='y', labelcolor=speedup_color)
        ax2.tick_params(axis='y', labelcolor=efficiency_color)
        
        # Set y-axis limits
        max_speedup = max(max(speedups), max(ideal_speedup)) * 1.1
        ax1.set_ylim(0, max_speedup)
        ax2.set_ylim(0, 1.2)
        
        # Legends
        ax1.legend(loc='upper left', frameon=True, fancybox=True, shadow=True)
        ax2.legend(loc='upper right', frameon=True, fancybox=True, shadow=True)
        
        # Add value labels on bars
        for bar, speedup, efficiency in zip(bars1, speedups, efficiencies):
            height = bar.get_height()
            ax1.text(bar.get_x() + bar.get_width()/2., height + max_speedup*0.01,
                    f'{speedup:.1f}x', ha='center', va='bottom', fontsize=8, fontweight='bold')
        
        # Grid
        ax1.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(self.output_dir / f'strong_scalability_{problem_size}_items.png', 
                   dpi=300, bbox_inches='tight')
        plt.savefig(self.output_dir / f'strong_scalability_{problem_size}_items.pdf', 
                   bbox_inches='tight')
        plt.show()
        
        print(f"Strong scalability graph saved for {problem_size} items")
    
    def create_weak_scalability_graph(self):
        """Create weak scalability graph showing how performance scales with problem size"""
        # Focus on a few key implementations and thread counts
        target_configs = [
            ('Sequential', 1),
            ('OpenMP', 4),
            ('OpenMP', 8),
            ('MPI_Master_Slave', 4),
            ('MPI_Divide_Conquer', 4),
            ('MPI_OpenMP_Hybrid', '2x4')
        ]
        
        fig, ax1 = plt.subplots(figsize=(14, 8))
        ax2 = ax1.twinx()
        
        colors = plt.cm.Set1(np.linspace(0, 1, len(target_configs)))
        
        problem_sizes = sorted(self.df_success['problem_size'].unique())
        
        for i, (impl, threads) in enumerate(target_configs):
            data = self.df_success[
                (self.df_success['implementation'] == impl) & 
                (self.df_success['processes_threads'] == threads)
            ].copy()
            
            if len(data) == 0:
                continue
            
            # Sort by problem size
            data = data.sort_values('problem_size')
            
            sizes = data['problem_size'].values
            times = data['execution_time_ms'].values / 1000  # Convert to seconds
            speedups = data['speedup'].values
            efficiencies = data['efficiency'].values
            
            if len(sizes) > 1:
                # Plot execution time
                label = f"{impl} ({threads}T)" if impl == "OpenMP" else f"{impl} ({threads}P)"
                if 'x' in str(threads):
                    label = f"{impl} ({threads})"
                
                ax1.plot(sizes, times, 'o-', color=colors[i], linewidth=2, 
                        markersize=6, label=label)
                
                # Plot efficiency on second axis
                ax2.plot(sizes, efficiencies, 's--', color=colors[i], 
                        linewidth=1.5, markersize=4, alpha=0.7)
        
        # Formatting
        ax1.set_xlabel('Problem Size (Number of Items)', fontsize=12, fontweight='bold')
        ax1.set_ylabel('Execution Time (seconds)', fontsize=12, fontweight='bold', color='darkblue')
        ax2.set_ylabel('Efficiency', fontsize=12, fontweight='bold', color='darkred')
        
        ax1.set_title('Weak Scalability Analysis\\nExecution Time and Efficiency vs Problem Size', 
                     fontsize=14, fontweight='bold', pad=20)
        
        # Logarithmic scale for y-axis (time)
        ax1.set_yscale('log')
        
        # Color y-axis labels
        ax1.tick_params(axis='y', labelcolor='darkblue')
        ax2.tick_params(axis='y', labelcolor='darkred')
        
        # Set efficiency limits
        ax2.set_ylim(0, 1.2)
        ax2.axhline(y=1.0, color='darkred', linestyle='--', alpha=0.5, label='Ideal Efficiency')
        
        # Legends
        ax1.legend(loc='upper left', frameon=True, fancybox=True, shadow=True, title='Execution Time')
        ax2.legend(loc='upper right', frameon=True, fancybox=True, shadow=True, title='Efficiency')
        
        # Grid
        ax1.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(self.output_dir / 'weak_scalability_analysis.png', dpi=300, bbox_inches='tight')
        plt.savefig(self.output_dir / 'weak_scalability_analysis.pdf', bbox_inches='tight')
        plt.show()
        
        print("Weak scalability graph saved")
    
    def create_performance_heatmap(self):
        """Create heatmap showing performance across problem sizes and implementations"""
        # Pivot data for heatmap
        pivot_data = self.df_success.pivot_table(
            values='speedup', 
            index='problem_size', 
            columns=['implementation', 'processes_threads'],
            aggfunc='mean'
        )
        
        plt.figure(figsize=(16, 10))
        sns.heatmap(pivot_data, annot=True, fmt='.1f', cmap='YlOrRd', 
                   cbar_kws={'label': 'Speedup'})
        plt.title('Performance Heatmap: Speedup Across Problem Sizes and Implementations', 
                 fontsize=14, fontweight='bold', pad=20)
        plt.xlabel('Implementation and Thread/Process Count', fontsize=12, fontweight='bold')
        plt.ylabel('Problem Size (Items)', fontsize=12, fontweight='bold')
        plt.xticks(rotation=45, ha='right')
        plt.tight_layout()
        plt.savefig(self.output_dir / 'performance_heatmap.png', dpi=300, bbox_inches='tight')
        plt.savefig(self.output_dir / 'performance_heatmap.pdf', bbox_inches='tight')
        plt.show()
        
        print("Performance heatmap saved")
    
    def generate_summary_report(self):
        """Generate comprehensive summary report"""
        report_path = self.output_dir / 'benchmark_summary.txt'
        
        with open(report_path, 'w') as f:
            f.write("COMPREHENSIVE KNAPSACK BENCHMARK ANALYSIS REPORT\\n")
            f.write("=" * 50 + "\\n\\n")
            
            # Overall statistics
            f.write("OVERALL STATISTICS:\\n")
            f.write(f"Total tests executed: {len(self.df)}\\n")
            f.write(f"Successful tests: {len(self.df_success)}\\n")
            f.write(f"Timeout tests: {len(self.df[self.df['timeout_occurred'] == True])}\\n")
            f.write(f"Problem sizes tested: {sorted(self.df['problem_size'].unique())}\\n")
            f.write(f"Implementations tested: {list(self.df['implementation'].unique())}\\n\\n")
            
            # Best speedups
            f.write("BEST SPEEDUPS BY IMPLEMENTATION:\\n")
            for impl in self.df_success['implementation'].unique():
                impl_data = self.df_success[self.df_success['implementation'] == impl]
                best_speedup = impl_data['speedup'].max()
                best_row = impl_data[impl_data['speedup'] == best_speedup].iloc[0]
                f.write(f"{impl}: {best_speedup:.2f}x (Problem size: {best_row['problem_size']}, "
                       f"Threads/Processes: {best_row['processes_threads']})\\n")
            f.write("\\n")
            
            # Efficiency analysis
            f.write("EFFICIENCY ANALYSIS:\\n")
            for impl in self.df_success['implementation'].unique():
                impl_data = self.df_success[self.df_success['implementation'] == impl]
                avg_efficiency = impl_data['efficiency'].mean()
                max_efficiency = impl_data['efficiency'].max()
                f.write(f"{impl}: Average efficiency: {avg_efficiency:.2f}, "
                       f"Max efficiency: {max_efficiency:.2f}\\n")
            f.write("\\n")
            
            # Timeout analysis
            timeout_data = self.df[self.df['timeout_occurred'] == True]
            if len(timeout_data) > 0:
                f.write("TIMEOUT ANALYSIS:\\n")
                for impl in timeout_data['implementation'].unique():
                    count = len(timeout_data[timeout_data['implementation'] == impl])
                    f.write(f"{impl}: {count} timeouts\\n")
                f.write("\\n")
        
        print(f"Summary report saved to: {report_path}")
    
    def run_complete_analysis(self):
        """Run complete analysis and generate all graphs"""
        print("Starting comprehensive benchmark analysis...")
        
        # Generate all graphs
        self.create_strong_scalability_graph(34)  # 34-item benchmark
        if len(self.df_success[self.df_success['problem_size'] == 30]) > 0:
            self.create_strong_scalability_graph(30)  # Alternative size
        
        self.create_weak_scalability_graph()
        self.create_performance_heatmap()
        self.generate_summary_report()
        
        print(f"\\nAnalysis complete! All graphs and reports saved to: {self.output_dir}")
        print("Generated files:")
        for file in sorted(self.output_dir.glob('*')):
            print(f"  - {file.name}")

def main():
    if len(sys.argv) > 1:
        csv_path = sys.argv[1]
    else:
        csv_path = "benchmark_results/benchmark_data.csv"
    
    analyzer = BenchmarkAnalyzer(csv_path)
    analyzer.run_complete_analysis()

if __name__ == "__main__":
    main()