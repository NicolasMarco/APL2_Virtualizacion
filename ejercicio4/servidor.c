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

#define SHM_NAME "/shm_ahorcado"
#define SEM_CLIENT_READY "/sem_client_ready"
#define SEM_SERVER_READY "/sem_server_ready"
#define MAX_LEN 256
#define MAX_PLAYERS 100

typedef struct {
    char frase_secreta[MAX_LEN];
    char frase_oculta[MAX_LEN];
    char letra;
    int intentos_restantes;
    int partida_en_curso;
    int juego_terminado;
    int acierto;
    int resultado;
    char nickname[64];
    time_t tiempo_inicio;
    time_t tiempo_fin;
} Juego;

typedef struct {
    char nickname[64];
    double tiempo;
} Ranking;


Juego *juego;
Ranking ranking[MAX_PLAYERS];
int ranking_count = 0;
int intentos_por_partida = 5;
char frases[100][MAX_LEN];
int total_frases = 0;
char archivo_frases[MAX_LEN];
int terminarServidor = 0;


sem_t *sem_client_ready, *sem_server_ready;
int shm_fd;

void cargar_frases() {
    FILE *file = fopen(archivo_frases, "r");
    if (!file) {
        perror("No se pudo abrir el archivo de frases");
        exit(1);
    }
    while (fgets(frases[total_frases], MAX_LEN, file)) {
        frases[total_frases][strcspn(frases[total_frases], "\r\n")] = 0;
        total_frases++;
    }
    fclose(file);
}

void ocultar_frase(const char *frase, char *oculta) {
    for (int i = 0; frase[i]; i++)
        oculta[i] = frase[i] == ' ' ? ' ' : '_';
    oculta[strlen(frase)] = 0;
}

int actualizar_oculta(const char *frase, char *oculta, char letra, int *acierto) {
    if (letra == '*') {
        return 1;
    } else {
        *acierto = 0;
        for (int i = 0; frase[i]; i++) {
            if (frase[i] == letra && oculta[i] == '_') {
                oculta[i] = letra;
                *acierto = 1;
            }
        }
    }
    return 0;
}

void manejar_SIGUSR1(int sig) {
    if (juego->partida_en_curso) {
        printf("\nSIGUSR1 recibido. Esperando fin de la partida actual...\n");
        terminarServidor = 1;
    } else {
        juego->juego_terminado = 1;
        printf("\nSIGUSR1 recibido. Cerrando servidor.\n");
        if (ranking_count > 0) {
            printf("\nRanking final:\n");
            for (int i = 0; i < ranking_count; i++) {
                printf("%s - %.2f segundos\n", ranking[i].nickname, ranking[i].tiempo);
            }
        } else {
            printf("\nNingun participante logro acertar la frase\n");
        }
        exit(0);
    }
}

void manejar_SIGUSR2(int sig) {
    juego->juego_terminado = 1;
    if (juego->partida_en_curso) {
        printf("\nSIGUSR2 recibido. Finalizando partida y cerrando.\n");
        juego->juego_terminado = -1;
    } else {
        printf("\nSIGUSR2 recibido. Cerrando servidor.\n");
    }

    if (ranking_count > 0) {
            printf("\nRanking final:\n");
            for (int i = 0; i < ranking_count; i++) {
                printf("%s - %.2f segundos\n", ranking[i].nickname, ranking[i].tiempo);
            }
        } else {
            printf("\nNingun participante logro acertar la frase\n");
        }

    exit(0);
}

void sigint_handler(int sig) {
    // Ignorar Ctrl+C
}

void sigterm_handler(int sig) {
    printf("\nRecibido SIGTERM. Cerrando limpiamente...\n");

    if (juego->partida_en_curso) {
        juego->juego_terminado = -1;
    } 

    exit(0);
}

