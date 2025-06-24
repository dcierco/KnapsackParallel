#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>
#include "knapsack_common.h"

// Tags para comunicação MPI
#define TAG_ITEM_DATA 0
#define TAG_TASK 1
#define TAG_RESULT 2
#define TAG_BEST_UPDATE 3
#define TAG_TERMINATE 4

// Estrutura para tarefas (subproblemas)
typedef struct {
    int nivel_inicio;
    int peso_atual;
    int valor_atual;
    int *solucao_parcial;  // Será alocado dinamicamente
    int valid;
} Task;

// Estrutura para resultados
typedef struct {
    int valor;
    int *solucao;  // Será alocado dinamicamente
    int worker_id;
    int task_explored;  // Número de nós explorados nesta tarefa
} Result;

// Estrutura para atualização do melhor valor global
typedef struct {
    int melhor_valor;
    int *melhor_solucao;  // Será alocado dinamicamente
} BestUpdate;

// Função auxiliar para gerar tarefas recursivamente
static void generate_recursive_tasks(const Item itens[], int n_itens, int capacidade,
                                    Task tasks[], int *task_count, int max_tasks,
                                    int nivel, int peso, int valor, int *solucao_parcial,
                                    int task_depth) {
    if (*task_count >= max_tasks) return;
    
    if (nivel >= task_depth || nivel >= n_itens) {
        // Cria uma tarefa
        tasks[*task_count].nivel_inicio = nivel;
        tasks[*task_count].peso_atual = peso;
        tasks[*task_count].valor_atual = valor;
        tasks[*task_count].solucao_parcial = malloc(n_itens * sizeof(int));
        memcpy(tasks[*task_count].solucao_parcial, solucao_parcial, n_itens * sizeof(int));
        tasks[*task_count].valid = 1;
        (*task_count)++;
        return;
    }
    
    // Ramo: não incluir item atual
    solucao_parcial[nivel] = 0;
    generate_recursive_tasks(itens, n_itens, capacidade, tasks, task_count, max_tasks,
                           nivel + 1, peso, valor, solucao_parcial, task_depth);
    
    // Ramo: incluir item atual (se couber)
    if (peso + itens[nivel].peso <= capacidade) {
        solucao_parcial[nivel] = 1;
        generate_recursive_tasks(itens, n_itens, capacidade, tasks, task_count, max_tasks,
                               nivel + 1, peso + itens[nivel].peso, 
                               valor + itens[nivel].valor, solucao_parcial, task_depth);
    }
    
    solucao_parcial[nivel] = 0; // backtrack
}

// Função para gerar tarefas de forma mais inteligente e garantida
static void generate_balanced_tasks(const Item itens[], int n_itens, int capacidade,
                                   Task tasks[], int *task_count, int max_tasks, 
                                   int target_workers) {
    *task_count = 0;
    
    // Garante que sempre temos pelo menos tantas tarefas quanto workers
    int min_tasks = target_workers * 2;
    
    // Calcula profundidade baseada no número de workers e tamanho do problema
    int task_depth = 3;  // Começar mais baixo para garantir tarefas
    if (target_workers >= 4) task_depth = 4;
    if (target_workers >= 8) task_depth = 5;
    if (n_itens > 10000) task_depth++;
    
    // Limita a profundidade
    if (task_depth > 8) task_depth = 8;
    if (task_depth > n_itens) task_depth = n_itens;
    
    // Debug: printf("Gerando tarefas com profundidade %d para %d workers\n", task_depth, target_workers);
    
    // Método simples e garantido de gerar tarefas
    int *solucao_temp = calloc(n_itens, sizeof(int));
    generate_recursive_tasks(itens, n_itens, capacidade, tasks, task_count, max_tasks,
                           0, 0, 0, solucao_temp, task_depth);
    free(solucao_temp);
    
    // Se ainda não temos tarefas suficientes, cria tarefas simples
    if (*task_count < min_tasks) {
        // Debug: printf("Criando tarefas adicionais simples...\n");
        for (int i = *task_count; i < min_tasks && i < max_tasks; i++) {
            tasks[i].nivel_inicio = i % (task_depth + 1);
            tasks[i].peso_atual = 0;
            tasks[i].valor_atual = 0;
            tasks[i].solucao_parcial = calloc(n_itens, sizeof(int));
            tasks[i].valid = 1;
            (*task_count)++;
        }
    }
    
    // Debug: printf("Geradas %d tarefas balanceadas\n", *task_count);
}

