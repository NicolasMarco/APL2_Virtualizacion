// cliente.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>
#include <errno.h>

#define MAX_LEN 256
#define SHM_NAME "/shm_ahorcado"
#define SEM_CLIENT_READY "/sem_client_ready"
#define SEM_SERVER_READY "/sem_server_ready"

typedef struct {
    char frase_secreta[MAX_LEN];
    char frase_oculta[MAX_LEN];
    char letra;
    int intentos_restantes;
    int partida_en_curso;
    int juego_terminado;
    int acierto;
    int resultado; // 1 = ganó, 0 = perdió
    char nickname[64];
    time_t tiempo_inicio;
    time_t tiempo_fin;
} Juego;

int shm_fd;
Juego* juego;
sem_t* sem_client_ready;
sem_t* sem_server_ready;
char nickname[50];

void signal_handler(int sig) {
    
}

void sigterm_handler(int sig) {
    printf("\nRecibido SIGTERM. Cerrando limpiamente...\n");
    juego->letra = '*';
    sem_post(sem_client_ready);

    // Limpiar recursos
    munmap(juego, sizeof(Juego));
    close(shm_fd);
    sem_close(sem_client_ready);
    sem_close(sem_server_ready);

    exit(0);
}

void mostrar_ayuda() {
    printf("Uso: ./cliente -n <nickname>\n");
    printf("\t-n, --nickname\tNombre del usuario (obligatorio)\n");
    printf("\t-h, --help\t\tMuestra esta ayuda\n");
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, sigterm_handler);

    int opt;
    int flag_n = 0, flag_h = 0;
    int option_index = 0;
    static struct option long_options[] = {
        {"nickname", required_argument, 0, 'n'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "n:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'n':
                strncpy(nickname, optarg, sizeof(nickname));
                flag_n = 1;
                break;
            case 'h':
                flag_h = 1;
                break;
            default:
                mostrar_ayuda();
                return 1;
        }
    }

    if (flag_h) {
        if (flag_n) {
            fprintf(stderr, "La opción -h no puede combinarse con -n\n");
            mostrar_ayuda();
            return 1;
        }
        mostrar_ayuda();
        return 1;
    }

    if (!flag_n) {
        fprintf(stderr, "Debe ingresar un nickname.\n");
        mostrar_ayuda();
        return 1;
    }

    if (strlen(nickname) == 0) {
        fprintf(stderr, "Nickname obligatorio. Use -n <nickname>\n");
        return 1;
    }

    // Abrir memoria compartida
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }

    juego = mmap(NULL, sizeof(Juego), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (juego == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    // Abrir semáforos
    sem_client_ready = sem_open(SEM_CLIENT_READY, 0);
    if (sem_client_ready == SEM_FAILED) {
        perror("sem_open client");
        exit(EXIT_FAILURE);
    }

    sem_server_ready = sem_open(SEM_SERVER_READY, 0);
    if (sem_server_ready == SEM_FAILED) {
        perror("sem_open server");
        exit(EXIT_FAILURE);
    }

    // Verificar si hay una partida en curso
    if (juego->juego_terminado) {
        printf("No hay un servidor disponible para jugar.\n");
        return 1;
    }

    if (juego->partida_en_curso) {
        printf("Ya hay una partida en curso. Intente más tarde.\n");
        return 1;
    }

    // Configurar juego
    strcpy(juego->nickname, nickname);
    //juego->partida_en_curso = 1;
    //juego->tiempo_inicio = time(NULL);
    sem_post(sem_client_ready);

    while (1) {
        if (juego->juego_terminado == -1) {
            printf("El servidor interrumpio el juego\n");
            break;
        } else {
            printf("Esperando servidor...\n");
            sem_wait(sem_server_ready);
            printf("Servidor respondio...\n");

            if (juego->intentos_restantes == 0 || strcmp(juego->frase_secreta, juego->frase_oculta) == 0) {
                //juego->tiempo_fin = time(NULL);

                if (strcmp(juego->frase_secreta, juego->frase_oculta) == 0) {
                    printf("¡Ganaste! Frase: %s\n", juego->frase_secreta);
                } else {
                    printf("Perdiste. Frase: %s\n", juego->frase_secreta);
                }

                //juego->resultado = (strcmp(juego->frase_secreta, juego->frase_oculta) == 0);
                //juego->partida_en_curso = 0;

                //sem_post(sem_client_ready);  // Notificar al servidor que cliente terminó
                break;
            }

            printf("Frase: %s\n", juego->frase_oculta);
            printf("Intentos restantes: %d\n", juego->intentos_restantes);
            printf("Ingrese una letra: ");
            char letra;
            scanf(" %c", &letra);

            juego->letra = letra;

            sem_post(sem_client_ready);  // Notifica al servidor que envió letra
        }
        
    }

    // Limpiar recursos
    munmap(juego, sizeof(Juego));
    close(shm_fd);
    sem_close(sem_client_ready);
    sem_close(sem_server_ready);

    return 0;
}