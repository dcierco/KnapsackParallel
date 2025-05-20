#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h> // Para offsetof
#include "knapsack_common.h"

#define PRINT_MAX_ITEMS 10

// Constante para o tamanho máximo do array de solução parcial em uma tarefa.
// Isso limita a profundidade que o mestre explora para gerar tarefas iniciais.
// Ou, se a tarefa for toda a solução, este é o N máximo.
// Para simplificar, vamos assumir que n_itens não excederá este valor para
// enviar o array de solução completo como parte da tarefa ou resultado.
#define MAX_N_ITENS_CONST 20000 // Ajuste conforme necessário, baseado nos testes anteriores.

/**
 * @brief Estrutura para definir uma tarefa (subproblema) do Branch and Bound.
 * Contém o estado a partir do qual um escravo deve começar a explorar.
 */
typedef struct {
    int nivel_inicio;        ///< Nível (índice do item) a partir do qual o escravo começa.
    int peso_atual;          ///< Peso acumulado até o nível_inicio.
    int valor_atual;         ///< Valor acumulado até o nível_inicio.
    int solucao_parcial_prefixo[MAX_N_ITENS_CONST]; ///< Decisões (0 ou 1) para itens 0 a nivel_inicio-1.
} BnB_Tarefa;

// Tags MPI
#define TAG_PEDIDO_TAREFA 1
#define TAG_TAREFA_DADOS 2
#define TAG_NOVA_SOLUCAO 3
#define TAG_ATUALIZACAO_MELHOR_VALOR 4
#define TAG_TERMINAR 5
#define TAG_SEM_TAREFAS 6 // Sinaliza que não há mais tarefas no momento.

// Protótipos de funções auxiliares (implementadas abaixo)
static double calcular_limite_superior_mpi(int nivel, int peso_acumulado, int valor_acumulado,
                                           const Item itens[], int n_itens, int capacidade_maxima);

static void escravo_branch_and_bound_recursivo(int nivel, int peso_atual, int valor_atual,
                                               const Item itens_ordenados[], int n_itens, int capacidade_mochila,
                                               int *melhor_valor_local_escravo, int solucao_atual_escravo[],
                                               int rank_escravo, int melhor_solucao_escravo[]);


/**
 * @brief Ponto de entrada principal para o solucionador MPI do problema da mochila usando Branch and Bound.
 */
