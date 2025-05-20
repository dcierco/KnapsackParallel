#include <stdio.h>
#include <stdlib.h>
#include <string.h> // Para memset e memcpy
#include <time.h>   // Para clock_gettime
#include "knapsack_common.h"

#define PRINT_MAX_ITEMS 10 // Número máximo de itens a serem impressos se lidos do arquivo

/**
 * @brief Calcula um limite superior (upper bound) para o valor máximo que pode ser obtido a partir do nível atual.
 *
 * Esta função é usada no algoritmo Branch and Bound para podar ramos da árvore de busca
 * que não podem levar a uma solução melhor que a já encontrada.
 * Ela calcula um valor otimista adicionando itens inteiros enquanto possível e,
 * em seguida, uma fração do próximo item para preencher a capacidade restante.
 *
 * @param nivel O índice do item atual a ser considerado (no array de itens ordenados).
 * @param peso_acumulado O peso total dos itens já incluídos na mochila na subárvore atual.
 * @param valor_acumulado O valor total dos itens já incluídos na mochila na subárvore atual.
 * @param itens Array de todos os itens (já ordenados por razão valor/peso).
 * @param n_itens O número total de itens.
 * @param capacidade_maxima A capacidade máxima da mochila.
 * @return double O limite superior calculado.
 */
static double calcular_limite_superior(int nivel, int peso_acumulado, int valor_acumulado,
                                       const Item itens[], int n_itens, int capacidade_maxima) {
    double limite_superior = (double)valor_acumulado;
    int peso_restante = capacidade_maxima - peso_acumulado;
    int i = nivel;

    // Adiciona itens inteiros enquanto houver capacidade e itens
    while (i < n_itens && itens[i].peso <= peso_restante) {
        peso_restante -= itens[i].peso;
        limite_superior += itens[i].valor;
        i++;
    }

    // Se ainda houver capacidade restante e itens, adiciona uma fração do próximo item
    if (i < n_itens && peso_restante > 0) {
        limite_superior += (double)peso_restante * itens[i].razao;
    }

    return limite_superior;
}

/**
 * @brief Função recursiva principal para o algoritmo Branch and Bound.
 *
 * Explora a árvore de soluções possíveis para o problema da mochila.
 * Utiliza uma estratégia de busca em profundidade, podando ramos que não
 * podem levar a uma solução melhor que a melhor já conhecida (usando `melhor_valor_global`
 * e o `calcular_limite_superior`).
 *
 * @param nivel O índice do item atual sendo considerado para inclusão/exclusão.
 * @param peso_atual O peso acumulado dos itens incluídos até este ponto na busca.
 * @param valor_atual O valor acumulado dos itens incluídos até este ponto na busca.
 * @param itens_ordenados Array de itens, ordenados decrescentemente pela razão valor/peso.
 * @param n_itens O número total de itens.
 * @param capacidade_mochila A capacidade máxima da mochila.
 * @param melhor_valor_global Ponteiro para o maior valor encontrado até o momento.
 *                            Este valor é atualizado sempre que uma solução melhor é encontrada.
 * @param solucao_atual Array booleano (0 ou 1) indicando se cada item (ordenado) está na solução parcial atual.
 * @param melhor_solucao Array booleano (0 ou 1) para armazenar a combinação de itens (ordenados) da melhor solução encontrada.
 */
static void branch_and_bound_recursivo(int nivel, int peso_atual, int valor_atual,
                                       const Item itens_ordenados[], int n_itens, int capacidade_mochila,
                                       int *melhor_valor_global, int solucao_atual[], int melhor_solucao[]) {
    // Caso base: Se o valor atual já é melhor, atualiza o melhor_valor_global
    // Isso também cobre o caso onde chegamos a uma folha com uma solução válida.
    if (valor_atual > *melhor_valor_global) {
        *melhor_valor_global = valor_atual;
        memcpy(melhor_solucao, solucao_atual, n_itens * sizeof(int));
    }

    // Caso base: Todos os itens foram considerados
    if (nivel == n_itens) {
        return;
    }

    // Calcula o limite superior para a subárvore atual
    // (considerando a possibilidade de incluir ou não os itens restantes)
    double limite_sup = calcular_limite_superior(nivel, peso_atual, valor_atual, itens_ordenados, n_itens, capacidade_mochila);

    // Poda: Se o limite superior não é melhor que a melhor solução já encontrada,
    // não há necessidade de explorar este ramo.
    if (limite_sup <= (double)(*melhor_valor_global)) {
        return;
    }


    // --- Ramo 1: Incluir o item do nível atual ---
    if (peso_atual + itens_ordenados[nivel].peso <= capacidade_mochila) {
        solucao_atual[nivel] = 1; // Marca o item como incluído
        branch_and_bound_recursivo(nivel + 1,
                                   peso_atual + itens_ordenados[nivel].peso,
                                   valor_atual + itens_ordenados[nivel].valor,
                                   itens_ordenados, n_itens, capacidade_mochila,
                                   melhor_valor_global, solucao_atual, melhor_solucao);
    }

    // --- Ramo 2: Não incluir o item do nível atual ---
    // Antes de explorar o ramo "não incluir", recalcular o limite superior
    // para essa decisão específica. Se o item atual é crucial para um bom limite,
    // não incluí-lo pode tornar o limite para o resto dos itens (nivel+1 em diante)
    // pior, permitindo uma poda mais cedo.
    double limite_sup_sem_item_atual = calcular_limite_superior(nivel + 1, peso_atual, valor_atual, itens_ordenados, n_itens, capacidade_mochila);

    if (limite_sup_sem_item_atual > (double)(*melhor_valor_global)) {
        solucao_atual[nivel] = 0; // Marca o item como não incluído (backtrack para este ramo)
        branch_and_bound_recursivo(nivel + 1,
                                   peso_atual,
                                   valor_atual,
                                   itens_ordenados, n_itens, capacidade_mochila,
                                   melhor_valor_global, solucao_atual, melhor_solucao);
    }

    // Desfaz a marcação para o backtracking implícito da recursão ao retornar
    // Se solucao_atual fosse global ou passada de forma diferente, um backtrack explícito seria necessário.
    // Como é um array passado por valor (efetivamente, um ponteiro para uma cópia gerenciada pela pilha de recursão ou uma cópia explícita),
    // as modificações são locais para cada chamada de ramo, a menos que solucao_atual seja o mesmo array modificado.
    // Para esta implementação com array passado, é importante zerar ao final do ramo para não poluir o outro.
    // No entanto, a forma como os ramos são chamados (primeiro com item, depois sem) e solucao_atual[nivel] é
    // redefinido para 0 antes do segundo chamado, o estado é corretamente preparado.
    solucao_atual[nivel] = 0; // Garante que está zerado ao sair do nível, para o pai.

}

