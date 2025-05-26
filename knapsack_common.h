#ifndef KNAPSACK_COMMON_H
#define KNAPSACK_COMMON_H

#include <stdlib.h> // Para qsort
#include <string.h> // Para memset e memcpy

/**
 * @file knapsack_common.h
 * @brief Definições comuns otimizadas para o problema da mochila 0/1 usando Branch and Bound.
 *
 * Este arquivo contém as estruturas de dados, funções auxiliares e algoritmos compartilhados
 * entre as implementações sequencial e paralela (MPI) do solucionador de mochila.
 *
 * Principais funcionalidades:
 * - Estrutura Item com razão valor/peso
 * - Algoritmo Branch and Bound recursivo otimizado
 * - Cálculo de limites superiores para poda
 * - Solução gulosa inicial para acelerar convergência
 * - Funções de comparação para ordenação de itens
 */

/**
 * @struct Item
 * @brief Estrutura otimizada para representar um item da mochila.
 *
 * Contém todas as informações necessárias para o algoritmo Branch and Bound:
 * - Propriedades físicas (peso, valor)
 * - Razão valor/peso pré-calculada para ordenação eficiente
 * - Índice original para rastreamento após ordenação
 *
 * A razão valor/peso é fundamental para:
 * - Ordenação decrescente dos itens (melhor primeiro)
 * - Cálculo de limites superiores no Branch and Bound
 * - Heurística gulosa para solução inicial
 */
typedef struct {
    int peso;           ///< O peso do item.
    int valor;          ///< O valor do item.
    double razao;       ///< A razão valor/peso do item (valor por unidade de peso).
    int indice_original; ///< O índice original do item antes da ordenação.
} Item;

/**
 * @brief Calcula o máximo entre dois números inteiros.
 *
 * @param a O primeiro número inteiro.
 * @param b O segundo número inteiro.
 * @return int O maior valor entre a e b.
 */
static inline int max(int a, int b) {
    return (a > b) ? a : b;
}

/**
 * @brief Função de comparação otimizada para ordenação de itens por razão valor/peso.
 *
 * Implementa comparação em ordem decrescente de razão valor/peso, com critério
 * secundário por valor para garantir ordenação determinística.
 *
 * Estratégia de ordenação:
 * 1. Prioridade: Maior razão valor/peso (mais eficiente primeiro)
 * 2. Desempate: Maior valor absoluto
 *
 * Esta ordenação é crucial para:
 * - Eficiência do algoritmo guloso
 * - Qualidade dos limites superiores no Branch and Bound
 * - Convergência mais rápida para a solução ótima
 *
 * @param a Ponteiro para o primeiro item (const void* para compatibilidade com qsort)
 * @param b Ponteiro para o segundo item (const void* para compatibilidade com qsort)
 * @return int Valor de comparação:
 *         - Negativo: 'a' tem prioridade maior que 'b'
 *         - Positivo: 'b' tem prioridade maior que 'a'
 *         - Zero: prioridades iguais
 */
static int comparar_itens(const void *a, const void *b) {
    Item *item_a = (Item *)a;
    Item *item_b = (Item *)b;

    // Ordenar em ordem decrescente de razao
    if (item_a->razao < item_b->razao) {
        return 1;
    } else if (item_a->razao > item_b->razao) {
        return -1;
    }
    // Se as razões são iguais, usar valor como critério secundário
    if (item_a->valor < item_b->valor) {
        return 1;
    } else if (item_a->valor > item_b->valor) {
        return -1;
    }
    return 0;
}

/**
 * @brief Calcula limite superior otimizado para poda eficiente no Branch and Bound.
 *
 * Implementa relaxação fracionária do problema da mochila para estimar o melhor
 * valor possível a partir do estado atual. Este limite é usado para:
 * - Podar ramos que não podem melhorar a solução atual
 * - Reduzir drasticamente o espaço de busca
 * - Acelerar convergência para a solução ótima
 *
 * Algoritmo:
 * 1. Adiciona itens completos enquanto couberem na capacidade restante
 * 2. Adiciona fração do próximo item para preencher capacidade restante
 * 3. Retorna valor otimista (limite superior) para este ramo
 *
 * @param nivel Índice do próximo item a ser considerado (0-based)
 * @param peso_acumulado Peso total dos itens já selecionados
 * @param valor_acumulado Valor total dos itens já selecionados
 * @param itens Array ordenado por razão valor/peso (decrescente)
 * @param n_itens Número total de itens no problema
 * @param capacidade_maxima Capacidade máxima da mochila
 * @return double Limite superior (valor máximo teórico possível)
 */