void servidor() {
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(Juego));
    juego = mmap(NULL, sizeof(Juego), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    juego->partida_en_curso = 0;
    juego->juego_terminado = 0;

    sem_unlink(SEM_CLIENT_READY);
    sem_unlink(SEM_SERVER_READY);

    sem_client_ready = sem_open(SEM_CLIENT_READY, O_CREAT, 0666, 0);
    sem_server_ready = sem_open(SEM_SERVER_READY, O_CREAT, 0666, 0);
    pid_t pid = getpid();
    printf("Servidor iniciado. PID: %d\n", pid);
    
    while (1) {
        if (!terminarServidor) {
            printf("Esperando cliente...\n");
            sem_wait(sem_client_ready);
            printf("Llego Cliente...\n");

            int idx = rand() % total_frases;
            strncpy(juego->frase_secreta, frases[idx], MAX_LEN);
            ocultar_frase(juego->frase_secreta, juego->frase_oculta);
            juego->intentos_restantes = intentos_por_partida;
            juego->partida_en_curso = 1;
            juego->juego_terminado = 0;
            juego->tiempo_inicio = time(NULL);

            sem_post(sem_server_ready);

            while (juego->partida_en_curso) {
                sem_wait(sem_client_ready);
                if (!actualizar_oculta(juego->frase_secreta, juego->frase_oculta, juego->letra, &juego->acierto)) {
                    if (!juego->acierto) juego->intentos_restantes--;
                    if (strcmp(juego->frase_oculta, juego->frase_secreta) == 0 || juego->intentos_restantes <= 0) {
                        juego->partida_en_curso = 0;
                        juego->tiempo_fin = time(NULL);
                        if (strcmp(juego->frase_oculta, juego->frase_secreta) == 0) {
                            juego->resultado = 1;
                            double duracion = difftime(juego->tiempo_fin, juego->tiempo_inicio);
                            strcpy(ranking[ranking_count].nickname, juego->nickname);
                            ranking[ranking_count++].tiempo = duracion;
                        } else {
                            juego->resultado = 0;
                        }
                    }
                    sem_post(sem_server_ready);
                } else {
                    juego->partida_en_curso = 0;
                }
            }

            printf("Partida finalizada con %s\n", juego->nickname);
        } else {
            juego->juego_terminado = 1;
            if (ranking_count > 0) {
                printf("\nRanking final:\n");
                for (int i = 0; i < ranking_count; i++) {
                    printf("%s - %.2f segundos\n", ranking[i].nickname, ranking[i].tiempo);
                }
            } else {
                printf("\nNingun participante logro acertar la frase\n");
            }
            printf("Finalizando servidor \n");
            exit(0);
        } 
    }
}

void mostrar_ayuda() {
    printf("Uso: ./servidor -a ARCHIVO -c INTENTOS\n");
}

int main(int argc, char *argv[]) {
    int opt;
    int flag_a = 0, flag_c = 0, flag_h = 0;
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigterm_handler);
    signal(SIGUSR1, manejar_SIGUSR1);
    signal(SIGUSR2, manejar_SIGUSR2);

    static struct option long_options[] = {
        {"archivo",  required_argument, 0, 'a'},
        {"cantidad", required_argument, 0, 'c'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0} // Fin del array
    };

    while ((opt = getopt_long(argc, argv, "a:c:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a':
                strncpy(archivo_frases, optarg, MAX_LEN);
                flag_a = 1;
                break;
            case 'c':
                intentos_por_partida = atoi(optarg);
                flag_c = 1;
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
        if (flag_a || flag_c) {
            fprintf(stderr, "La opción -h no puede combinarse con -a ni -c\n");
            mostrar_ayuda();
            return 1;
        }
        mostrar_ayuda();
        return 0;
    }

    if (!flag_a || !flag_c) {
        fprintf(stderr, "Faltan opciones obligatorias -a y/o -c\n");
        mostrar_ayuda();
        return 1;
    }

    /*while ((opt = getopt(argc, argv, "a:c:h")) != -1) {
        switch (opt) {
            case 'a': strncpy(archivo_frases, optarg, MAX_LEN); break;
            case 'c': intentos_por_partida = atoi(optarg); break;
            case 'h': mostrar_ayuda(); exit(0);
            default: mostrar_ayuda(); exit(1);
        }
    }*/

    if (!archivo_frases[0] || intentos_por_partida <= 0) {
        mostrar_ayuda();
        return 1;
    }

    srand(time(NULL));
    cargar_frases();
    servidor();
    return 0;
}