/**
 * @brief Calcula uma solução inicial gulosa para o problema da mochila.
 *
 * Preenche a mochila com os itens de maior razão valor/peso primeiro.
 * Este valor pode ser usado como um `melhor_valor_global` inicial para
 * o algoritmo Branch and Bound, ajudando a podar mais ramos no início.
 *
 * @param itens_ordenados Array de itens, já ordenados decrescentemente pela razão valor/peso.
 * @param n_itens O número total de itens.
 * @param capacidade_mochila A capacidade máxima da mochila.
 * @param solucao_gulosa Array (de tamanho n_itens) que será preenchido com 0s ou 1s
 *                       indicando os itens selecionados na solução gulosa.
 * @return int O valor total da solução gulosa.
 */
static int calcular_solucao_gulosa_inicial(const Item itens_ordenados[], int n_itens, int capacidade_mochila, int solucao_gulosa[]) {
    int valor_total_guloso = 0;
    int peso_atual_guloso = 0;
    memset(solucao_gulosa, 0, n_itens * sizeof(int));

    for (int i = 0; i < n_itens; i++) {
        if (peso_atual_guloso + itens_ordenados[i].peso <= capacidade_mochila) {
            peso_atual_guloso += itens_ordenados[i].peso;
            valor_total_guloso += itens_ordenados[i].valor;
            solucao_gulosa[i] = 1;
        }
    }
    return valor_total_guloso;
}


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

    printf("Solucionador Sequencial Knapsack (Branch and Bound)\n");
    printf("Lidos %d itens de %s.\n", n_itens, nome_arquivo_entrada);
    printf("Capacidade da mochila: %d\n", capacidade_mochila);

    if (n_itens > 0) {
        printf("Primeiros %d itens (ou todos, se menos) após ordenação por razão V/P:\n", PRINT_MAX_ITEMS);
        for (int i = 0; i < n_itens && i < PRINT_MAX_ITEMS; i++) {
            printf("  Item (ordenado %d, original %d): Peso = %d, Valor = %d, Razao = %.2f\n",
                   i, itens[i].indice_original, itens[i].peso, itens[i].valor, itens[i].razao);
        }
    }

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


    printf("\nCalculando para o dataset de %s...\n", nome_arquivo_entrada);
    printf("Melhor valor inicial (guloso): %d\n", melhor_valor_global);

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // Inicia a busca Branch and Bound
    branch_and_bound_recursivo(0, 0, 0, itens, n_itens, capacidade_mochila,
                               &melhor_valor_global, solucao_atual, melhor_solucao);

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double tempo_decorrido_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                               (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;

    printf("\nValor máximo na Mochila (Dataset: %s) = %d\n", nome_arquivo_entrada, melhor_valor_global);
    printf("Itens selecionados (índices originais, Peso, Valor):\n");
    int peso_total_solucao = 0;
    int valor_verificado_solucao = 0;
    for (int i = 0; i < n_itens; i++) {
        if (melhor_solucao[i] == 1) { // Se o item i (na lista ordenada) foi selecionado
            // printf("  - Item original %d: Peso = %d, Valor = %d (Razao %.2f)\n",
            //        itens[i].indice_original, itens[i].peso, itens[i].valor, itens[i].razao);
            peso_total_solucao += itens[i].peso;
            valor_verificado_solucao += itens[i].valor;
        }
    }
    printf("Peso total da solução: %d (Capacidade: %d)\n", peso_total_solucao, capacidade_mochila);
    printf("Valor total verificado da solução: %d\n", valor_verificado_solucao);
    printf("Tempo de execução sequencial (Branch and Bound): %.3f ms\n", tempo_decorrido_ms);

    free(itens);
    free(solucao_atual);
    free(melhor_solucao);

    return 0;
}
