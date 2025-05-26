#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Default parameters for generation - Much larger and harder instances for MPI showcase
#define DEFAULT_NUM_ITEMS 50000
#define DEFAULT_CAPACITY 1000000
#define DEFAULT_MAX_ITEM_WEIGHT 2000
#define DEFAULT_MAX_ITEM_VALUE 3000
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

    // Debug: printf("Gerando arquivo de entrada da Mochila: %s\n", output_filename);
    // Debug: printf("Número de Itens (N): %d\n", num_items);
    // Debug: printf("Capacidade da Mochila (W): %d\n", capacity);
    // Debug: printf("Peso Máximo do Item: %d\n", max_weight);
    // Debug: printf("Valor Máximo do Item: %d\n", max_value);

    // Generate challenging knapsack instance with varied value/weight ratios
    // This creates a mix of items that will force branch-and-bound exploration
    for (int i = 0; i < num_items; i++) {
        int weight, value;
        
        // Create different types of items to make the problem harder
        int item_type = i % 4;
        switch (item_type) {
            case 0: // High value, high weight items
                weight = (rand() % (max_weight / 2)) + (max_weight / 2);
                value = (rand() % (max_value / 2)) + (max_value / 2);
                break;
            case 1: // Low value, low weight items
                weight = (rand() % (max_weight / 3)) + 1;
                value = (rand() % (max_value / 3)) + 1;
                break;
            case 2: // Medium value, varying weight
                weight = (rand() % max_weight) + 1;
                value = (rand() % (max_value / 2)) + (max_value / 4);
                break;
            default: // Random items
                weight = (rand() % max_weight) + 1;
                value = (rand() % max_value) + 1;
                break;
        }
        
        // Add some noise to make similar value/weight ratios
        if (i % 10 == 0) {
            // Create some items with very similar value/weight ratios to force more exploration
            double target_ratio = 1.5 + (rand() % 100) / 100.0;
            value = (int)(weight * target_ratio);
            if (value > max_value) value = max_value;
            if (value < 1) value = 1;
        }
        
        fprintf(outfile, "%d %d\n", value, weight);
    }

    fclose(outfile);
    printf("Generated: %s (%d items, capacity %d)\n", output_filename, num_items, capacity);

    return 0;
}
