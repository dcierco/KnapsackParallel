#include <stdio.h>
#include <time.h>
#include "knapsack_common.h"


/**
 * @brief Ponto de entrada principal para o solucionador sequencial do problema da mochila usando Força Bruta.
 *
 * Lê a definição do problema de um arquivo e executa o
 * algoritmo de força bruta e imprime a solução ótima encontrada e o tempo de execução.
 *
 * @param argc Número de argumentos da linha de comando.
 * @param argv Array de strings dos argumentos da linha de comando. Espera-se que argv[1]
 *             seja o nome do arquivo de entrada.
 * @return int 0 se sucesso, 1 em caso de erro.
 */

/**
 * @brief Algoritmo de força bruta recursivo para o problema da mochila 0/1.
 *
 * Explora todas as combinações possíveis de itens para encontrar a solução ótima.
 *
 * @param nivel Índice do item atual sendo considerado (0 a n_itens-1)
 * @param peso_atual Peso acumulado na solução parcial atual
 * @param valor_atual Valor acumulado na solução parcial atual
 * @param itens Array de itens disponíveis
 * @param n_itens Número total de itens no problema
 * @param capacidade_mochila Capacidade máxima disponível
 * @param melhor_valor_global Ponteiro para melhor valor encontrado
 * @param solucao_atual Array de trabalho para solução sendo construída
 * @param melhor_solucao Array de saída com a melhor solução encontrada
 */
static void brute_force_recursivo(int nivel, int peso_atual, int valor_atual,
                                 const Item itens[], int n_itens, int capacidade_mochila,
                                 int *melhor_valor_global, int solucao_atual[], int melhor_solucao[],
                                 long long *nos_explorados) {
    (*nos_explorados)++;
    
    // Caso base: Todos os itens foram considerados
    if (nivel == n_itens) {
        // Atualiza a melhor solução se o valor atual é melhor
        if (valor_atual > *melhor_valor_global) {
            *melhor_valor_global = valor_atual;
            memcpy(melhor_solucao, solucao_atual, n_itens * sizeof(int));
        }
        return;
    }

    // Ramo 1: Não incluir o item atual
    solucao_atual[nivel] = 0;
    brute_force_recursivo(nivel + 1, peso_atual, valor_atual,
                         itens, n_itens, capacidade_mochila,
                         melhor_valor_global, solucao_atual, melhor_solucao,
                         nos_explorados);

    // Ramo 2: Incluir o item atual (se couber na capacidade)
    if (peso_atual + itens[nivel].peso <= capacidade_mochila) {
        solucao_atual[nivel] = 1;
        brute_force_recursivo(nivel + 1,
                             peso_atual + itens[nivel].peso,
                             valor_atual + itens[nivel].valor,
                             itens, n_itens, capacidade_mochila,
                             melhor_valor_global, solucao_atual, melhor_solucao,
                             nos_explorados);
    }

    // Limpa a marcação para backtracking
    solucao_atual[nivel] = 0;
}
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
    long long nos_explorados = 0;

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Inicia a busca por força bruta completa - sem limites
    brute_force_recursivo(0, 0, 0, problem->itens, problem->n_itens, problem->capacidade,
                         &melhor_valor_global, problem->solucao_atual, problem->melhor_solucao,
                         &nos_explorados);

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double tempo_decorrido_ms = get_elapsed_time_ms(start_time, end_time);

    // Calcula e imprime estatísticas usando funções compartilhadas
    SolutionStats stats = calculate_solution_stats(problem->itens, problem->melhor_solucao, 
                                                   problem->n_itens, tempo_decorrido_ms, nos_explorados);
    print_solution_stats("SEQUENTIAL", &stats, problem->n_itens, problem->capacidade);

    free_knapsack_problem(problem);

    return 0;
}