static inline double calcular_limite_superior(int nivel, int peso_acumulado, int valor_acumulado,
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
 * @brief Gera solução gulosa otimizada como ponto de partida para Branch and Bound.
 *
 * Implementa heurística gulosa que seleciona itens em ordem decrescente de razão
 * valor/peso até esgotar a capacidade. Esta solução serve como:
 * - Limite inferior inicial (incumbent) para poda
 * - Baseline para avaliação de qualidade das soluções
 * - Acelera convergência significativamente
 *
 * Características:
 * - Complexidade O(n) para itens já ordenados
 * - Produz soluções de alta qualidade na prática
 * - Essencial para eficiência do Branch and Bound
 *
 * @param itens_ordenados Array ordenado por razão valor/peso (decrescente)
 * @param n_itens Número total de itens disponíveis
 * @param capacidade_mochila Capacidade máxima da mochila
 * @param solucao_gulosa Array de saída (n_itens): 1=selecionado, 0=não selecionado
 *                       Pode ser NULL se apenas o valor for necessário
 * @return int Valor total da solução gulosa (limite inferior)
 */
static inline int calcular_solucao_gulosa_inicial(const Item itens_ordenados[], int n_itens,
                                                  int capacidade_mochila, int solucao_gulosa[]) {
    int valor_total_guloso = 0;
    int peso_atual_guloso = 0;
    if (solucao_gulosa) {
        memset(solucao_gulosa, 0, n_itens * sizeof(int));
    }

    for (int i = 0; i < n_itens; i++) {
        if (peso_atual_guloso + itens_ordenados[i].peso <= capacidade_mochila) {
            peso_atual_guloso += itens_ordenados[i].peso;
            valor_total_guloso += itens_ordenados[i].valor;
            if (solucao_gulosa) {
                solucao_gulosa[i] = 1;
            }
        }
    }
    return valor_total_guloso;
}

/**
 * @brief Algoritmo Branch and Bound recursivo otimizado para mochila 0/1.
 *
 * Implementação eficiente que explora sistematicamente o espaço de soluções usando:
 * - Busca em profundidade (DFS) para menor uso de memória
 * - Poda agressiva baseada em limites superiores
 * - Backtracking automático via recursão
 * - Atualização incremental da melhor solução global
 *
 * Estratégia de poda:
 * 1. Calcula limite superior do ramo atual
 * 2. Se limite ≤ melhor_valor_global, poda o ramo completo
 * 3. Explora ramo "incluir item" (se couber na capacidade)
 * 4. Explora ramo "não incluir item" (se ainda prometedor)
 *
 * Otimizações implementadas:
 * - Poda dupla (incluir/não incluir calculadas separadamente)
 * - Atualização imediata da melhor solução
 * - Backtracking implícito via stack de recursão
 *
 * @param nivel Índice do item atual sendo considerado (0 a n_itens-1)
 * @param peso_atual Peso acumulado na solução parcial atual
 * @param valor_atual Valor acumulado na solução parcial atual
 * @param itens_ordenados Array ordenado por razão valor/peso (decrescente)
 * @param n_itens Número total de itens no problema
 * @param capacidade_mochila Capacidade máxima disponível
 * @param melhor_valor_global Ponteiro para melhor valor encontrado (incumbent)
 * @param solucao_atual Array de trabalho para solução sendo construída
 * @param melhor_solucao Array de saída com a melhor solução encontrada
 */
static inline void branch_and_bound_recursivo(int nivel, int peso_atual, int valor_atual,
                                              const Item itens_ordenados[], int n_itens, int capacidade_mochila,
                                              int *melhor_valor_global, int solucao_atual[], int melhor_solucao[]) {
    // Atualiza a melhor solução se o valor atual é melhor
    if (valor_atual > *melhor_valor_global) {
        *melhor_valor_global = valor_atual;
        memcpy(melhor_solucao, solucao_atual, n_itens * sizeof(int));
    }

    // Caso base: Todos os itens foram considerados
    if (nivel == n_itens) {
        return;
    }

    // Calcula o limite superior para poda
    double limite_sup = calcular_limite_superior(nivel, peso_atual, valor_atual,
                                                 itens_ordenados, n_itens, capacidade_mochila);

    // Poda: Se o limite superior não é melhor que a melhor solução já encontrada
    if (limite_sup <= (double)(*melhor_valor_global)) {
        return;
    }

    // Ramo 1: Incluir o item atual
    if (peso_atual + itens_ordenados[nivel].peso <= capacidade_mochila) {
        solucao_atual[nivel] = 1;
        branch_and_bound_recursivo(nivel + 1,
                                   peso_atual + itens_ordenados[nivel].peso,
                                   valor_atual + itens_ordenados[nivel].valor,
                                   itens_ordenados, n_itens, capacidade_mochila,
                                   melhor_valor_global, solucao_atual, melhor_solucao);
    }

    // Ramo 2: Não incluir o item atual
    double limite_sup_sem_item = calcular_limite_superior(nivel + 1, peso_atual, valor_atual,
                                                          itens_ordenados, n_itens, capacidade_mochila);

    if (limite_sup_sem_item > (double)(*melhor_valor_global)) {
        solucao_atual[nivel] = 0;
        branch_and_bound_recursivo(nivel + 1, peso_atual, valor_atual,
                                   itens_ordenados, n_itens, capacidade_mochila,
                                   melhor_valor_global, solucao_atual, melhor_solucao);
    }

    // Limpa a marcação para backtracking
    solucao_atual[nivel] = 0;
}

#endif // KNAPSACK_COMMON_H
