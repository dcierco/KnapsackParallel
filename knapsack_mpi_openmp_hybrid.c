#include <mpi.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "knapsack_common.h"

// Tags para comunicação MPI
#define TAG_ITEM_DATA 0
#define TAG_TASK 1
#define TAG_RESULT 2
#define TAG_TERMINATE 4

// Estrutura para tarefas MPI simplificada
typedef struct {
    int nivel_inicio;
    int nivel_fim;
    int peso_atual;
    int valor_atual;
    int solucao_parcial[50]; // Buffer fixo
} MPITask;

// Forward declaration
static void solve_brute_force_sequential(int nivel, int nivel_fim, int peso_atual, int valor_atual,
                                        const Item itens[], int n_itens, int capacidade,
                                        int *melhor_valor, int solucao_atual[], int melhor_solucao[],
                                        long long *nos_explorados);

// Força bruta com OpenMP e limite de nós
static void solve_with_openmp(int nivel_inicio, int nivel_fim, int peso_atual, int valor_atual,
                             const Item itens[], int n_itens, int capacidade,
                             int *melhor_valor_global, int melhor_solucao_global[],
                             long long *nos_explorados_global, int solucao_inicial[]) {
    
    // Thread-local variables
    int melhor_valor_local = valor_atual;
    int *melhor_solucao_local = malloc(n_itens * sizeof(int));
    memcpy(melhor_solucao_local, solucao_inicial, n_itens * sizeof(int));
    long long nos_explorados_local = 0;
    
    // Paraleliza apenas o primeiro nível para evitar overhead excessivo
    #pragma omp parallel
    {
        int *solucao_thread = malloc(n_itens * sizeof(int));
        memcpy(solucao_thread, solucao_inicial, n_itens * sizeof(int));
        int melhor_valor_thread = valor_atual;
        int *melhor_solucao_thread = malloc(n_itens * sizeof(int));
        memcpy(melhor_solucao_thread, solucao_inicial, n_itens * sizeof(int));
        long long nos_thread = 0;
        
        // Cada thread processa uma parte das possibilidades  
        #pragma omp for schedule(dynamic, 1)
        for (int choice = 0; choice < 2; choice++) {
            
            if (choice == 0) {
                // Não incluir primeiro item
                solucao_thread[nivel_inicio] = 0;
                solve_brute_force_sequential(nivel_inicio + 1, nivel_fim, peso_atual, valor_atual,
                                           itens, n_itens, capacidade, &melhor_valor_thread,
                                           solucao_thread, melhor_solucao_thread, &nos_thread);
            } else {
                // Incluir primeiro item (se couber)
                if (peso_atual + itens[nivel_inicio].peso <= capacidade) {
                    solucao_thread[nivel_inicio] = 1;
                    solve_brute_force_sequential(nivel_inicio + 1, nivel_fim,
                                               peso_atual + itens[nivel_inicio].peso,
                                               valor_atual + itens[nivel_inicio].valor,
                                               itens, n_itens, capacidade, &melhor_valor_thread,
                                               solucao_thread, melhor_solucao_thread, &nos_thread);
                }
            }
        }
        
        // Critical section para atualizar resultado global
        #pragma omp critical
        {
            nos_explorados_local += nos_thread;
            if (melhor_valor_thread > melhor_valor_local) {
                melhor_valor_local = melhor_valor_thread;
                memcpy(melhor_solucao_local, melhor_solucao_thread, n_itens * sizeof(int));
            }
        }
        
        free(solucao_thread);
        free(melhor_solucao_thread);
    }
    
    *melhor_valor_global = melhor_valor_local;
    memcpy(melhor_solucao_global, melhor_solucao_local, n_itens * sizeof(int));
    *nos_explorados_global = nos_explorados_local;
    
    free(melhor_solucao_local);
}

// Força bruta sequencial simples com limite
static void solve_brute_force_sequential(int nivel, int nivel_fim, int peso_atual, int valor_atual,
                                        const Item itens[], int n_itens, int capacidade,
                                        int *melhor_valor, int solucao_atual[], int melhor_solucao[],
                                        long long *nos_explorados) {
    (*nos_explorados)++;
    
    if (nivel > nivel_fim) {
        if (valor_atual > *melhor_valor) {
            *melhor_valor = valor_atual;
            memcpy(melhor_solucao, solucao_atual, n_itens * sizeof(int));
        }
        return;
    }
    
    // Não incluir item
    solucao_atual[nivel] = 0;
    solve_brute_force_sequential(nivel + 1, nivel_fim, peso_atual, valor_atual,
                               itens, n_itens, capacidade, melhor_valor, solucao_atual,
                               melhor_solucao, nos_explorados);
    
    // Incluir item (se couber)
    if (peso_atual + itens[nivel].peso <= capacidade) {
        solucao_atual[nivel] = 1;
        solve_brute_force_sequential(nivel + 1, nivel_fim,
                                   peso_atual + itens[nivel].peso,
                                   valor_atual + itens[nivel].valor,
                                   itens, n_itens, capacidade, melhor_valor, solucao_atual,
                                   melhor_solucao, nos_explorados);
    }
    
    solucao_atual[nivel] = 0; // backtrack
}

