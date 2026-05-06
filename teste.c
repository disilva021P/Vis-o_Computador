#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

// 1. Definição da Estrutura
typedef struct {
    unsigned char *frame;
    pthread_mutex_t lock;
    pthread_cond_t can_process;
    int novo_frame_pronto;
    int rodando;
} SharedData;

// 2. Thread de Captura (Produtor)
void* capturar(void* arg) {
    SharedData *data = (SharedData*)arg;
    
    while(data->rodando) {
        // Simula captura de hardware
        pthread_mutex_lock(&data->lock);
        
        // Aqui você leria da câmera. Exemplo simbólico:
        data->frame[0] = rand() % 256; 
        printf("[Captura] Frame capturado!\n");
        
        data->novo_frame_pronto = 1;
        
        // Avisa a outra thread e libera o cadeado
        pthread_cond_signal(&data->can_process);
        pthread_mutex_unlock(&data->lock);
        
        usleep(33000); // Simula 30 FPS
    }
    return NULL;
}

// 3. Thread de Processamento (Consumidor)
void* processar(void* arg) {
    SharedData *data = (SharedData*)arg;
    
    while(data->rodando) {
        pthread_mutex_lock(&data->lock);
        
        // Espera até que um novo frame esteja pronto
        while(!data->novo_frame_pronto && data->rodando) {
            pthread_cond_wait(&data->can_process, &data->lock);
        }
        
        if(!data->rodando) {
            pthread_mutex_unlock(&data->lock);
            break;
        }

        // --- SEU ALGORITMO AQUI ---
        printf("[Processador] Segmentando frame: %d\n", data->frame[0]);
        // --------------------------

        data->novo_frame_pronto = 0;
        pthread_mutex_unlock(&data->lock);
    }
    return NULL;
}

int main() {
    SharedData data;
    
    // Inicialização manual obrigatória em C
    data.frame = malloc(1024 * 1024); // Aloca 1MB para exemplo
    data.rodando = 1;
    data.novo_frame_pronto = 0;
    pthread_mutex_init(&data.lock, NULL);
    pthread_cond_init(&data.can_process, NULL);

    pthread_t t1, t2;

    // Criando as threads
    pthread_create(&t1, NULL, capturar, &data);
    pthread_create(&t2, NULL, processar, &data);

    // Roda por 5 segundos e para
    sleep(5);
    data.rodando = 0;
    pthread_cond_broadcast(&data.can_process); // Acorda quem estiver dormindo para fechar

    // Limpeza
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_mutex_destroy(&data.lock);
    pthread_cond_destroy(&data.can_process);
    free(data.frame);

    printf("Programa finalizado com sucesso.\n");
    return 0;
}