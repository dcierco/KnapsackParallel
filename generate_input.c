#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Default parameters for generation
#define DEFAULT_NUM_ITEMS 1000
#define DEFAULT_CAPACITY 20000
#define DEFAULT_MAX_ITEM_WEIGHT 500
#define DEFAULT_MAX_ITEM_VALUE 750
#define DEFAULT_OUTPUT_FILENAME "knapsack_data.txt"

int main(int argc, char *argv[]) {
    int num_items = DEFAULT_NUM_ITEMS;
    int capacity = DEFAULT_CAPACITY;
    int max_weight = DEFAULT_MAX_ITEM_WEIGHT;
    int max_value = DEFAULT_MAX_ITEM_VALUE;
    const char *output_filename = DEFAULT_OUTPUT_FILENAME;

    // Basic command-line argument parsing
    if (argc > 1) {
        num_items = atoi(argv[1]);
    }
    if (argc > 2) {
        capacity = atoi(argv[2]);
    }
    if (argc > 3) {
        max_weight = atoi(argv[3]);
        if (max_weight <= 0) max_weight = DEFAULT_MAX_ITEM_WEIGHT;
    }
    if (argc > 4) {
        max_value = atoi(argv[4]);
        if (max_value <= 0) max_value = DEFAULT_MAX_ITEM_VALUE;
    }
    if (argc > 5) {
        output_filename = argv[5];
    }

    if (num_items <=0 || capacity <=0 || max_weight <=0 || max_value <=0) {
        fprintf(stderr, "Erro: Número de itens, capacidade, peso máximo e valor máximo devem ser positivos.\n");
        fprintf(stderr, "Uso: %s [num_items capacidade max_weight max_value [filename]]\n", argv[0]);
        return 1;
    }

    FILE *outfile = fopen(output_filename, "w");
    if (outfile == NULL) {
        perror("Erro ao abrir o arquivo de saída");
        return 1;
    }

    srand(time(NULL)); // Seed random number generator

    // Write N and W to the first line
    fprintf(outfile, "%d %d\n", num_items, capacity);

    printf("Gerando arquivo de entrada da Mochila: %s\n", output_filename);
    printf("Número de Itens (N): %d\n", num_items);
    printf("Capacidade da Mochila (W): %d\n", capacity);
    printf("Peso Máximo do Item: %d\n", max_weight);
    printf("Valor Máximo do Item: %d\n", max_value);

    // Write each item's value and weight
    for (int i = 0; i < num_items; i++) {
        int weight = (rand() % max_weight) + 1; // Weight between 1 and max_weight
        int value = (rand() % max_value) + 1;   // Value between 1 and max_value
        fprintf(outfile, "%d %d\n", value, weight); // <<< MODIFIED LINE HERE: value then weight
    }

    fclose(outfile);
    printf("Gerado com sucesso %s com %d itens.\n", output_filename, num_items);

    return 0;
}
