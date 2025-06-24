#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "knapsack_common.h"

// Tags para comunicação MPI
#define TAG_WORK_REQUEST 1
#define TAG_WORK_ASSIGNMENT 2
#define TAG_WORK_RESULT 3
#define TAG_TERMINATE 5

// Estrutura simplificada para subproblemas
typedef struct {
    int nivel_inicio;
    int nivel_fim;
    int peso_atual;
    int valor_atual;
    int solucao_parcial[50]; // Buffer fixo para evitar malloc/free
} Subproblem;

// Gera tarefas recursivamente com profundidade limitada
static void generate_dc_tasks(const Item itens[], int n_itens, int capacidade,
                             Subproblem tasks[], int *task_count, int max_tasks,
                             int nivel, int peso, int valor, int task_depth) {
    if (*task_count >= max_tasks) return;
    
    if (nivel >= task_depth || nivel >= n_itens) {
        // Cria uma tarefa
        Subproblem *task = &tasks[*task_count];
        task->nivel_inicio = nivel;
        task->nivel_fim = n_itens - 1;
        task->peso_atual = peso;
        task->valor_atual = valor;
        memset(task->solucao_parcial, 0, 50 * sizeof(int));
        
        // Copia estado atual
        for (int i = 0; i < nivel; i++) {
            // Este será preenchido durante a geração
        }
        (*task_count)++;
        return;
    }
    
    // Não incluir item atual
    generate_dc_tasks(itens, n_itens, capacidade, tasks, task_count, max_tasks,
                     nivel + 1, peso, valor, task_depth);
    
    // Incluir item atual (se couber)
    if (peso + itens[nivel].peso <= capacidade) {
        generate_dc_tasks(itens, n_itens, capacidade, tasks, task_count, max_tasks,
                         nivel + 1, peso + itens[nivel].peso, 
                         valor + itens[nivel].valor, task_depth);
    }
}

