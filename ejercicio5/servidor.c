// Archivos: servidor.c, cliente.c, Makefile, frases.txt
// Este es el nuevo servidor.c con mejoras: ranking, tiempo, señales y ayuda.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define MAX_LEN 256

typedef struct {
    char frase_secreta[MAX_LEN];
    char frase_oculta[MAX_LEN];
    char letra;
    int intentos_restantes;
    int partida_en_curso;
    int juego_terminado;
    int acierto;
    char nickname[64];
} Juego;

typedef struct {
    char nickname[64];
    int aciertos;
} Ranking;

typedef struct {
    int socket;
    Juego juego;
} Cliente;

Cliente clientes[MAX_CLIENTES];
int usuarios_conectados = 0;
pthread_mutex_t mutex_clientes = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_ranking = PTHREAD_MUTEX_INITIALIZER;


int ranking_count = 0;
char frases[100][MAX_LEN];
int total_frases = 0;
char archivo_frases[MAX_LEN];
int terminarServidor = 0;

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

void actualizar_oculta(const char *frase, char *oculta, char letra, int *acierto) {
    *acierto = 0;
    for (int i = 0; frase[i]; i++) {
        if (frase[i] == letra && oculta[i] == '_') {
            oculta[i] = letra;
            *acierto = 1;
        }
    }
}

