#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "knapsack_common.h"


/**
 * @brief Ponto de entrada principal para o solucionador sequencial do problema da mochila usando Branch and Bound.
 *
 * Lê a definição do problema de um arquivo, ordena os itens, executa o
 * algoritmo Branch and Bound e imprime a solução ótima encontrada e o tempo de execução.
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
    const char *nome_arquivo_entrada = argv[1];
    FILE *arquivo_entrada = fopen(nome_arquivo_entrada, "r");
    if (arquivo_entrada == NULL) {
        perror("Erro ao abrir o arquivo de entrada");
        return 1;
    }

    int n_itens;
    int capacidade_mochila;

    if (fscanf(arquivo_entrada, "%d %d", &n_itens, &capacidade_mochila) != 2) {
        fprintf(stderr, "Erro ao ler N e W do arquivo de entrada.\n");
        fclose(arquivo_entrada);
        return 1;
    }

    if (n_itens <= 0 || capacidade_mochila < 0) {
        fprintf(stderr, "Número de itens deve ser positivo e capacidade não-negativa. Lidos N=%d, W=%d\n", n_itens, capacidade_mochila);
        fclose(arquivo_entrada);
        return 1;
    }

    Item *itens = (Item *)malloc(n_itens * sizeof(Item));
    if (itens == NULL) {
        perror("Falha ao alocar memória para os itens");
        fclose(arquivo_entrada);
        return 1;
    }

    for (int i = 0; i < n_itens; i++) {
        if (fscanf(arquivo_entrada, "%d %d", &itens[i].valor, &itens[i].peso) != 2) { // Ordem: valor peso no arquivo
            fprintf(stderr, "Erro ao ler item %d do arquivo de entrada.\n", i);
            free(itens);
            fclose(arquivo_entrada);
            return 1;
        }
        itens[i].indice_original = i;
        if (itens[i].peso > 0) {
            itens[i].razao = (double)itens[i].valor / itens[i].peso;
        } else { // Evita divisão por zero; itens com peso 0 e valor > 0 são ideais
            itens[i].razao = (itens[i].valor > 0) ? __DBL_MAX__ : 0.0;
        }
    }
    fclose(arquivo_entrada);

    // Ordena os itens pela razão valor/peso em ordem decrescente
    qsort(itens, n_itens, sizeof(Item), comparar_itens);

    // Mensagem de início (comentar para testes de performance limpos)
    // printf("Solucionador Sequencial: %d itens, capacidade %d\n", n_itens, capacidade_mochila);

    int melhor_valor_global = 0;
    int *solucao_atual = (int *)malloc(n_itens * sizeof(int));
    int *melhor_solucao = (int *)malloc(n_itens * sizeof(int));

    if (solucao_atual == NULL || melhor_solucao == NULL) {
        perror("Falha ao alocar memória para arrays de solução");
        free(itens);
        if(solucao_atual) free(solucao_atual);
        if(melhor_solucao) free(melhor_solucao);
        return 1;
    }
    memset(solucao_atual, 0, n_itens * sizeof(int));
    memset(melhor_solucao, 0, n_itens * sizeof(int));

    // Calcula uma solução gulosa inicial para definir um `melhor_valor_global` inicial
    // e potencialmente podar mais ramos cedo.
    melhor_valor_global = calcular_solucao_gulosa_inicial(itens, n_itens, capacidade_mochila, melhor_solucao);

    // Debug: printf("Melhor valor inicial (guloso): %d\n", melhor_valor_global);

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Inicia a busca Branch and Bound usando a função compartilhada
    branch_and_bound_recursivo(0, 0, 0, itens, n_itens, capacidade_mochila,
                               &melhor_valor_global, solucao_atual, melhor_solucao);

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double tempo_decorrido_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                               (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;

    // Calcula estatísticas da solução
    int peso_total_solucao = 0;
    int valor_verificado_solucao = 0;
    int itens_selecionados = 0;
    for (int i = 0; i < n_itens; i++) {
        if (melhor_solucao[i] == 1) {
            peso_total_solucao += itens[i].peso;
            valor_verificado_solucao += itens[i].valor;
            itens_selecionados++;
        }
    }

    // Resultados essenciais
    printf("SEQUENTIAL: Valor=%d, Itens=%d/%d, Peso=%d/%d, Tempo=%.3fms\n",
           melhor_valor_global, itens_selecionados, n_itens,
           peso_total_solucao, capacidade_mochila, tempo_decorrido_ms);

    free(itens);
    free(solucao_atual);
    free(melhor_solucao);

    return 0;
}
