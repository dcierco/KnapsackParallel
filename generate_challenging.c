#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

// Generator for challenging knapsack instances that force branch-and-bound exploration
#define DEFAULT_OUTPUT_FILENAME "knapsack_challenging.txt"

int main(int argc, char *argv[]) {
    int num_items = 5000;
    int capacity = 150000;
    const char *output_filename = DEFAULT_OUTPUT_FILENAME;

    if (argc > 1) {
        num_items = atoi(argv[1]);
    }
    if (argc > 2) {
        capacity = atoi(argv[2]);
    }
    if (argc > 3) {
        output_filename = argv[3];
    }

    if (num_items <= 0 || capacity <= 0) {
        fprintf(stderr, "Erro: Número de itens e capacidade devem ser positivos.\n");
        fprintf(stderr, "Uso: %s [num_items capacidade [filename]]\n", argv[0]);
        return 1;
    }

    FILE *outfile = fopen(output_filename, "w");
    if (outfile == NULL) {
        perror("Erro ao abrir o arquivo de saída");
        return 1;
    }

    srand(time(NULL));

    fprintf(outfile, "%d %d\n", num_items, capacity);

    // Debug: printf("Gerando instância DESAFIADORA da Mochila: %s\n", output_filename);
    // Debug: printf("Número de Itens (N): %d\n", num_items);
    // Debug: printf("Capacidade da Mochila (W): %d\n", capacity);
    // Debug: printf("Tipo: Instância com múltiplas soluções ótimas próximas (< 30s sequencial)\n");

    // Gera itens com razões valor/peso similares mas não idênticas para forçar exploração moderada
    double base_ratio = 2.0;
    int base_weight = capacity / (num_items / 4); // Peso base para preencher ~25% da capacidade
    
    for (int i = 0; i < num_items; i++) {
        int weight, value;
        
        // Cria diferentes grupos de itens com razões similares mas distinguíveis
        int group = i % 3;
        double ratio_variation = 0.1 * (rand() % 21 - 10) / 10.0; // -10% a +10% variação
        
        switch (group) {
            case 0: // Grupo com razão ~2.0
                weight = base_weight + (rand() % (base_weight / 2)) - (base_weight / 4);
                value = (int)(weight * (base_ratio + ratio_variation));
                break;
                
            case 1: // Grupo com razão ~1.8
                weight = base_weight + (rand() % (base_weight / 2)) - (base_weight / 4);
                value = (int)(weight * (1.8 + ratio_variation));
                break;
                
            default: // Grupo com razão ~2.2
                weight = base_weight + (rand() % (base_weight / 2)) - (base_weight / 4);
                value = (int)(weight * (2.2 + ratio_variation));
                break;
        }
        
        // Garante valores mínimos
        if (weight < 1) weight = 1;
        if (value < 1) value = 1;
        
        // Adiciona alguns itens especiais ocasionalmente
        if (i % 500 == 0) {
            // Item com alta razão mas peso grande
            weight = base_weight * 2;
            value = (int)(weight * 2.5);
        } else if (i % 750 == 0) {
            // Item com baixa razão mas peso pequeno
            weight = base_weight / 2;
            value = (int)(weight * 1.5);
        }
        
        fprintf(outfile, "%d %d\n", value, weight);
    }

    fclose(outfile);
    printf("Generated challenging: %s (%d items, capacity %d)\n", output_filename, num_items, capacity);

    return 0;
}