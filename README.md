# Implementações Paralelas do Problema da Mochila 0/1

Este projeto implementa diferentes versões paralelas do algoritmo de força bruta para o problema da mochila 0/1.

## Implementações Disponíveis

- **Sequential**: Versão sequencial de força bruta
- **OpenMP**: Paralelização com memória compartilhada usando tasks
- **MPI Master-Slave**: Distribuição de trabalho usando MPI
- **MPI Divide & Conquer**: Estratégia divide-e-conquista com balanceamento de carga
- **MPI+OpenMP Hybrid**: Paralelização híbrida combinando MPI e OpenMP

## Como Compilar

```bash
make all          # Compila todas as versões
make clean        # Remove executáveis e arquivos temporários
```

## Como Executar

### Execução Individual
```bash
# Sequencial
./knapsack_sequential <arquivo_entrada>

# OpenMP (definir número de threads)
export OMP_NUM_THREADS=4
./knapsack_openmp <arquivo_entrada>

# MPI (definir número de processos)
mpirun -np 4 ./knapsack_mpi <arquivo_entrada>
mpirun -np 4 ./knapsack_mpi_divide_conquer <arquivo_entrada>

# Híbrido MPI+OpenMP
export OMP_NUM_THREADS=4
mpirun -np 2 ./knapsack_mpi_openmp_hybrid <arquivo_entrada>
```

### Gerar Dados de Teste
```bash
make generate N=20    # Gera test_20.txt com 20 itens
make generate N=25    # Gera test_25.txt com 25 itens

# Executar implementações específicas:
make run_sequential N=20
make run_mpi N=20 NP=4
make run_openmp N=20
make run_hybrid N=20 NP=2

# Teste rápido com todas as implementações:
make test            # Testa todas com 10 itens
```

## Benchmark Abrangente

### Executar Benchmark
```bash
make comprehensive_benchmark
```

Este comando:
- Gera problemas de 20 a 37 itens
- Testa cada implementação com 2-16 threads/processos
- Timeout de 10 minutos por teste
- Salva resultados em `benchmark_results/`

### Analisar Resultados
```bash
python3 analyze_benchmark.py
```

**Dependências para análise:**
- `pandas`
- `matplotlib`
- `seaborn`
- `numpy`

### Arquivos Gerados pela Análise

- **Gráficos PNG/PDF:**
  - `strong_scalability_{tamanho}_items.png/pdf` - Escalabilidade forte por tamanho
  - `weak_scalability_analysis.png/pdf` - Escalabilidade fraca
  - `performance_heatmap.png/pdf` - Mapa de calor de desempenho

- **Relatórios:**
  - `benchmark_summary.txt` - Resumo completo dos resultados
  - `benchmark_data.csv` - Dados brutos do benchmark

## Formato dos Arquivos de Entrada

```
<número_de_itens> <capacidade_da_mochila>
<valor_1> <peso_1>
<valor_2> <peso_2>
...
<valor_n> <peso_n>
```

## Algoritmo

Todas as implementações usam força bruta com limite de nós explorados para prevenir explosão exponencial. O algoritmo explora todas as combinações possíveis de itens (2^n) respeitando a restrição de capacidade da mochila.

## Estrutura do Projeto

- `knapsack_*.c` - Implementações em C
- `knapsack_common.h` - Funções e estruturas compartilhadas
- `generate_input.c` - Gerador de instâncias de teste
- `run_comprehensive_benchmark.sh` - Script de benchmark
- `analyze_benchmark.py` - Análise e visualização de resultados