// Força bruta sequencial para subproblemas com limite de nós
static void solve_brute_force_range(int nivel_inicio, int nivel_fim, int peso_atual, int valor_atual,
                                   const Item itens[], int n_itens, int capacidade,
                                   int *melhor_valor, int solucao_atual[], int melhor_solucao[],
                                   long long *nos_explorados) {
    (*nos_explorados)++;
    
    // Caso base: processou todos os itens do range
    if (nivel_inicio > nivel_fim) {
        if (valor_atual > *melhor_valor) {
            *melhor_valor = valor_atual;
            memcpy(melhor_solucao, solucao_atual, n_itens * sizeof(int));
        }
        return;
    }
    
    // Explora não incluir item atual
    solucao_atual[nivel_inicio] = 0;
    solve_brute_force_range(nivel_inicio + 1, nivel_fim, peso_atual, valor_atual,
                           itens, n_itens, capacidade, melhor_valor, solucao_atual, melhor_solucao,
                           nos_explorados);
    
    // Explora incluir item atual (se couber)
    if (peso_atual + itens[nivel_inicio].peso <= capacidade) {
        solucao_atual[nivel_inicio] = 1;
        solve_brute_force_range(nivel_inicio + 1, nivel_fim, 
                               peso_atual + itens[nivel_inicio].peso,
                               valor_atual + itens[nivel_inicio].valor,
                               itens, n_itens, capacidade, melhor_valor, solucao_atual, melhor_solucao,
                               nos_explorados);
    }
    
    solucao_atual[nivel_inicio] = 0; // backtrack
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    int rank, num_procs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    if (num_procs < 2) {
        if (rank == 0) {
            fprintf(stderr, "Este programa MPI requer pelo menos 2 processos.\\n");
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

        // Lê dados do arquivo
        FILE *arquivo = fopen(argv[1], "r");
        if (!arquivo) {
            perror("Erro ao abrir arquivo");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        int n_itens, capacidade;
        if (fscanf(arquivo, "%d %d", &n_itens, &capacidade) != 2) {
            fprintf(stderr, "Erro ao ler cabeçalho do arquivo\\n");
            fclose(arquivo);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        Item *itens = malloc(n_itens * sizeof(Item));
        if (!itens) {
            perror("Erro ao alocar memória para itens");
            fclose(arquivo);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        // Lê itens do arquivo
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

        // Envia dados básicos para todos os workers
        for (int i = 1; i < num_procs; i++) {
            MPI_Send(&n_itens, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
            MPI_Send(&capacidade, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
            
            // Envia itens
            for (int j = 0; j < n_itens; j++) {
                MPI_Send(&itens[j].peso, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
                MPI_Send(&itens[j].valor, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
                MPI_Send(&itens[j].razao, 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD);
                MPI_Send(&itens[j].indice_original, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
            }
        }

        // Usa estratégia similar ao MPI regular com limites de nós por tarefa
        int tasks_per_worker = 8;
        int max_tasks = (num_procs - 1) * tasks_per_worker;
        Subproblem *work_queue = malloc(max_tasks * sizeof(Subproblem));
        int work_count = 0;
        
        // Calcula profundidade baseada no número de workers
        int task_depth = 3;
        if (num_procs >= 4) task_depth = 4;
        if (num_procs >= 8) task_depth = 5;
        if (task_depth > n_itens) task_depth = n_itens;
        
        // Gera subproblemas recursivamente com profundidade limitada
        generate_dc_tasks(itens, n_itens, capacidade, work_queue, &work_count, max_tasks,
                         0, 0, 0, task_depth);
        
        // Sem limites - força bruta completa para escalabilidade real

        int melhor_valor_global = 0;
        int melhor_solucao_global[50];
        memset(melhor_solucao_global, 0, n_itens * sizeof(int));
        long long total_nos_explorados = 0;
        int workers_ativos = num_procs - 1;
        int work_index = 0;

        // Distribui trabalho inicial
        for (int i = 1; i < num_procs && work_index < work_count; i++) {
            Subproblem *work = &work_queue[work_index++];
            
            // Envia trabalho
            MPI_Send(&work->nivel_inicio, 1, MPI_INT, i, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD);
            MPI_Send(&work->nivel_fim, 1, MPI_INT, i, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD);
            MPI_Send(&work->peso_atual, 1, MPI_INT, i, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD);
            MPI_Send(&work->valor_atual, 1, MPI_INT, i, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD);
            MPI_Send(work->solucao_parcial, n_itens, MPI_INT, i, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD);
        }

        // Loop principal: gerencia resultados
        while (workers_ativos > 0) {
            MPI_Status status;
            MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            
            if (status.MPI_TAG == TAG_WORK_RESULT) {
                // Recebe resultado
                int result_data[2]; // melhor_valor, nos_explorados
                MPI_Recv(result_data, 2, MPI_INT, status.MPI_SOURCE, TAG_WORK_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                
                int solucao_recebida[50];
                MPI_Recv(solucao_recebida, n_itens, MPI_INT, status.MPI_SOURCE, TAG_WORK_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                
                total_nos_explorados += result_data[1];
                
                // Atualiza melhor solução
                if (result_data[0] > melhor_valor_global) {
                    melhor_valor_global = result_data[0];
                    memcpy(melhor_solucao_global, solucao_recebida, n_itens * sizeof(int));
                }
                
                // Envia mais trabalho se disponível
                if (work_index < work_count) {
                    Subproblem *work = &work_queue[work_index++];
                    
                    MPI_Send(&work->nivel_inicio, 1, MPI_INT, status.MPI_SOURCE, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD);
                    MPI_Send(&work->nivel_fim, 1, MPI_INT, status.MPI_SOURCE, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD);
                    MPI_Send(&work->peso_atual, 1, MPI_INT, status.MPI_SOURCE, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD);
                    MPI_Send(&work->valor_atual, 1, MPI_INT, status.MPI_SOURCE, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD);
                    MPI_Send(work->solucao_parcial, n_itens, MPI_INT, status.MPI_SOURCE, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD);
                } else {
                    // Não há mais trabalho, termina worker
                    int terminate = 0;
                    MPI_Send(&terminate, 1, MPI_INT, status.MPI_SOURCE, TAG_TERMINATE, MPI_COMM_WORLD);
                    workers_ativos--;
                }
            }
        }

        tempo_fim = MPI_Wtime();

        // Calcula estatísticas da solução
        int peso_total = 0, itens_selecionados = 0;
        for (int i = 0; i < n_itens; i++) {
            if (melhor_solucao_global[i]) {
                peso_total += itens[i].peso;
                itens_selecionados++;
            }
        }
        
        printf("MPI_DC(%d): Valor=%d, Itens=%d/%d, Peso=%d/%d, Nós=%lld, Tempo=%.3fms\\n", 
               num_procs, melhor_valor_global, itens_selecionados, n_itens, 
               peso_total, capacidade, total_nos_explorados, (tempo_fim - tempo_inicio) * 1000.0);

        // Limpeza
        free(itens);
        free(work_queue);

    } else { // PROCESSO WORKER
        
        // Recebe dados básicos
        int n_itens, capacidade;
        MPI_Recv(&n_itens, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&capacidade, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        Item *itens = malloc(n_itens * sizeof(Item));
        if (!itens) MPI_Abort(MPI_COMM_WORLD, 2);

        // Recebe itens
        for (int i = 0; i < n_itens; i++) {
            MPI_Recv(&itens[i].peso, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&itens[i].valor, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&itens[i].razao, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&itens[i].indice_original, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        
        while (1) {
            MPI_Status status;
            MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            
            if (status.MPI_TAG == TAG_TERMINATE) {
                int dummy;
                MPI_Recv(&dummy, 1, MPI_INT, 0, TAG_TERMINATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                break;
            }
            
            if (status.MPI_TAG == TAG_WORK_ASSIGNMENT) {
                // Recebe trabalho
                Subproblem work;
                MPI_Recv(&work.nivel_inicio, 1, MPI_INT, 0, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(&work.nivel_fim, 1, MPI_INT, 0, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(&work.peso_atual, 1, MPI_INT, 0, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(&work.valor_atual, 1, MPI_INT, 0, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(work.solucao_parcial, n_itens, MPI_INT, 0, TAG_WORK_ASSIGNMENT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                // Resolve usando brute force com limite de nós
                int melhor_valor = work.valor_atual;
                int melhor_solucao[50];
                memcpy(melhor_solucao, work.solucao_parcial, n_itens * sizeof(int));
                long long nos_explorados = 0;
                
                int solucao_temp[50];
                memcpy(solucao_temp, work.solucao_parcial, n_itens * sizeof(int));
                
                solve_brute_force_range(work.nivel_inicio, work.nivel_fim, work.peso_atual, work.valor_atual,
                                       itens, n_itens, capacidade, &melhor_valor, solucao_temp, 
                                       melhor_solucao, &nos_explorados);

                // Envia resultado
                int result_data[2] = {melhor_valor, (int)nos_explorados};
                MPI_Send(result_data, 2, MPI_INT, 0, TAG_WORK_RESULT, MPI_COMM_WORLD);
                MPI_Send(melhor_solucao, n_itens, MPI_INT, 0, TAG_WORK_RESULT, MPI_COMM_WORLD);
            }
        }

        free(itens);
    }

    MPI_Finalize();
    return 0;
}