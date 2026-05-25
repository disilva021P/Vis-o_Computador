#include <stdio.h>
#include "executaOpenCV.h"

// Declaração da função que está no ficheiro .cpp

int main(void) {
    printf("Sistema de Visão Computacional Iniciado...\n");

    // Chama a função que faz a ponte com o OpenCV
    processaVideo("video.avi");
    printf("Fim do programa.\n");
    return 0;
}