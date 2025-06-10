// Archivos: servidor.c, cliente.c, Makefile, frases.txt
// Este es el nuevo servidor.c con mejoras: ranking, tiempo, señales y ayuda.

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

void mostrar_ayuda() {
    printf("\nUso: ./servidor -a ARCHIVO -c CANTIDAD_INTENTOS\n");
    printf("       o ./servidor --archivo ARCHIVO --cantidad CANTIDAD_INTENTOS\n");
    printf("\n");
    printf("Descripción:\n");
    printf("  Este programa implementa un juego donde un único cliente puede conectarse al servidor\n");
    printf("  para adivinar frases secretas dentro de un número limitado de intentos.\n");
    printf("\n");
    printf("Características del juego:\n");
    printf("  ✓ Solo se permite un cliente ejecutándose a la vez.\n");
    printf("  ✓ Solo puede haber un servidor en ejecución por computadora.\n");
    printf("  ✓ El servidor espera que un cliente se conecte para iniciar una partida.\n");
    printf("  ✓ Tanto el cliente como el servidor ignoran la señal SIGINT (Ctrl+C).\n");
    printf("  ✓ El servidor finaliza al recibir SIGUSR1 si no hay partida en curso;\n");
    printf("    de lo contrario, espera a que la partida termine para finalizar.\n");
    printf("  ✓ El servidor finaliza inmediatamente al recibir SIGUSR2;\n");
    printf("    si hay partida en curso, la finaliza y muestra los resultados.\n");
    printf("\n");
    printf("Parámetros:\n");
    printf("  -a, --archivo   Archivo de frases secretas (obligatorio).\n");
    printf("  -c, --cantidad  Cantidad de intentos permitidos por partida (obligatorio).\n");
    printf("  -h, --help      Muestra esta ayuda y termina.\n");
    printf("\n");
    printf("Ejemplo de uso:\n");
    printf("  ./servidor -a frases.txt -c 5\n");
    printf("\n");
    printf("Sugerencias:\n");
    printf("  - Antes de ejecutar el servidor, asegúrese de no tener otra instancia activa.\n");
    printf("  - Para terminar el servidor correctamente, use SIGUSR1 o SIGUSR2 según corresponda.\n");
    printf("\n");
}
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

void limpiar_recursos() {
    munmap(juego, sizeof(Juego));
    close(shm_fd);
    shm_unlink(SHM_NAME);

    sem_close(sem_client_ready);
    sem_close(sem_server_ready);
    sem_unlink(SEM_CLIENT_READY);
    sem_unlink(SEM_SERVER_READY);
}

// Función de comparación para qsort
int comparar_por_tiempo(const void *a, const void *b) {
    const Ranking *r1 = (const Ranking *)a;
    const Ranking *r2 = (const Ranking *)b;
    if (r1->tiempo < r2->tiempo) return -1;
    if (r1->tiempo > r2->tiempo) return 1;
    return 0;
}

void mostrar_ranking_final() {
    if (ranking_count > 0) {
        printf("\nRanking final:\n");
        qsort(ranking, ranking_count, sizeof(Ranking), comparar_por_tiempo);
    	
		printf("%s - %.2f segundos", ranking[0].nickname, ranking[0].tiempo);
		printf(" ***** GANADOR ***** \n");
		
		for (int i = 1; i < ranking_count; i++) {
			printf("%s - %.2f segundos\n", ranking[i].nickname, ranking[i].tiempo);
        }
    } else {
        printf("\nNingún participante logró acertar la frase.\n");
    }
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
        mostrar_ranking_final();
		limpiar_recursos();
        exit(0);
    }
}

void manejar_SIGUSR2(int sig) {
    juego->juego_terminado = 1;
    if (juego->partida_en_curso) {
        printf("\nSIGUSR2 recibido. Finalizando partida y cerrando.\n");
        juego->juego_terminado = -1;
		sem_post(sem_server_ready);
    } else {
        printf("\nSIGUSR2 recibido. Cerrando servidor.\n");
    }

    mostrar_ranking_final();
	limpiar_recursos();
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
	limpiar_recursos();
    exit(0);
}


void servidor() {
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
	if (shm_fd == -1) {
		if (errno == EEXIST) {
			fprintf(stderr, "Ya hay un servidor activo.\n");
		} else {
			perror("shm_open");
		}
		exit(1);
	}
	
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
			
            mostrar_ranking_final();
			
            printf("Finalizando servidor \n");
			limpiar_recursos();
            exit(0);
        } 
    }
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
        return 1;
    }


    if (!archivo_frases[0] || intentos_por_partida <= 0) {
        return 1;
    }

    srand(time(NULL));
    cargar_frases();
    servidor();
    return 0;
}
