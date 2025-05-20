```markdown
# Solucionador do Problema da Mochila (Knapsack) com MPI

Este projeto implementa e compara duas abordagens para resolver o problema da mochila 0/1:

1.  **Sequencial**: Utiliza um algoritmo Branch and Bound.
2.  **Paralelo com MPI**: Utiliza um algoritmo Branch and Bound paralelizado com o padrão Mestre-Escravo.

O objetivo é observar o ganho de desempenho (speedup) obtido com a paralelização usando MPI em relação à versão sequencial.

## Estrutura do Diretório

```
MPI/
├── knapsack_common.h       # Definições comuns (struct Item, comparador)
├── knapsack_sequential.c   # Implementação sequencial (Branch and Bound)
├── knapsack_mpi.c          # Implementação paralela com MPI (Branch and Bound Mestre-Escravo)
├── generate_input.c        # Programa para gerar arquivos de dados de entrada
├── Makefile                # Makefile para compilação e execução
├── knapsack_data.txt       # Arquivo de dados de exemplo (gerado)
└── README.md               # Este arquivo
```

## Pré-requisitos

*   Um compilador C (como GCC).
*   Uma implementação de MPI (como OpenMPI ou MPICH) e o compilador `mpicc`.

## Compilação

Para compilar todos os os programas (gerador de entrada, solucionador sequencial e solucionador MPI), navegue até o diretório `MPI/` e execute:

```bash
make all
```

Ou simplesmente:

```bash
make
```

Isso irá gerar os seguintes executáveis:
*   `generate_input`
*   `knapsack_sequential`
*   `knapsack_mpi`

## Geração de Dados de Entrada

Antes de executar os solucionadores, você precisa gerar um arquivo de dados. O `Makefile` facilita isso:

```bash
make generate_data
```

Isso executará o programa `generate_input` com os parâmetros padrão (1000 itens, capacidade 20000) e criará/sobrescreverá o arquivo `knapsack_data.txt`.

Para gerar dados com parâmetros diferentes (número de itens, capacidade da mochila, peso máximo do item, valor máximo do item):

```bash
make generate_data NUM_ITEMS=N CAPACIDADE=W MAX_PESO=P MAX_VALOR=V
```

Substitua `N`, `W`, `P`, `V` pelos valores desejados. Por exemplo:

```bash
make generate_data NUM_ITEMS=2000 CAPACIDADE=40000 MAX_PESO=700 MAX_VALOR=1000
```

O arquivo de dados `knapsack_data.txt` terá o seguinte formato:
*   Linha 1: `<numero_de_itens> <capacidade_da_mochila>`
*   Linhas subsequentes (uma para cada item): `<valor_do_item> <peso_do_item>`

## Execução dos Solucionadores

Certifique-se de que o arquivo `knapsack_data.txt` existe (use `make generate_data` se necessário).

### Solucionador Sequencial

Para executar a versão sequencial:

```bash
make run_sequential
```

O programa lerá de `knapsack_data.txt`, resolverá o problema e imprimirá o valor máximo, os itens selecionados e o tempo de execução.

### Solucionador Paralelo com MPI

Para executar a versão paralela com MPI (usando 4 processos por padrão):

```bash
make run_mpi
```

Para especificar o número de processos (substitua `X` pelo número desejado, e.g., 2, 8):

```bash
make run_mpi NP=X
```

O mestre (rank 0) lerá de `knapsack_data.txt`, distribuirá o trabalho e, ao final, imprimirá o valor máximo e o tempo de execução medido pelo mestre.

## Limpeza

Para remover os executáveis compilados e o arquivo de dados `knapsack_data.txt`:

```bash
make clean
```

## Observando o Desempenho

Ao executar as versões sequencial e MPI (com diferentes números de processos) no *mesmo* arquivo `knapsack_data.txt`, você pode comparar os tempos de execução para calcular o speedup.

**Exemplo de fluxo de teste:**
1.  `make clean` (para um início limpo)
2.  `make generate_data NUM_ITEMS=1500 CAPACIDADE=30000` (gera um conjunto de dados específico)
3.  `make` (compila tudo)
4.  `make run_sequential` (anote o tempo)
5.  `make run_mpi NP=2` (anote o tempo)
6.  `make run_mpi NP=4` (anote o tempo)
7.  `make run_mpi NP=8` (anote o tempo)

Compare os tempos para analisar a eficiência da paralelização.

## Detalhes da Implementação Branch and Bound

*   Os itens são inicialmente ordenados pela razão valor/peso em ordem decrescente.
*   A implementação sequencial usa uma busca em profundidade recursiva.
*   Uma solução gulosa inicial é calculada para fornecer um bom limite inferior inicial (`melhor_valor_global`).
*   A função `calcular_limite_superior` estima o valor máximo otimista de um nó na árvore de busca para permitir a poda.
*   A versão MPI usa um padrão Mestre-Escravo:
    *   O Mestre distribui tarefas iniciais (subproblemas da árvore BnB) para os Escravos.
    *   Os Escravos exploram seus subproblemas atribuídos.
    *   Se um Escravo encontra uma solução melhor que a melhor global conhecida (que o Mestre atualiza e retransmite), ele informa o Mestre.
    *   O Mestre coordena o processo e detecta a terminação.