// Versão worker de força bruta
static int brute_force_worker(int nivel, int peso_atual, int valor_atual,
                             const Item itens[], int n_itens, int capacidade,
                             int *melhor_valor, int solucao_atual[], int melhor_solucao[],
                             int *nos_explorados) {
    (*nos_explorados)++;
    
    // Caso base: todos os itens foram considerados
    if (nivel == n_itens) {
        // Atualiza melhor solução se necessário
        if (valor_atual > *melhor_valor) {
            *melhor_valor = valor_atual;
            memcpy(melhor_solucao, solucao_atual, n_itens * sizeof(int));
        }
        return *nos_explorados;
    }
    
    // Explora ramo não incluindo item atual
    solucao_atual[nivel] = 0;
    brute_force_worker(nivel + 1, peso_atual, valor_atual,
                      itens, n_itens, capacidade, melhor_valor, 
                      solucao_atual, melhor_solucao, nos_explorados);
    
    // Explora ramo incluindo item atual (se couber)
    if (peso_atual + itens[nivel].peso <= capacidade) {
        solucao_atual[nivel] = 1;
        brute_force_worker(nivel + 1, peso_atual + itens[nivel].peso,
                          valor_atual + itens[nivel].valor,
                          itens, n_itens, capacidade, melhor_valor, 
                          solucao_atual, melhor_solucao, nos_explorados);
    }
    
    solucao_atual[nivel] = 0;  // Backtrack
    return *nos_explorados;
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    int rank, num_procs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    if (num_procs < 2) {
        if (rank == 0) {
            fprintf(stderr, "Este programa MPI requer pelo menos 2 processos (1 mestre + workers).\n");
        }
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    double tempo_inicio, tempo_fim;

    if (rank == 0) { // PROCESSO MESTRE
        if (argc < 2) {
            fprintf(stderr, "Uso: %s <arquivo_de_entrada>\n", argv[0]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        // Debug: printf("=== Solucionador MPI Knapsack (Força Bruta Mestre-Escravo) ===\n");
        // Debug: printf("Processos: %d (1 mestre + %d workers)\n", num_procs, num_procs - 1);

        // Lê dados do arquivo
        FILE *arquivo = fopen(argv[1], "r");
        if (!arquivo) {
            perror("Erro ao abrir arquivo");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        int n_itens, capacidade;
        if (fscanf(arquivo, "%d %d", &n_itens, &capacidade) != 2) {
            fprintf(stderr, "Erro ao ler cabeçalho do arquivo\n");
            fclose(arquivo);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        // Debug: printf("Problema: %d itens, capacidade %d\n", n_itens, capacidade);

        Item *itens = malloc(n_itens * sizeof(Item));
        if (!itens) {
            perror("Erro ao alocar memória para itens");
            fclose(arquivo);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        // Lê itens do arquivo
        for (int i = 0; i < n_itens; i++) {
            if (fscanf(arquivo, "%d %d", &itens[i].valor, &itens[i].peso) != 2) {
                fprintf(stderr, "Erro ao ler item %d\n", i);
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

        // Para força bruta, não precisamos ordenar os itens
        // Inicializa com valor 0
        int valor_guloso = 0;

        // Gera tarefas balanceadas - mais tarefas para datasets maiores
        int tasks_per_worker = 8;
        if (n_itens > 15000) tasks_per_worker = 12;
        if (n_itens > 30000) tasks_per_worker = 16;
        
        int max_tasks = (num_procs - 1) * tasks_per_worker;
        Task *tasks = malloc(max_tasks * sizeof(Task));
        int task_count = 0;
        
        generate_balanced_tasks(itens, n_itens, capacidade, tasks, &task_count, max_tasks, num_procs - 1);

        tempo_inicio = MPI_Wtime();

        // Envia dados básicos para todos os workers
        for (int i = 1; i < num_procs; i++) {
            MPI_Send(&n_itens, 1, MPI_INT, i, TAG_ITEM_DATA, MPI_COMM_WORLD);
            MPI_Send(&capacidade, 1, MPI_INT, i, TAG_ITEM_DATA, MPI_COMM_WORLD);
            
            // Envia itens um por um para evitar problemas com tipos customizados
            for (int j = 0; j < n_itens; j++) {
                MPI_Send(&itens[j].peso, 1, MPI_INT, i, TAG_ITEM_DATA, MPI_COMM_WORLD);
                MPI_Send(&itens[j].valor, 1, MPI_INT, i, TAG_ITEM_DATA, MPI_COMM_WORLD);
                MPI_Send(&itens[j].razao, 1, MPI_DOUBLE, i, TAG_ITEM_DATA, MPI_COMM_WORLD);
                MPI_Send(&itens[j].indice_original, 1, MPI_INT, i, TAG_ITEM_DATA, MPI_COMM_WORLD);
            }
        }

        // Distribuição dinâmica de tarefas
        int task_idx = 0;
        int workers_ativos = 0;
        int melhor_valor_global = valor_guloso;
        int *melhor_solucao_global = calloc(n_itens, sizeof(int));

        // Envia tarefas iniciais
        for (int i = 1; i < num_procs && task_idx < task_count; i++) {
            // Envia dados da tarefa
            MPI_Send(&tasks[task_idx].nivel_inicio, 1, MPI_INT, i, TAG_TASK, MPI_COMM_WORLD);
            MPI_Send(&tasks[task_idx].peso_atual, 1, MPI_INT, i, TAG_TASK, MPI_COMM_WORLD);
            MPI_Send(&tasks[task_idx].valor_atual, 1, MPI_INT, i, TAG_TASK, MPI_COMM_WORLD);
            MPI_Send(tasks[task_idx].solucao_parcial, n_itens, MPI_INT, i, TAG_TASK, MPI_COMM_WORLD);
            MPI_Send(&tasks[task_idx].valid, 1, MPI_INT, i, TAG_TASK, MPI_COMM_WORLD);
            
            workers_ativos++;
            task_idx++;
        }

        // Loop principal: coleta resultados e distribui novas tarefas
        int total_nos_explorados = 0;
        while (workers_ativos > 0) {
            MPI_Status status;
            int result_size[3];  // valor, worker_id, task_explored
            
            // Recebe resultado
            MPI_Recv(result_size, 3, MPI_INT, MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &status);
            int worker_id = status.MPI_SOURCE;
            
            int *solucao_recebida = malloc(n_itens * sizeof(int));
            MPI_Recv(solucao_recebida, n_itens, MPI_INT, worker_id, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            total_nos_explorados += result_size[2];  // task_explored
            
            // Atualiza melhor solução se necessário
            if (result_size[0] > melhor_valor_global) {
                melhor_valor_global = result_size[0];
                memcpy(melhor_solucao_global, solucao_recebida, n_itens * sizeof(int));
                // Debug: printf("Novo melhor valor: %d (worker %d, nós explorados: %d)\n", 
                //        melhor_valor_global, result_size[1], total_nos_explorados);
                
                // Informa todos os workers sobre o novo melhor valor
                for (int i = 1; i < num_procs; i++) {
                    if (i != worker_id) {  // Não precisa informar quem achou
                        MPI_Send(&melhor_valor_global, 1, MPI_INT, i, TAG_BEST_UPDATE, MPI_COMM_WORLD);
                        MPI_Send(melhor_solucao_global, n_itens, MPI_INT, i, TAG_BEST_UPDATE, MPI_COMM_WORLD);
                    }
                }
            }
            
            free(solucao_recebida);

            // Envia próxima tarefa ou sinal de término
            if (task_idx < task_count) {
                // Ainda há tarefas disponíveis
                MPI_Send(&tasks[task_idx].nivel_inicio, 1, MPI_INT, worker_id, TAG_TASK, MPI_COMM_WORLD);
                MPI_Send(&tasks[task_idx].peso_atual, 1, MPI_INT, worker_id, TAG_TASK, MPI_COMM_WORLD);
                MPI_Send(&tasks[task_idx].valor_atual, 1, MPI_INT, worker_id, TAG_TASK, MPI_COMM_WORLD);
                MPI_Send(tasks[task_idx].solucao_parcial, n_itens, MPI_INT, worker_id, TAG_TASK, MPI_COMM_WORLD);
                MPI_Send(&tasks[task_idx].valid, 1, MPI_INT, worker_id, TAG_TASK, MPI_COMM_WORLD);
                task_idx++;
            } else {
                // Não há mais tarefas, termina este worker
                int invalid = 0;
                MPI_Send(&invalid, 1, MPI_INT, worker_id, TAG_TERMINATE, MPI_COMM_WORLD);
                workers_ativos--;
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
        
        // Resultados essenciais
        printf("MPI(%d): Valor=%d, Itens=%d/%d, Peso=%d/%d, Nós=%d, Tempo=%.3fms\n", 
               num_procs, melhor_valor_global, itens_selecionados, n_itens, 
               peso_total, capacidade, total_nos_explorados, (tempo_fim - tempo_inicio) * 1000.0);

        // Limpeza
        for (int i = 0; i < task_count; i++) {
            free(tasks[i].solucao_parcial);
        }
        free(tasks);
        free(itens);
        free(melhor_solucao_global);

    } else { // PROCESSO WORKER
        
        // Recebe dados básicos
        int n_itens, capacidade;
        MPI_Recv(&n_itens, 1, MPI_INT, 0, TAG_ITEM_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&capacidade, 1, MPI_INT, 0, TAG_ITEM_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        Item *itens = malloc(n_itens * sizeof(Item));
        if (!itens) MPI_Abort(MPI_COMM_WORLD, 2);

        // Recebe itens
        for (int i = 0; i < n_itens; i++) {
            MPI_Recv(&itens[i].peso, 1, MPI_INT, 0, TAG_ITEM_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&itens[i].valor, 1, MPI_INT, 0, TAG_ITEM_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&itens[i].razao, 1, MPI_DOUBLE, 0, TAG_ITEM_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&itens[i].indice_original, 1, MPI_INT, 0, TAG_ITEM_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }

        int *solucao_atual = calloc(n_itens, sizeof(int));
        int *melhor_solucao = calloc(n_itens, sizeof(int));
        int melhor_valor_local = 0;

        // Sem limites - força bruta completa para escalabilidade real
        
        while (1) {
            MPI_Status status;
            MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            
            if (status.MPI_TAG == TAG_TERMINATE) {
                int dummy;
                MPI_Recv(&dummy, 1, MPI_INT, 0, TAG_TERMINATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                break;  // Termina o worker
            }
            
            if (status.MPI_TAG == TAG_BEST_UPDATE) {
                // Atualização do melhor valor global
                int novo_melhor;
                MPI_Recv(&novo_melhor, 1, MPI_INT, 0, TAG_BEST_UPDATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(melhor_solucao, n_itens, MPI_INT, 0, TAG_BEST_UPDATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                if (novo_melhor > melhor_valor_local) {
                    melhor_valor_local = novo_melhor;
                }
                continue;
            }
            
            if (status.MPI_TAG == TAG_TASK) {
                // Nova tarefa
                Task task;
                task.solucao_parcial = malloc(n_itens * sizeof(int));
                
                MPI_Recv(&task.nivel_inicio, 1, MPI_INT, 0, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(&task.peso_atual, 1, MPI_INT, 0, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(&task.valor_atual, 1, MPI_INT, 0, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(task.solucao_parcial, n_itens, MPI_INT, 0, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Recv(&task.valid, 1, MPI_INT, 0, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                if (!task.valid) {
                    free(task.solucao_parcial);
                    break;  // Não há mais tarefas
                }

                // Inicializa solução com prefixo da tarefa
                memcpy(solucao_atual, task.solucao_parcial, n_itens * sizeof(int));
                if (task.valor_atual > melhor_valor_local) {
                    melhor_valor_local = task.valor_atual;
                    memcpy(melhor_solucao, solucao_atual, n_itens * sizeof(int));
                }

                // Resolve subproblema com limite de nós usando força bruta
                int nos_explorados = 0;
                brute_force_worker(task.nivel_inicio, task.peso_atual, task.valor_atual,
                                  itens, n_itens, capacidade, &melhor_valor_local,
                                  solucao_atual, melhor_solucao, &nos_explorados);

                // Envia resultado
                int result_data[3] = {melhor_valor_local, rank, nos_explorados};
                MPI_Send(result_data, 3, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
                MPI_Send(melhor_solucao, n_itens, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);

                free(task.solucao_parcial);
            }
        }

        free(itens);
        free(solucao_atual);
        free(melhor_solucao);
    }

    MPI_Finalize();
    return 0;
}