int main(int argc, char *argv[]) {
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    
    if (provided < MPI_THREAD_FUNNELED) {
        fprintf(stderr, "MPI não suporta threading necessário\\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    
    int rank, num_procs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    if (num_procs < 2) {
        if (rank == 0) {
            fprintf(stderr, "Este programa requer pelo menos 2 processos MPI\\n");
        }
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    double tempo_inicio, tempo_fim;

    if (rank == 0) { // PROCESSO MESTRE
        if (argc < 2) {
            fprintf(stderr, "Uso: %s <arquivo_de_entrada>\\n", argv[0]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        FILE *arquivo = fopen(argv[1], "r");
        if (!arquivo) {
            perror("Erro ao abrir arquivo");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        int n_itens, capacidade;
        if (fscanf(arquivo, "%d %d", &n_itens, &capacidade) != 2) {
            fprintf(stderr, "Erro ao ler cabeçalho\\n");
            fclose(arquivo);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        Item *itens = malloc(n_itens * sizeof(Item));
        for (int i = 0; i < n_itens; i++) {
            if (fscanf(arquivo, "%d %d", &itens[i].valor, &itens[i].peso) != 2) {
                fprintf(stderr, "Erro ao ler item %d\\n", i);
                free(itens);
                fclose(arquivo);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            itens[i].indice_original = i;
            itens[i].razao = (itens[i].peso > 0) ? 
                            (double)itens[i].valor / itens[i].peso : 
                            (itens[i].valor > 0 ? __DBL_MAX__ : 0.0);
        }
        fclose(arquivo);

        tempo_inicio = MPI_Wtime();

        // Envia dados para workers
        for (int i = 1; i < num_procs; i++) {
            MPI_Send(&n_itens, 1, MPI_INT, i, TAG_ITEM_DATA, MPI_COMM_WORLD);
            MPI_Send(&capacidade, 1, MPI_INT, i, TAG_ITEM_DATA, MPI_COMM_WORLD);
            
            for (int j = 0; j < n_itens; j++) {
                MPI_Send(&itens[j].peso, 1, MPI_INT, i, TAG_ITEM_DATA, MPI_COMM_WORLD);
                MPI_Send(&itens[j].valor, 1, MPI_INT, i, TAG_ITEM_DATA, MPI_COMM_WORLD);
                MPI_Send(&itens[j].razao, 1, MPI_DOUBLE, i, TAG_ITEM_DATA, MPI_COMM_WORLD);
                MPI_Send(&itens[j].indice_original, 1, MPI_INT, i, TAG_ITEM_DATA, MPI_COMM_WORLD);
            }
        }

        // Gera tarefas simples
        int tasks_per_worker = 4;
        int max_tasks = (num_procs - 1) * tasks_per_worker;
        MPITask *tasks = malloc(max_tasks * sizeof(MPITask));
        int task_count = 0;
        
        int task_depth = 3;
        if (num_procs >= 4) task_depth = 4;
        
        for (int combo = 0; combo < (1 << task_depth) && task_count < max_tasks; combo++) {
            MPITask *task = &tasks[task_count];
            task->nivel_inicio = task_depth;
            task->nivel_fim = n_itens - 1;
            task->peso_atual = 0;
            task->valor_atual = 0;
            memset(task->solucao_parcial, 0, 50 * sizeof(int));
            
            int valid = 1;
            for (int bit = 0; bit < task_depth; bit++) {
                if (combo & (1 << bit)) {
                    if (task->peso_atual + itens[bit].peso <= capacidade) {
                        task->solucao_parcial[bit] = 1;
                        task->peso_atual += itens[bit].peso;
                        task->valor_atual += itens[bit].valor;
                    } else {
                        valid = 0;
                        break;
                    }
                }
            }
            
            if (valid) task_count++;
        }

        int melhor_valor_global = 0;
        int melhor_solucao_global[50];
        memset(melhor_solucao_global, 0, 50 * sizeof(int));
        long long total_nos = 0;
        int workers_ativos = num_procs - 1;
        int task_index = 0;

        // Distribui trabalho inicial
        for (int i = 1; i < num_procs && task_index < task_count; i++) {
            MPITask *task = &tasks[task_index++];
            
            MPI_Send(&task->nivel_inicio, 1, MPI_INT, i, TAG_TASK, MPI_COMM_WORLD);
            MPI_Send(&task->nivel_fim, 1, MPI_INT, i, TAG_TASK, MPI_COMM_WORLD);
            MPI_Send(&task->peso_atual, 1, MPI_INT, i, TAG_TASK, MPI_COMM_WORLD);
            MPI_Send(&task->valor_atual, 1, MPI_INT, i, TAG_TASK, MPI_COMM_WORLD);
            MPI_Send(task->solucao_parcial, n_itens, MPI_INT, i, TAG_TASK, MPI_COMM_WORLD);
        }

        // Coleta resultados
        while (workers_ativos > 0) {
            MPI_Status status;
            MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            
            if (status.MPI_TAG == TAG_RESULT) {
                int result_data[2];
                MPI_Recv(result_data, 2, MPI_INT, status.MPI_SOURCE, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                
                int solucao_recebida[50];
                MPI_Recv(solucao_recebida, n_itens, MPI_INT, status.MPI_SOURCE, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                
                total_nos += result_data[1];
                
                if (result_data[0] > melhor_valor_global) {
                    melhor_valor_global = result_data[0];
                    memcpy(melhor_solucao_global, solucao_recebida, n_itens * sizeof(int));
                }
                
                if (task_index < task_count) {
                    MPITask *task = &tasks[task_index++];
                    
                    MPI_Send(&task->nivel_inicio, 1, MPI_INT, status.MPI_SOURCE, TAG_TASK, MPI_COMM_WORLD);
                    MPI_Send(&task->nivel_fim, 1, MPI_INT, status.MPI_SOURCE, TAG_TASK, MPI_COMM_WORLD);
                    MPI_Send(&task->peso_atual, 1, MPI_INT, status.MPI_SOURCE, TAG_TASK, MPI_COMM_WORLD);
                    MPI_Send(&task->valor_atual, 1, MPI_INT, status.MPI_SOURCE, TAG_TASK, MPI_COMM_WORLD);
                    MPI_Send(task->solucao_parcial, n_itens, MPI_INT, status.MPI_SOURCE, TAG_TASK, MPI_COMM_WORLD);
                } else {
                    int terminate = 0;
                    MPI_Send(&terminate, 1, MPI_INT, status.MPI_SOURCE, TAG_TERMINATE, MPI_COMM_WORLD);
                    workers_ativos--;
                }
            }
        }

        tempo_fim = MPI_Wtime();

        int peso_total = 0, itens_selecionados = 0;
        for (int i = 0; i < n_itens; i++) {
            if (melhor_solucao_global[i]) {
                peso_total += itens[i].peso;
                itens_selecionados++;
            }
        }
        
        printf("HYBRID(%d): Valor=%d, Itens=%d/%d, Peso=%d/%d, Nós=%lld, Tempo=%.3fms\\n", 
               num_procs, melhor_valor_global, itens_selecionados, n_itens, 
               peso_total, capacidade, total_nos, (tempo_fim - tempo_inicio) * 1000.0);

        free(itens);
        free(tasks);

    } else { // PROCESSO WORKER
        
        int n_itens, capacidade;
        MPI_Recv(&n_itens, 1, MPI_INT, 0, TAG_ITEM_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&capacidade, 1, MPI_INT, 0, TAG_ITEM_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        Item *itens = malloc(n_itens * sizeof(Item));
        for (int i = 0; i < n_itens; i++) {
            MPI_Recv(&itens[i].peso, 1, MPI_INT, 0, TAG_ITEM_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&itens[i].valor, 1, MPI_INT, 0, TAG_ITEM_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&itens[i].razao, 1, MPI_DOUBLE, 0, TAG_ITEM_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&itens[i].indice_original, 1, MPI_INT, 0, TAG_ITEM_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        
        while (1) {
            MPI_Status status;
            MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            
            if (status.MPI_TAG == TAG_TERMINATE) {
                int dummy;
                MPI_Recv(&dummy, 1, MPI_INT, 0, TAG_TERMINATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                break;
            }
            
            if (status.MPI_TAG == TAG_TASK) {
                MPITask task;
                
                MPI_Recv(&task.nivel_inicio, 1, MPI_INT, 0, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(&task.nivel_fim, 1, MPI_INT, 0, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(&task.peso_atual, 1, MPI_INT, 0, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(&task.valor_atual, 1, MPI_INT, 0, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(task.solucao_parcial, n_itens, MPI_INT, 0, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                // Resolve com OpenMP
                int melhor_valor = task.valor_atual;
                int melhor_solucao[50];
                memcpy(melhor_solucao, task.solucao_parcial, n_itens * sizeof(int));
                long long nos_explorados = 0;
                
                // Sem limites - força bruta completa para escalabilidade real
                solve_with_openmp(task.nivel_inicio, task.nivel_fim, task.peso_atual, task.valor_atual,
                                 itens, n_itens, capacidade, &melhor_valor, melhor_solucao,
                                 &nos_explorados, task.solucao_parcial);

                int result_data[2] = {melhor_valor, (int)nos_explorados};
                MPI_Send(result_data, 2, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
                MPI_Send(melhor_solucao, n_itens, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
            }
        }

        free(itens);
    }

    MPI_Finalize();
    return 0;
}