int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    int rank, num_procs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    if (num_procs < 1) { // Precisa de pelo menos 1 processo (mestre, mesmo que não haja escravos para este modelo)
        if (rank == 0) {
             fprintf(stderr, "Este programa MPI requer pelo menos 1 processo.\n");
        }
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
     if (num_procs < 2 && rank == 0) {
        fprintf(stderr, "Aviso: Executando em modo \"sequencial\" com menos de 2 processos. O Mestre fará todo o trabalho se não houver escravos.\n");
        // O código do mestre pode ser adaptado para rodar sequencialmente se num_procs == 1.
        // Para este exemplo, vamos manter a lógica mestre/escravo e esperar pelo menos 2 para paralelismo real.
    }


    // Criação do tipo MPI para a struct Item
    MPI_Datatype MPI_ITEM_TYPE;
    int blocklengths[4] = {1, 1, 1, 1};
    MPI_Aint displacements[4];
    MPI_Datatype types[4] = {MPI_INT, MPI_INT, MPI_DOUBLE, MPI_INT};

    displacements[0] = offsetof(Item, peso);
    displacements[1] = offsetof(Item, valor);
    displacements[2] = offsetof(Item, razao);
    displacements[3] = offsetof(Item, indice_original);

    MPI_Type_create_struct(4, blocklengths, displacements, types, &MPI_ITEM_TYPE);
    MPI_Type_commit(&MPI_ITEM_TYPE);


    // Variáveis comuns
    int n_itens_global = 0;
    int capacidade_mochila_global = 0;
    Item *itens_ordenados_global = NULL;
    int melhor_valor_global_mpi = 0; // Melhor valor encontrado por qualquer processo

    double tempo_inicio_total, tempo_fim_total;

    if (rank == 0) { // Código do Mestre
        if (argc < 2) {
            fprintf(stderr, "Mestre (rank 0): Uso: %s <arquivo_de_entrada>\n", argv[0]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        const char *nome_arquivo_entrada = argv[1];
        FILE *arquivo_entrada = fopen(nome_arquivo_entrada, "r");
        if (arquivo_entrada == NULL) {
            perror("Mestre: Erro ao abrir o arquivo de entrada");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        if (fscanf(arquivo_entrada, "%d %d", &n_itens_global, &capacidade_mochila_global) != 2) {
             fprintf(stderr, "Mestre: Erro ao ler N e W do arquivo de entrada.\n");
             fclose(arquivo_entrada);
             MPI_Abort(MPI_COMM_WORLD, 1);
        }

        if (n_itens_global <= 0 || capacidade_mochila_global < 0 || n_itens_global > MAX_N_ITENS_CONST) {
            fprintf(stderr, "Mestre: Parâmetros inválidos N=%d (max %d suportado para buffer de tarefa %d), W=%d\\n",
                    n_itens_global, MAX_N_ITENS_CONST, MAX_N_ITENS_CONST, capacidade_mochila_global);
            fclose(arquivo_entrada);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        itens_ordenados_global = (Item *)malloc(n_itens_global * sizeof(Item));
        if (itens_ordenados_global == NULL) {
            perror("Mestre: Falha ao alocar memória para itens_ordenados_global");
            fclose(arquivo_entrada);
            MPI_Abort(MPI_COMM_WORLD,1);
        }
        for (int i = 0; i < n_itens_global; i++) {
            if (fscanf(arquivo_entrada, "%d %d", &itens_ordenados_global[i].valor, &itens_ordenados_global[i].peso) != 2) {
                fprintf(stderr, "Mestre: Erro ao ler item %d do arquivo de entrada.\n", i);
                free(itens_ordenados_global);
                fclose(arquivo_entrada);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            itens_ordenados_global[i].indice_original = i;
            itens_ordenados_global[i].razao = (itens_ordenados_global[i].peso > 0) ?
                                           (double)itens_ordenados_global[i].valor / itens_ordenados_global[i].peso :
                                           (itens_ordenados_global[i].valor > 0 ? __DBL_MAX__ : 0.0);
        }
        fclose(arquivo_entrada);
        qsort(itens_ordenados_global, n_itens_global, sizeof(Item), comparar_itens);

        printf("Mestre (rank %d) iniciado.\n", rank);
        printf("Lidos %d itens de %s. Capacidade da mochila: %d\n", n_itens_global, nome_arquivo_entrada, capacidade_mochila_global);
        printf("Total de processos: %d. Mestre: 1, Escravos: %d\n", num_procs, num_procs - 1);

        // Solução gulosa inicial pelo mestre
        int *solucao_gulosa_temp = (int *)calloc(n_itens_global, sizeof(int)); // Não estritamente necessário para o valor
        if (solucao_gulosa_temp == NULL && n_itens_global > 0) {
            perror("Mestre: Falha ao alocar solucao_gulosa_temp");
            free(itens_ordenados_global);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        int peso_guloso = 0;
        for(int i=0; i<n_itens_global; ++i) {
            if (peso_guloso + itens_ordenados_global[i].peso <= capacidade_mochila_global) {
                peso_guloso += itens_ordenados_global[i].peso;
                melhor_valor_global_mpi += itens_ordenados_global[i].valor;
            }
        }
        if (solucao_gulosa_temp) free(solucao_gulosa_temp);
        printf("Mestre: Melhor valor inicial (guloso): %d\n", melhor_valor_global_mpi);
        tempo_inicio_total = MPI_Wtime();

        // Enviar dados iniciais para escravos (se houver escravos)
        if (num_procs > 1) {
            for (int i = 1; i < num_procs; i++) {
                MPI_Send(&n_itens_global, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
                MPI_Send(&capacidade_mochila_global, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
                MPI_Send(itens_ordenados_global, n_itens_global, MPI_ITEM_TYPE, i, 0, MPI_COMM_WORLD);
                MPI_Send(&melhor_valor_global_mpi, 1, MPI_INT, i, TAG_ATUALIZACAO_MELHOR_VALOR, MPI_COMM_WORLD);
            }
        }

        BnB_Tarefa tarefa;
        int tarefas_distribuidas = 0;
        int num_escravos_ativos = 0;

        // Geração e distribuição de tarefas iniciais simples
        // Tarefa 1: Incluir o primeiro item (se possível)
        if (n_itens_global > 0 && num_procs > 1 && itens_ordenados_global[0].peso <= capacidade_mochila_global) {
            if (tarefas_distribuidas < num_procs -1) {
                tarefa.nivel_inicio = 1;
                tarefa.peso_atual = itens_ordenados_global[0].peso;
                tarefa.valor_atual = itens_ordenados_global[0].valor;
                memset(tarefa.solucao_parcial_prefixo, 0, MAX_N_ITENS_CONST * sizeof(int));
                tarefa.solucao_parcial_prefixo[0] = 1;

                MPI_Send(&tarefa, sizeof(BnB_Tarefa), MPI_BYTE, tarefas_distribuidas + 1, TAG_TAREFA_DADOS, MPI_COMM_WORLD);
                tarefas_distribuidas++;
                num_escravos_ativos++;
            }
        }
        // Tarefa 2: Não incluir o primeiro item
        if (n_itens_global > 0 && num_procs > 1) {
             if (tarefas_distribuidas < num_procs -1) {
                tarefa.nivel_inicio = 1;
                tarefa.peso_atual = 0;
                tarefa.valor_atual = 0;
                memset(tarefa.solucao_parcial_prefixo, 0, MAX_N_ITENS_CONST * sizeof(int));
                tarefa.solucao_parcial_prefixo[0] = 0;

                MPI_Send(&tarefa, sizeof(BnB_Tarefa), MPI_BYTE, tarefas_distribuidas + 1, TAG_TAREFA_DADOS, MPI_COMM_WORLD);
                tarefas_distribuidas++;
                num_escravos_ativos++;
             }
        }

        int melhor_solucao_final[MAX_N_ITENS_CONST];
        if (n_itens_global > 0) memset(melhor_solucao_final, 0, n_itens_global * sizeof(int));

        MPI_Status status_msg;
        int valor_recebido;
        int solucao_recebida_buffer[MAX_N_ITENS_CONST];


        while(num_escravos_ativos > 0) {
            // Usar MPI_Probe para checar se é uma solução ou um pedido de tarefa (não implementado aqui)
            // Por ora, espera apenas soluções.
            MPI_Recv(&valor_recebido, 1, MPI_INT, MPI_ANY_SOURCE, TAG_NOVA_SOLUCAO, MPI_COMM_WORLD, &status_msg);
            int escravo_fonte = status_msg.MPI_SOURCE;

            // Recebe o array da solução correspondente
            MPI_Recv(solucao_recebida_buffer, n_itens_global, MPI_INT, escravo_fonte, TAG_NOVA_SOLUCAO, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (valor_recebido > melhor_valor_global_mpi) {
                melhor_valor_global_mpi = valor_recebido;
                if (n_itens_global > 0) {
                    memcpy(melhor_solucao_final, solucao_recebida_buffer, n_itens_global * sizeof(int));
                }
                printf("Mestre: Novo melhor valor global: %d (do escravo %d)\n", melhor_valor_global_mpi, escravo_fonte);
                // Notificar todos os outros escravos sobre o novo melhor valor
                for (int i = 1; i < num_procs; i++) {
                     MPI_Send(&melhor_valor_global_mpi, 1, MPI_INT, i, TAG_ATUALIZACAO_MELHOR_VALOR, MPI_COMM_WORLD);
                }
            }
            num_escravos_ativos--;
        }

        // Se não houve escravos (num_procs == 1), o mestre deveria ter feito o trabalho.
        // Esta lógica não está implementada aqui, assumimos num_procs >= 2 para paralelismo.
        // Se num_procs == 1, o mestre calcula e imprime o resultado guloso.

        if (num_procs > 1) {
            for (int i = 1; i < num_procs; i++) {
                MPI_Send(NULL, 0, MPI_BYTE, i, TAG_TERMINAR, MPI_COMM_WORLD);
            }
        }

        tempo_fim_total = MPI_Wtime();
        printf("\nValor máximo final na Mochila (MPI Branch and Bound): %d\n", melhor_valor_global_mpi);
        // Imprimir itens selecionados (se n_itens_global > 0 e solução foi encontrada/armazenada)
        if (n_itens_global > 0 && melhor_valor_global_mpi > 0) { // Apenas imprime se uma solução válida foi encontrada
            printf("Itens selecionados (índices originais, Peso, Valor):\n");
            int peso_total_solucao = 0;
            for (int i = 0; i < n_itens_global; i++) {
                if (melhor_solucao_final[i] == 1) {
                    // printf("  - Item original %d: Peso = %d, Valor = %d (Razao %.2f)\n",
                           //itens_ordenados_global[i].indice_original, itens_ordenados_global[i].peso, itens_ordenados_global[i].valor, itens_ordenados_global[i].razao);
                    peso_total_solucao += itens_ordenados_global[i].peso;
                }
            }
            printf("Peso total da solução: %d (Capacidade: %d)\n", peso_total_solucao, capacidade_mochila_global);
        }
        printf("Tempo de execução MPI (Mestre): %.3f ms\n", (tempo_fim_total - tempo_inicio_total) * 1000.0);

        if (itens_ordenados_global) free(itens_ordenados_global);

    } else { // Código do Escravo (rank > 0)
        int n_local, capacidade_local;
        Item *itens_local = NULL;
        int melhor_valor_escravo_atualizado; // Melhor valor conhecido pelo escravo, atualizado pelo mestre

        MPI_Recv(&n_local, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&capacidade_local, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        if (n_local > 0) {
             itens_local = (Item *)malloc(n_local * sizeof(Item));
             if(itens_local == NULL) { MPI_Abort(MPI_COMM_WORLD, 2); }
        }
        MPI_Recv(itens_local, n_local, MPI_ITEM_TYPE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&melhor_valor_escravo_atualizado, 1, MPI_INT, 0, TAG_ATUALIZACAO_MELHOR_VALOR, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        BnB_Tarefa tarefa_recebida;
        MPI_Status status_tarefa;
        MPI_Recv(&tarefa_recebida, sizeof(BnB_Tarefa), MPI_BYTE, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status_tarefa);

        if (status_tarefa.MPI_TAG == TAG_TAREFA_DADOS) {
            int *solucao_atual_escravo = (int *)calloc(n_local, sizeof(int));
            int *melhor_solucao_para_esta_tarefa = (int *)calloc(n_local, sizeof(int));
            if ((solucao_atual_escravo == NULL || melhor_solucao_para_esta_tarefa == NULL) && n_local > 0) {
                 if(solucao_atual_escravo) free(solucao_atual_escravo);
                 if(melhor_solucao_para_esta_tarefa) free(melhor_solucao_para_esta_tarefa);
                 if(itens_local) free(itens_local);
                 MPI_Abort(MPI_COMM_WORLD, 3);
            }

            // O valor inicial para melhor_valor_escravo_atualizado (que é passado para a função recursiva)
            // é o valor atualizado que o escravo tem do mestre.
            // A função recursiva tentará melhorar este valor.

            for(int i=0; i < tarefa_recebida.nivel_inicio; ++i) {
                solucao_atual_escravo[i] = tarefa_recebida.solucao_parcial_prefixo[i];
            }

            // O valor inicial para melhor_valor_local_escravo na função recursiva
            // será o `melhor_valor_escravo_atualizado` recebido do mestre.
            // Se a exploração desta tarefa encontrar algo melhor que isso, será atualizado.
            // O valor inicial da tarefa (tarefa_recebida.valor_atual) já está considerado ao iniciar
            // a recursão. A melhor_solucao_para_esta_tarefa será preenchida dentro da recursão
            // se um valor melhor que melhor_valor_escravo_atualizado for encontrado.
            if (n_local > 0) {
                 // Inicializa melhor_solucao_para_esta_tarefa com o prefixo da tarefa,
                 // pois este é o ponto de partida da exploração do escravo.
                 memcpy(melhor_solucao_para_esta_tarefa, solucao_atual_escravo, n_local * sizeof(int));
            }


            escravo_branch_and_bound_recursivo(tarefa_recebida.nivel_inicio,
                                               tarefa_recebida.peso_atual,
                                               tarefa_recebida.valor_atual,
                                               itens_local, n_local, capacidade_local,
                                               &melhor_valor_escravo_atualizado,
                                               solucao_atual_escravo, rank, melhor_solucao_para_esta_tarefa);

            // Após a exploração, melhor_valor_escravo_atualizado contém o melhor valor que este escravo
            // pôde encontrar ou confirmar para sua sub-árvore, considerando as atualizações do mestre.
            // E melhor_solucao_para_esta_tarefa contém o caminho.
            MPI_Send(&melhor_valor_escravo_atualizado, 1, MPI_INT, 0, TAG_NOVA_SOLUCAO, MPI_COMM_WORLD);
            MPI_Send(melhor_solucao_para_esta_tarefa, n_local, MPI_INT, 0, TAG_NOVA_SOLUCAO, MPI_COMM_WORLD);

            if (solucao_atual_escravo) free(solucao_atual_escravo);
            if (melhor_solucao_para_esta_tarefa) free(melhor_solucao_para_esta_tarefa);

        } else if (status_tarefa.MPI_TAG == TAG_TERMINAR) {
            // Não recebeu tarefa, apenas sinal para terminar.
        }

        // Aguarda o sinal de término final do mestre, caso ainda não o tenha recebido como a primeira mensagem.
        // Se recebeu TAG_TAREFA_DADOS, precisa deste Recv.
        // Se recebeu TAG_TERMINAR no Recv anterior, este Recv também pegará um TAG_TERMINAR (o mestre envia para todos).
        // Mas isso pode causar deadlock se o mestre já enviou TAG_TERMINAR e o escravo recebeu e saiu do if/else.
        // A lógica de terminação precisa ser robusta.
        // Um escravo que recebeu TAG_TAREFA_DADOS e processou, deve esperar pelo TAG_TERMINAR final.
        // Um escravo que recebeu TAG_TERMINAR inicialmente, já pode ter saído.
        // A forma mais simples é que todo escravo espere uma última mensagem TAG_TERMINAR.

        if (status_tarefa.MPI_TAG != TAG_TERMINAR) { // Se não recebeu terminar como primeira mensagem
             MPI_Recv(NULL, 0, MPI_BYTE, 0, TAG_TERMINAR, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }

        if(itens_local) free(itens_local);
    }

    MPI_Type_free(&MPI_ITEM_TYPE);
    MPI_Finalize();
    return 0;
}


/**
 * @brief (Escravo) Calcula um limite superior (upper bound) para o valor máximo que pode ser obtido a partir do nível atual.
 */
static double calcular_limite_superior_mpi(int nivel, int peso_acumulado, int valor_acumulado,
                                           const Item itens[], int n_itens, int capacidade_maxima) {
    double limite_superior = (double)valor_acumulado;
    int peso_restante = capacidade_maxima - peso_acumulado;
    int i = nivel;

    while (i < n_itens && itens[i].peso <= peso_restante) {
        peso_restante -= itens[i].peso;
        limite_superior += itens[i].valor;
        i++;
    }
    if (i < n_itens && peso_restante > 0) {
        limite_superior += (double)peso_restante * itens[i].razao;
    }
    return limite_superior;
}

/**
 * @brief (Escravo) Função recursiva principal para o algoritmo Branch and Bound, executada pelo escravo.
 */
static void escravo_branch_and_bound_recursivo(int nivel, int peso_atual, int valor_atual,
                                               const Item itens_ordenados[], int n_itens, int capacidade_mochila,
                                               int *melhor_valor_global_conhecido_pelo_escravo,
                                               int solucao_atual_escravo[],
                                               int rank_escravo, int melhor_solucao_da_subarvore_escravo[]) {

    int flag_msg_pendente = 0;
    MPI_Status status_iprobe;
    // Verifica se há uma atualização do melhor valor global enviada pelo mestre
    MPI_Iprobe(0, TAG_ATUALIZACAO_MELHOR_VALOR, MPI_COMM_WORLD, &flag_msg_pendente, &status_iprobe);
    if (flag_msg_pendente) {
        int novo_melhor_valor_global_recebido;
        MPI_Recv(&novo_melhor_valor_global_recebido, 1, MPI_INT, 0, TAG_ATUALIZACAO_MELHOR_VALOR, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        if (novo_melhor_valor_global_recebido > *melhor_valor_global_conhecido_pelo_escravo) {
            *melhor_valor_global_conhecido_pelo_escravo = novo_melhor_valor_global_recebido;
            // printf("Escravo %d: Atualizou melhor_valor_global_conhecido para %d via Iprobe (nível %d)\n",
            //        rank_escravo, *melhor_valor_global_conhecido_pelo_escravo, nivel);
        }
    }

    // Atualiza a melhor solução encontrada *nesta subárvore específica pelo escravo*
    // se o valor_atual for melhor que o que ele já tem para *esta subárvore*.
    // No entanto, a poda global usa `melhor_valor_global_conhecido_pelo_escravo`.
    // Se `valor_atual` for maior que `melhor_valor_global_conhecido_pelo_escravo`, ele se torna um candidato a ser o novo melhor global.
    if (valor_atual > *melhor_valor_global_conhecido_pelo_escravo) {
        *melhor_valor_global_conhecido_pelo_escravo = valor_atual; // Esta linha é crucial. O escravo atualiza seu conhecimento do melhor global.
        memcpy(melhor_solucao_da_subarvore_escravo, solucao_atual_escravo, n_itens * sizeof(int));
    }
    // Se apenas valor_atual > valor da melhor_solucao_da_subarvore_escravo, mas não > melhor_valor_global_conhecido_pelo_escravo,
    // ainda assim atualizamos a melhor_solucao_da_subarvore_escravo para refletir o melhor desta exploração.
    // O jeito mais simples é ter uma variável separada para o melhor valor *desta subárvore*.
    // Para manter simples, o `melhor_solucao_da_subarvore_escravo` será associado ao `melhor_valor_global_conhecido_pelo_escravo`
    // se este escravo o melhorou.


    if (nivel == n_itens) {
        return;
    }

    double limite_sup = calcular_limite_superior_mpi(nivel, peso_atual, valor_atual, itens_ordenados, n_itens, capacidade_mochila);
    if (limite_sup <= (double)(*melhor_valor_global_conhecido_pelo_escravo)) {
        return;
    }

    // Ramo 1: Incluir o item do nível atual
    if (peso_atual + itens_ordenados[nivel].peso <= capacidade_mochila) {
        solucao_atual_escravo[nivel] = 1;
        escravo_branch_and_bound_recursivo(nivel + 1,
                                           peso_atual + itens_ordenados[nivel].peso,
                                           valor_atual + itens_ordenados[nivel].valor,
                                           itens_ordenados, n_itens, capacidade_mochila,
                                           melhor_valor_global_conhecido_pelo_escravo, solucao_atual_escravo,
                                           rank_escravo, melhor_solucao_da_subarvore_escravo);
    }

    // Ramo 2: Não incluir o item do nível atual
    double limite_sup_sem_item_atual = calcular_limite_superior_mpi(nivel + 1, peso_atual, valor_atual, itens_ordenados, n_itens, capacidade_mochila);
    if (limite_sup_sem_item_atual > (double)(*melhor_valor_global_conhecido_pelo_escravo)) {
        solucao_atual_escravo[nivel] = 0;
        escravo_branch_and_bound_recursivo(nivel + 1,
                                           peso_atual,
                                           valor_atual,
                                           itens_ordenados, n_itens, capacidade_mochila,
                                           melhor_valor_global_conhecido_pelo_escravo, solucao_atual_escravo,
                                           rank_escravo, melhor_solucao_da_subarvore_escravo);
    }
    solucao_atual_escravo[nivel] = 0;
}