void sigint_handler(int sig) {
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

void atender_cliente(void* arg) {
    Cliente* cliente = (Cliente*)arg;
    Juego* juego = &cliente->juego;
    char buffer[512];
    char nuevaRonda;
    int letraInvalida = 0;

    // Iniciar juego
    juego->intentos_restantes = 10;
    juego->partida_en_curso = 1;

    int idx = rand() % total_frases;
    strncpy(juego->frase_secreta, frases[idx], MAX_LEN);
    ocultar_frase(juego->frase_secreta, juego->frase_oculta);

    ranking[ranking_count].aciertos = 0;

    while (juego->partida_en_curso) {
        if (!terminarServidor) {
            // Enviar estado actual
            snprintf(buffer, sizeof(buffer), "Frase: %s\nIntentos restantes: %d\nIngrese letra: ",
                juego->frase_oculta, juego->intentos_restantes);
            send(cliente->socket, buffer, strlen(buffer), 0);

            // Esperar letra
            int n = recv(cliente->socket, &juego->letra, 1, 0);
            if (n <= 0) {
                printf("Cliente %s desconectado.\n", juego->nickname);
                break;
            }
            
            actualizar_oculta(juego->frase_secreta, juego->frase_oculta, juego->letra, &juego->acierto);
            if (!juego->acierto) juego->intentos_restantes--;
            
            if (strcmp(juego->frase_oculta, juego->frase_secreta) == 0 || juego->intentos_restantes <= 0) {
                juego->partida_en_curso = 0;
                if (strcmp(juego->frase_oculta, juego->frase_secreta) == 0) {
                    strcpy(buffer, "Ganaste!\n");
                    send(cliente->socket, buffer, strlen(buffer), 0);
                    pthread_mutex_lock(&mutex_ranking);
                    strcpy(ranking[ranking_count].nickname, juego->nickname);
                    ranking[ranking_count++].aciertos++;
                    pthread_mutex_unlock(&mutex_ranking);
                } else {
                    strcpy(buffer, "Perdiste!\n");
                    send(cliente->socket, buffer, strlen(buffer), 0);
                    strcpy(ranking[ranking_count].nickname, juego->nickname);
                }

                do {
                    if (!letraInvalida) {
                        snprintf(buffer, sizeof(buffer), "\nQueres volver a jugar? S/N: ");
                        send(cliente->socket, buffer, strlen(buffer), 0);

                        // Esperar respuesta
                        int n = recv(cliente->socket, &nuevaRonda, 1, 0);
                        if (n <= 0) {
                            printf("Cliente %s desconectado.\n", juego->nickname);
                            break;
                        } else {
                            if (nuevaRonda == 's' || nuevaRonda == 'S') {
                                //Inicializamos con nueva frase
                                letraInvalida = 1;
                                idx = rand() % total_frases;
                                strncpy(juego->frase_secreta, frases[idx], MAX_LEN);
                                ocultar_frase(juego->frase_secreta, juego->frase_oculta);
                                juego->intentos_restantes = 10;
                                juego->partida_en_curso = 1;
                            } else if (nuevaRonda == 'n' || nuevaRonda == 'N') {
                                snprintf(buffer, sizeof(buffer), "\nGracias por jugar! El ranking quedo asi hasta el momento:\n");
                                send(cliente->socket, buffer, strlen(buffer), 0);
                                for (int i = 0; i < ranking_count; i++) {
                                    snprintf(buffer, sizeof(buffer), "%s - %d aciertos\n", ranking[i].nickname, ranking[i].aciertos);
                                    send(cliente->socket, buffer, strlen(buffer), 0);
                                }
                            } else {
                                letraInvalida = 1;
                            }
                        }
                    } else {
                        snprintf(buffer, sizeof(buffer), "\nLETRA INVALIDA. Queres volver a jugar? S/N: ");
                        send(cliente->socket, buffer, strlen(buffer), 0);
                        letraInvalida = 0;
                    }     
                } while (nuevaRonda != 's' && nuevaRonda != 'S' && nuevaRonda != 'n' && nuevaRonda != 'N');
            }

        } else {
            if (ranking_count > 0) {
                printf("\nRanking final:\n");
                for (int i = 0; i < ranking_count; i++) {
                    printf("%s - %d segundos\n", ranking[i].nickname, ranking[i].aciertos);
                }
            } else {
                printf("\nNingun participante logro acertar la frase\n");
            }
            printf("Finalizando servidor \n");

            //LOGICA PARA CERRAR CONEXION CON EL CLIENTE DE FORMA ARMONIOSA INFORMANDOSELO
            pthread_exit(NULL);.
            exit(0);
        }
    }

    printf("Partida finalizada con %s\n", juego->nickname);
}

void mostrar_ayuda() {
    printf("Uso: ./servidor -a ARCHIVO -c INTENTOS\n");
}

int main(int argc, char *argv[]) {
    int opt;
    int flag_a = 0, flag_p = 0, flag_u = 0, flag_h = 0;
    int puerto;
    int max_usuarios;
    
    static struct option long_options[] = {
        {"archivo",  required_argument, 0, 'a'},
        {"puerto", required_argument, 0, 'p'},
        {"usuarios", required_argument, 0, 'u'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0} // Fin del array
    };

    while ((opt = getopt_long(argc, argv, "a:c:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a':
                strncpy(archivo_frases, optarg, MAX_LEN);
                flag_a = 1;
                break;
            case 'p':
                puerto = atoi(optarg);
                flag_p = 1;
                break;
            case 'u':
                max_usuarios = atoi(optarg);
                flag_u = 1;
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
        if (flag_a || flag_p || flag_u) {
            fprintf(stderr, "La opción -h no puede combinarse con -a ni -c\n");
            mostrar_ayuda();
            return 1;
        }
        mostrar_ayuda();
        return 0;
    }

    if (!flag_a || !flag_p || !flag_u) {
        fprintf(stderr, "Faltan opciones obligatorias -a, -p, -u\n");
        mostrar_ayuda();
        return 1;
    }

    if (!archivo_frases[0] || intentos_por_partida <= 0) {
        mostrar_ayuda();
        return 1;
    }

    Ranking ranking[max_usuarios];
    struct sockaddr_in servidor;
    servidor.sin_family = AF_INET;
    servidor.sin_addr.s_addr = INADDR_ANY;
    servidor.sin_port = htons(puerto);

    if (bind(sockfd, (struct sockaddr*)&servidor, sizeof(servidor)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(sockfd, max_usuarios) < 0) {
        perror("listen");
        return 1;
    }

    cargar_frases();
    printf("Servidor escuchando en puerto %d...\n", puerto);
    
    while (1) {
        if (usuarios_conectados < max_usuarios) {
            struct sockaddr_in cliente_addr;
            socklen_t addr_len = sizeof(cliente_addr);
            int cliente_fd = accept(sockfd, (struct sockaddr*)&cliente_addr, &addr_len);
            if (cliente_fd < 0) {
                perror("accept");
                continue;
            }
            
            pthread_mutex_lock(&mutex_clientes);
            int idx = usuarios_conectados;
            usuarios_conectados++;
            pthread_mutex_unlock(&mutex_clientes);

            clientes[idx].socket = cliente_fd;

            ssize_t bytes_recibidos = recv(cliente_fd, clientes[idx].juego.nickname,
                                   sizeof(clientes[idx].juego.nickname), 0);
            if (bytes_recibidos <= 0) {
                perror("recv nickname");
                close(cliente_fd);
                pthread_mutex_lock(&mutex_clientes);
                usuarios_conectados--;
                pthread_mutex_unlock(&mutex_clientes);
                continue;
            }

            printf("Cliente %s conectado.\n", clientes[idx].juego.nickname);

            pthread_t hilo;
            pthread_create(&hilo, NULL, atender_cliente, &clientes[idx]);
            pthread_detach(hilo);
        }
        else {
            sleep(1);
        }
    }

    close(sockfd);
    return 0;
}
