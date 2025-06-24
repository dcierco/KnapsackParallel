#include <stdio.h>
#include <time.h>
#include <omp.h>
#include "knapsack_common.h"

/**
 * @brief Algoritmo de força bruta sequencial para o problema da mochila 0/1.
 * Usado como função auxiliar para a versão paralela.
 */
static void brute_force_recursivo(int nivel, int peso_atual, int valor_atual,
                                 const Item itens[], int n_itens, int capacidade_mochila,
                                 int *melhor_valor_global, int solucao_atual[], int melhor_solucao[]) {
    // Caso base: Todos os itens foram considerados
    if (nivel == n_itens) {
        // Atualiza a melhor solução se necessário (com proteção thread-safe)
        #pragma omp critical
        {
            if (valor_atual > *melhor_valor_global) {
                *melhor_valor_global = valor_atual;
                memcpy(melhor_solucao, solucao_atual, n_itens * sizeof(int));
            }
        }
        return;
    }

    // Ramo 1: Não incluir o item atual
    solucao_atual[nivel] = 0;
    brute_force_recursivo(nivel + 1, peso_atual, valor_atual,
                         itens, n_itens, capacidade_mochila,
                         melhor_valor_global, solucao_atual, melhor_solucao);

    // Ramo 2: Incluir o item atual (se couber)
    if (peso_atual + itens[nivel].peso <= capacidade_mochila) {
        solucao_atual[nivel] = 1;
        brute_force_recursivo(nivel + 1,
                             peso_atual + itens[nivel].peso,
                             valor_atual + itens[nivel].valor,
                             itens, n_itens, capacidade_mochila,
                             melhor_valor_global, solucao_atual, melhor_solucao);
    }

    // Limpa a marcação para backtracking
    solucao_atual[nivel] = 0;
}

/**
 * @brief Ponto de entrada principal para o solucionador OpenMP do problema da mochila usando Força Bruta.
 *
 * Lê a definição do problema de um arquivo e executa o
 * algoritmo de força bruta paralelizado com OpenMP, imprimindo a solução ótima encontrada e o tempo de execução.
 *
 * @param argc Número de argumentos da linha de comando.
 * @param argv Array de strings dos argumentos da linha de comando. Espera-se que argv[1]
 *             seja o nome do arquivo de entrada.
 * @return int 0 se sucesso, 1 em caso de erro.
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <arquivo_de_entrada>\n", argv[0]);
        return 1;
    }

    // Lê e inicializa o problema usando função compartilhada
    KnapsackProblem *problem = read_knapsack_problem(argv[1]);
    if (!problem) {
        return 1;
    }

    int melhor_valor_global = 0;
    int num_threads = omp_get_max_threads();

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Calcula profundidade de paralelização baseada no número de threads
    int parallel_depth = 0;
    int temp_threads = num_threads;
    while (temp_threads > 1) {
        parallel_depth++;
        temp_threads /= 2;
    }
    // Garante profundidade mínima de 2 e máxima baseada no número de itens
    if (parallel_depth < 2) parallel_depth = 2;
    if (parallel_depth > problem->n_itens) parallel_depth = problem->n_itens;
    if (parallel_depth > 6) parallel_depth = 6; // Limita para evitar muitas combinações
    
    int num_combinations = 1 << parallel_depth; // 2^parallel_depth
    
    // Paralelização usando parallel for com trabalho dinâmico
    #pragma omp parallel for schedule(dynamic) shared(melhor_valor_global, problem)
    for (int combination = 0; combination < num_combinations; combination++) {
        // Cada thread terá sua própria cópia da solução para evitar conflitos
        int *solucao_local = (int*)malloc(problem->n_itens * sizeof(int));
        memset(solucao_local, 0, problem->n_itens * sizeof(int));
        
        // Decodifica a combinação em decisões binárias para os primeiros itens
        int peso_acum = 0;
        int valor_acum = 0;
        int valid_combination = 1;
        
        for (int i = 0; i < parallel_depth; i++) {
            int include_item = (combination >> i) & 1;
            solucao_local[i] = include_item;
            
            if (include_item) {
                peso_acum += problem->itens[i].peso;
                valor_acum += problem->itens[i].valor;
                
                // Verifica se a combinação é válida (não excede capacidade)
                if (peso_acum > problem->capacidade) {
                    valid_combination = 0;
                    break;
                }
            }
        }
        
        // Se a combinação é válida, continua a busca recursiva
        if (valid_combination) {
            brute_force_recursivo(parallel_depth, peso_acum, valor_acum,
                                problem->itens, problem->n_itens, problem->capacidade,
                                &melhor_valor_global, solucao_local, problem->melhor_solucao);
        }
        
        free(solucao_local);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double tempo_decorrido_ms = get_elapsed_time_ms(start_time, end_time);

    // Calcula e imprime estatísticas usando funções compartilhadas
    char algorithm_name[32];
    snprintf(algorithm_name, sizeof(algorithm_name), "OPENMP(%d)", num_threads);
    
    SolutionStats stats = calculate_solution_stats(problem->itens, problem->melhor_solucao, 
                                                   problem->n_itens, tempo_decorrido_ms, 0);
    print_solution_stats(algorithm_name, &stats, problem->n_itens, problem->capacidade);

    free_knapsack_problem(problem);

    return 0;
}