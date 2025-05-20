#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

    if (num_procs < 2 && rank == 0) {
        fprintf(stderr, "Este programa MPI requer pelo menos 2 processos (1 mestre, 1+ escravos).\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
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

        fscanf(arquivo_entrada, "%d %d", &n_itens_global, &capacidade_mochila_global);
        if (n_itens_global <= 0 || capacidade_mochila_global < 0 || n_itens_global > MAX_N_ITENS_CONST) {
            fprintf(stderr, "Mestre: Parâmetros inválidos N=%d (max %d), W=%d\n", n_itens_global, MAX_N_ITENS_CONST, capacidade_mochila_global);
            fclose(arquivo_entrada);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        itens_ordenados_global = (Item *)malloc(n_itens_global * sizeof(Item));
        for (int i = 0; i < n_itens_global; i++) {
            fscanf(arquivo_entrada, "%d %d", &itens_ordenados_global[i].valor, &itens_ordenados_global[i].peso);
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
        int *solucao_gulosa_temp = (int *)calloc(n_itens_global, sizeof(int));
        int peso_guloso = 0;
        for(int i=0; i<n_itens_global; ++i) {
            if (peso_guloso + itens_ordenados_global[i].peso <= capacidade_mochila_global) {
                peso_guloso += itens_ordenados_global[i].peso;
                melhor_valor_global_mpi += itens_ordenados_global[i].valor;
                // solucao_gulosa_temp[i] = 1; // Não precisamos guardar o caminho guloso globalmente ainda
            }
        }
        free(solucao_gulosa_temp);
        printf("Mestre: Melhor valor inicial (guloso): %d\n", melhor_valor_global_mpi);
        tempo_inicio_total = MPI_Wtime();

        // Enviar dados iniciais para escravos
        for (int i = 1; i < num_procs; i++) {
            MPI_Send(&n_itens_global, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
            MPI_Send(&capacidade_mochila_global, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
            MPI_Send(itens_ordenados_global, n_itens_global, MPI_ITEM_TYPE, i, 0, MPI_COMM_WORLD);
            MPI_Send(&melhor_valor_global_mpi, 1, MPI_INT, i, TAG_ATUALIZACAO_MELHOR_VALOR, MPI_COMM_WORLD);
        }

        // Geração e distribuição de tarefas iniciais
        // O mestre divide o trabalho no primeiro nível da árvore BnB
        BnB_Tarefa tarefa;
        int tarefas_distribuidas = 0;
        int num_escravos_ativos = 0;

        // Tarefa 1: Incluir o primeiro item (se possível)
        if (n_itens_global > 0 && itens_ordenados_global[0].peso <= capacidade_mochila_global) {
            if (tarefas_distribuidas < num_procs -1) {
                tarefa.nivel_inicio = 1; // Começa a decidir sobre o item 1
                tarefa.peso_atual = itens_ordenados_global[0].peso;
                tarefa.valor_atual = itens_ordenados_global[0].valor;
                memset(tarefa.solucao_parcial_prefixo, 0, MAX_N_ITENS_CONST * sizeof(int));
                tarefa.solucao_parcial_prefixo[0] = 1; // Item 0 incluído

                MPI_Send(&tarefa, sizeof(BnB_Tarefa), MPI_BYTE, tarefas_distribuidas + 1, TAG_TAREFA_DADOS, MPI_COMM_WORLD);
                tarefas_distribuidas++;
                num_escravos_ativos++;
            } else { /* Adicionar à uma pilha de tarefas pendentes se houver mais tarefas que escravos */ }
        }
        // Tarefa 2: Não incluir o primeiro item
        if (n_itens_global > 0) { // Sempre possível não incluir
             if (tarefas_distribuidas < num_procs -1) {
                tarefa.nivel_inicio = 1; // Começa a decidir sobre o item 1
                tarefa.peso_atual = 0;
                tarefa.valor_atual = 0;
                memset(tarefa.solucao_parcial_prefixo, 0, MAX_N_ITENS_CONST * sizeof(int));
                tarefa.solucao_parcial_prefixo[0] = 0; // Item 0 não incluído

                MPI_Send(&tarefa, sizeof(BnB_Tarefa), MPI_BYTE, tarefas_distribuidas + 1, TAG_TAREFA_DADOS, MPI_COMM_WORLD);
                tarefas_distribuidas++;
                num_escravos_ativos++;
             } else { /* Adicionar à pilha de tarefas */ }
        }

        // Se não houver itens, ou menos tarefas geradas que escravos, alguns escravos podem ficar ociosos.
        // O mestre precisa enviar TAG_SEM_TAREFAS para eles para que não fiquem esperando indefinidamente por uma tarefa.
        // Ou, mais robustamente, o mestre deveria ter uma pilha de tarefas e os escravos pediriam tarefas.
        // Para esta versão, se não houver tarefas suficientes, os escravos extras não receberão trabalho inicial.

        // Loop de gerenciamento do mestre
        int melhor_solucao_final[MAX_N_ITENS_CONST] = {0}; // Para armazenar o caminho da melhor solução

        MPI_Status status_msg;
        int valor_recebido;
        int solucao_recebida[MAX_N_ITENS_CONST];

        while(num_escravos_ativos > 0) {
            MPI_Recv(&valor_recebido, 1, MPI_INT, MPI_ANY_SOURCE, TAG_NOVA_SOLUCAO, MPI_COMM_WORLD, &status_msg);
            int escravo_fonte = status_msg.MPI_SOURCE;
            MPI_Recv(solucao_recebida, n_itens_global, MPI_INT, escravo_fonte, TAG_NOVA_SOLUCAO, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            //printf("Mestre: Recebeu solução de valor %d do escravo %d\n", valor_recebido, escravo_fonte);

            if (valor_recebido > melhor_valor_global_mpi) {
                melhor_valor_global_mpi = valor_recebido;
                memcpy(melhor_solucao_final, solucao_recebida, n_itens_global * sizeof(int));
                printf("Mestre: Novo melhor valor global: %d (do escravo %d)\n", melhor_valor_global_mpi, escravo_fonte);
                // Notificar todos os outros escravos sobre o novo melhor valor
                for (int i = 1; i < num_procs; i++) {
                    //if (i != escravo_fonte) { // O escravo que enviou já sabe
                        MPI_Send(&melhor_valor_global_mpi, 1, MPI_INT, i, TAG_ATUALIZACAO_MELHOR_VALOR, MPI_COMM_WORLD);
                    //}
                }
            }
            // Escravo terminou sua tarefa atribuída (seja ela qual for)
            // Para um modelo simples, assumimos que cada escravo só recebe uma grande tarefa inicial.
            // E após enviar sua melhor solução (ou nenhuma se não achou nada melhor), ele efetivamente terminou.
            // Em um modelo mais dinâmico, ele pediria mais trabalho.
            // Para este exemplo, vamos apenas decrementar ativos quando recebemos a TAG_NOVA_SOLUCAO
            // (assumindo que o escravo envia isso ao final de seu trabalho).
            num_escravos_ativos--;
        }


        // Enviar sinal de término para todos os escravos
        for (int i = 1; i < num_procs; i++) {
             // Se um escravo não recebeu uma tarefa inicial, ele pode estar esperando por TAG_TAREFA_DADOS
             // Precisamos de um mecanismo mais robusto para escravos pedirem tarefas
             // Por ora, vamos enviar TAG_TERMINAR. O escravo deve estar preparado para receber isso.
            MPI_Send(NULL, 0, MPI_BYTE, i, TAG_TERMINAR, MPI_COMM_WORLD);
        }

        tempo_fim_total = MPI_Wtime();
        printf("\nValor máximo final na Mochila (MPI Branch and Bound): %d\n", melhor_valor_global_mpi);
        // Imprimir itens selecionados (opcional, requer reconstrução a partir de melhor_solucao_final e itens_ordenados_global)
        printf("Tempo de execução MPI (Mestre): %.3f ms\n", (tempo_fim_total - tempo_inicio_total) * 1000.0);

        free(itens_ordenados_global);

    } else { // Código do Escravo
        int n_local, capacidade_local;
        Item *itens_local;
        int melhor_valor_escravo;

        MPI_Recv(&n_local, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&capacidade_local, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        itens_local = (Item *)malloc(n_local * sizeof(Item));
        MPI_Recv(itens_local, n_local, MPI_ITEM_TYPE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&melhor_valor_escravo, 1, MPI_INT, 0, TAG_ATUALIZACAO_MELHOR_VALOR, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        //printf("Escravo %d: Recebeu dados iniciais. N=%d, W=%d, MelhorValorInicial=%d\n", rank, n_local, capacidade_local, melhor_valor_escravo);

        BnB_Tarefa tarefa_recebida;
        MPI_Status status_tarefa;
        // Tenta receber uma tarefa. Se o mestre não tiver uma para este escravo, ele pode receber TAG_TERMINAR.
        MPI_Recv(&tarefa_recebida, sizeof(BnB_Tarefa), MPI_BYTE, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status_tarefa);

        if (status_tarefa.MPI_TAG == TAG_TAREFA_DADOS) {
            //printf("Escravo %d: Recebeu tarefa. Nível %d, Peso %d, Valor %d\n", rank, tarefa_recebida.nivel_inicio, tarefa_recebida.peso_atual, tarefa_recebida.valor_atual);

            int *solucao_atual_escravo = (int *)calloc(n_local, sizeof(int));
            int *melhor_solucao_escravo_local = (int *)calloc(n_local, sizeof(int));

            // Copia o prefixo da solução para o array de solução atual do escravo
            for(int i=0; i < tarefa_recebida.nivel_inicio; ++i) {
                solucao_atual_escravo[i] = tarefa_recebida.solucao_parcial_prefixo[i];
            }

            // Inicia o BnB para o subproblema atribuído
            escravo_branch_and_bound_recursivo(tarefa_recebida.nivel_inicio,
                                               tarefa_recebida.peso_atual,
                                               tarefa_recebida.valor_atual,
                                               itens_local, n_local, capacidade_local,
                                               &melhor_valor_escravo, // Passa o melhor valor conhecido pelo escravo
                                               solucao_atual_escravo, rank, melhor_solucao_escravo_local);

            // Ao final da sua exploração, o escravo envia seu melhor resultado encontrado para o mestre.
            // O escravo_branch_and_bound_recursivo já deve ter atualizado melhor_valor_escravo e melhor_solucao_escravo_local
            // se encontrou algo melhor que o valor inicial/atualizações.
            // Se não encontrou nada melhor, envia o valor que tinha (que pode ser o inicial guloso).
            MPI_Send(&melhor_valor_escravo, 1, MPI_INT, 0, TAG_NOVA_SOLUCAO, MPI_COMM_WORLD);
            MPI_Send(melhor_solucao_escravo_local, n_local, MPI_INT, 0, TAG_NOVA_SOLUCAO, MPI_COMM_WORLD);
            //printf("Escravo %d: Terminou sua tarefa e enviou resultado %d.\n", rank, melhor_valor_escravo);

            free(solucao_atual_escravo);
            free(melhor_solucao_escravo_local);

        } else if (status_tarefa.MPI_TAG == TAG_TERMINAR) {
            //printf("Escravo %d: Recebeu TAG_TERMINAR sem receber tarefa.\n", rank);
        } else {
            //printf("Escravo %d: Recebeu tag inesperada %d ao esperar tarefa.\n", rank, status_tarefa.MPI_TAG);
        }

        // O escravo pode precisar de um loop para continuar recebendo atualizações ou o sinal de término final
        // se o modelo de trabalho fosse mais dinâmico. Para este modelo simples, ele faz uma tarefa e termina.
        // Um loop de escuta para TAG_TERMINAR ou TAG_ATUALIZACAO_MELHOR_VALOR seria necessário aqui
        // se o escravo pudesse receber múltiplas tarefas ou se o mestre continuasse enviando atualizações
        // mesmo após o escravo ter enviado seu resultado final.
        // Para simplificar, o escravo finaliza após sua única tarefa (ou se não recebeu nenhuma).
        // O mestre deve garantir que o sinal de término é o último.

        MPI_Status status_final;
        int dummy_buf; // Para MPI_Recv de TAG_TERMINAR se for apenas um sinal
        // Loop para aguardar atualizações ou sinal de término final, caso o escravo ainda não tenha recebido.
        // Isto é importante se o mestre envia atualizações e depois o término.
        // No modelo atual, o escravo só faz uma tarefa. Ele já recebeu o término ou uma tarefa.
        // Se recebeu tarefa, ele processou. O mestre espera a TAG_NOVA_SOLUCAO.
        // Se o mestre enviasse TAG_TERMINAR depois disso, o escravo precisaria de outro Recv.
        // Por ora, o escravo finaliza após enviar sua solução. O mestre coordena o término.
        // Para garantir, adicionamos um Recv bloqueante para TAG_TERMINAR.

        // Se o escravo recebeu e processou uma tarefa, ele já enviou TAG_NOVA_SOLUCAO.
        // Ele ainda precisa receber o sinal final de TAG_TERMINAR do mestre.
        if (status_tarefa.MPI_TAG == TAG_TAREFA_DADOS) { // Se processou uma tarefa
            // Ele pode receber atualizações de melhor valor enquanto processa (não implementado com Iprobe aqui)
            // Depois de enviar sua solução, espera o sinal final de término.
            MPI_Recv(NULL, 0, MPI_BYTE, 0, TAG_TERMINAR, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            //printf("Escravo %d: Recebeu sinal final de término.\n", rank);
        }


        free(itens_local);
    }

    MPI_Type_free(&MPI_ITEM_TYPE);
    MPI_Finalize();
    return 0;
}


/**
 * @brief (Escravo) Calcula um limite superior (upper bound) para o valor máximo que pode ser obtido a partir do nível atual.
 * Cópia da função sequencial, adaptada para o contexto do escravo se necessário (nenhuma mudança aqui).
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
 * Modificada para:
 * 1. Usar `melhor_valor_local_escravo` para suas próprias decisões de poda.
 * 2. Não envia diretamente ao mestre a cada melhora local, mas atualiza `melhor_solucao_escravo`.
 *    O resultado final da exploração da sua sub-árvore é enviado uma vez ao mestre.
 *    (Nota: uma estratégia mais dinâmica enviaria melhorias parciais ao mestre imediatamente).
 *    Para esta versão, vamos atualizar melhor_valor_local_escravo e melhor_solucao_escravo,
 *    e o escravo enviará estes ao mestre *após* sua sub-árvore ser totalmente explorada.
 */
static void escravo_branch_and_bound_recursivo(int nivel, int peso_atual, int valor_atual,
                                               const Item itens_ordenados[], int n_itens, int capacidade_mochila,
                                               int *melhor_valor_local_escravo, int solucao_atual_escravo[],
                                               int rank_escravo, int melhor_solucao_escravo[]) {

    // O escravo também pode receber atualizações do mestre sobre o melhor_valor_global.
    // Para uma implementação simples, ele usa o `melhor_valor_local_escravo` que foi inicializado
    // e atualizado na última vez que ouviu do mestre.
    // Uma implementação mais avançada usaria MPI_Iprobe aqui para checar mensagens do mestre.

    if (valor_atual > *melhor_valor_local_escravo) {
        *melhor_valor_local_escravo = valor_atual;
        memcpy(melhor_solucao_escravo, solucao_atual_escravo, n_itens * sizeof(int));
        //printf("Escravo %d: Novo melhor local: %d (nível %d)\n", rank_escravo, *melhor_valor_local_escravo, nivel);
    }

    if (nivel == n_itens) {
        return;
    }

    double limite_sup = calcular_limite_superior_mpi(nivel, peso_atual, valor_atual, itens_ordenados, n_itens, capacidade_mochila);
    if (limite_sup <= (double)(*melhor_valor_local_escravo)) {
        return;
    }

    // Ramo 1: Incluir o item do nível atual
    if (peso_atual + itens_ordenados[nivel].peso <= capacidade_mochila) {
        solucao_atual_escravo[nivel] = 1;
        escravo_branch_and_bound_recursivo(nivel + 1,
                                           peso_atual + itens_ordenados[nivel].peso,
                                           valor_atual + itens_ordenados[nivel].valor,
                                           itens_ordenados, n_itens, capacidade_mochila,
                                           melhor_valor_local_escravo, solucao_atual_escravo,
                                           rank_escravo, melhor_solucao_escravo);
    }

    // Ramo 2: Não incluir o item do nível atual
    // Verifica o limite ANTES de fazer a chamada recursiva para este ramo
    double limite_sup_sem_item_atual = calcular_limite_superior_mpi(nivel + 1, peso_atual, valor_atual, itens_ordenados, n_itens, capacidade_mochila);
    if (limite_sup_sem_item_atual > (double)(*melhor_valor_local_escravo)) {
        solucao_atual_escravo[nivel] = 0;
        escravo_branch_and_bound_recursivo(nivel + 1,
                                           peso_atual,
                                           valor_atual,
                                           itens_ordenados, n_itens, capacidade_mochila,
                                           melhor_valor_local_escravo, solucao_atual_escravo,
                                           rank_escravo, melhor_solucao_escravo);
    }
    solucao_atual_escravo[nivel] = 0; // Backtrack (limpa para o nível pai na pilha de recursão)
}
