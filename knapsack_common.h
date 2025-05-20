#ifndef KNAPSACK_COMMON_H
#define KNAPSACK_COMMON_H

#include <stdlib.h> // Para qsort

/**
 * @file knapsack_common.h
 * @brief Definições comuns para o problema da mochila, incluindo a estrutura do item e funções auxiliares.
 */

/**
 * @struct Item
 * @brief Representa um item com peso, valor, a razão valor/peso e seu índice original.
 *
 * Esta estrutura é usada para armazenar as propriedades de cada item
 * que pode ser incluído na mochila. A razão é útil para algoritmos gulosos
 * e para o cálculo de limites no Branch and Bound. O índice original
 * ajuda a rastrear os itens após a ordenação.
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
 * @brief Função de comparação para qsort, usada para ordenar itens pela sua razão valor/peso.
 *
 * Ordena os itens em ordem decrescente de sua razão valor/peso.
 * Se as razões forem iguais, pode-se adicionar um critério secundário (e.g., por valor),
 * mas para o Branch and Bound básico, a razão é o principal.
 *
 * @param a Ponteiro para o primeiro item (do tipo Item).
 * @param b Ponteiro para o segundo item (do tipo Item).
 * @return int Retorna:
 *         - um valor negativo se a razão de 'a' é maior que a de 'b' (para ordem decrescente).
 *         - um valor positivo se a razão de 'a' é menor que a de 'b'.
 *         - zero se as razões são iguais.
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
    // Se as razões são iguais, podemos usar um critério secundário, como o valor (opcional)
    // ou simplesmente considerar iguais para a ordenação primária.
    // if (item_a->valor < item_b->valor) {
    //     return 1;
    // } else if (item_a->valor > item_b->valor) {
    //     return -1;
    // }
    return 0;
}

#endif // KNAPSACK_COMMON_H