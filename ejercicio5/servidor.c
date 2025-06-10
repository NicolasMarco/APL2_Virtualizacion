#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <getopt.h>
#include <netdb.h>

#define MAX_LEN 256
#define MAX_CLIENTES 100

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

Ranking ranking[MAX_CLIENTES];
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

void actualizar_ranking(const char* nickname, int gano) {
    pthread_mutex_lock(&mutex_ranking);
    int encontrado = 0;
    for (int i = 0; i < ranking_count; i++) {
        if (strcmp(ranking[i].nickname, nickname) == 0) {
            if (gano) ranking[i].aciertos++;
            encontrado = 1;
            break;
        }
    }
    if (!encontrado) {
        strcpy(ranking[ranking_count].nickname, nickname);
        ranking[ranking_count].aciertos = gano ? 1 : 0;
        ranking_count++;
    }
    pthread_mutex_unlock(&mutex_ranking);
}

int comparar_ranking(const void *a, const void *b) {
    return ((Ranking*)b)->aciertos - ((Ranking*)a)->aciertos;
}

void ordenar_ranking() {
    pthread_mutex_lock(&mutex_ranking);
    qsort(ranking, ranking_count, sizeof(Ranking), comparar_ranking);
    pthread_mutex_unlock(&mutex_ranking);
}

void sigint_handler(int sig) {
    printf("\nServidor detenido. Notificando a los clientes...\n");

    for (int i = 0; i < usuarios_conectados; i++) {
        send(clientes[i].socket, "Servidor cerrado\n", 18, 0);
        close(clientes[i].socket);
    }

    if (ranking_count > 0) {
        ordenar_ranking();
        printf("\nRanking final:\n");
        for (int i = 0; i < ranking_count; i++) {
            printf("%s - %d aciertos\n", ranking[i].nickname, ranking[i].aciertos);
        }
    } else {
        printf("\nNingún participante completo su turno.\n");
    }

    exit(0);
}

void* atender_cliente(void* arg) {
    Cliente* cliente = (Cliente*)arg;
    Juego* juego = &cliente->juego;
    char buffer[512];
    char nuevaRonda;
    int letraInvalida = 0;

    juego->intentos_restantes = 10;
    juego->partida_en_curso = 1;

    int idx = rand() % total_frases;
    strncpy(juego->frase_secreta, frases[idx], MAX_LEN);
    ocultar_frase(juego->frase_secreta, juego->frase_oculta);

    while (juego->partida_en_curso) {
        snprintf(buffer, sizeof(buffer), "Frase: %s\nIntentos restantes: %d\nIngrese letra: ",
                 juego->frase_oculta, juego->intentos_restantes);
        if (send(cliente->socket, buffer, strlen(buffer), 0) <= 0) break;

        int n = recv(cliente->socket, &juego->letra, 1, 0);
        if (n <= 0) break;

        actualizar_oculta(juego->frase_secreta, juego->frase_oculta, juego->letra, &juego->acierto);
        if (!juego->acierto) juego->intentos_restantes--;

        if (strcmp(juego->frase_oculta, juego->frase_secreta) == 0 || juego->intentos_restantes <= 0) {
            juego->partida_en_curso = 0;
            if (strcmp(juego->frase_oculta, juego->frase_secreta) == 0) {
                send(cliente->socket, "Ganaste!\n", 9, 0);
                actualizar_ranking(juego->nickname, 1);
            } else {
                send(cliente->socket, "Perdiste!\n", 10, 0);
                actualizar_ranking(juego->nickname, 0);
            }

            do {
                if (!letraInvalida) {
                    send(cliente->socket, "\n¿Querés volver a jugar? S/N: ", 32, 0);
                    n = recv(cliente->socket, &nuevaRonda, 1, 0);
                    if (n <= 0) break;

                    if (nuevaRonda == 's' || nuevaRonda == 'S') {
                        idx = rand() % total_frases;
                        strncpy(juego->frase_secreta, frases[idx], MAX_LEN);
                        ocultar_frase(juego->frase_secreta, juego->frase_oculta);
                        juego->intentos_restantes = 10;
                        juego->partida_en_curso = 1;
                    } else if (nuevaRonda == 'n' || nuevaRonda == 'N') {
                        send(cliente->socket, "\nGracias por jugar! Ranking actual:\n", 37, 0);
                        ordenar_ranking();
                        for (int i = 0; i < ranking_count; i++) {
                            snprintf(buffer, sizeof(buffer), "%s - %d aciertos\n",
                                     ranking[i].nickname, ranking[i].aciertos);
                            send(cliente->socket, buffer, strlen(buffer), 0);
                        }
                    } else {
                        letraInvalida = 1;
                    }
                } else {
                    send(cliente->socket, "\nLETRA INVALIDA. Querés volver a jugar? S/N: ", 45, 0);
                    letraInvalida = 0;
                }
            } while (nuevaRonda != 's' && nuevaRonda != 'S' && nuevaRonda != 'n' && nuevaRonda != 'N');
        }
    }

    printf("Cliente %s desconectado.\n", juego->nickname);
    close(cliente->socket);

    pthread_mutex_lock(&mutex_clientes);
    usuarios_conectados--;
    pthread_mutex_unlock(&mutex_clientes);

    free(cliente);
    return NULL;
}

void mostrar_ayuda() {
    printf("Uso: ./servidor -a ARCHIVO -p PUERTO -u MAX_USUARIOS\n");
}

int main(int argc, char *argv[]) {
    int opt, puerto = 0, max_usuarios = 0;
    int flag_a = 0, flag_p = 0, flag_u = 0;
    int sockfd;

    static struct option long_options[] = {
        {"archivo",  required_argument, 0, 'a'},
        {"puerto",   required_argument, 0, 'p'},
        {"usuarios", required_argument, 0, 'u'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "a:p:u:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a': strncpy(archivo_frases, optarg, MAX_LEN); flag_a = 1; break;
            case 'p': puerto = atoi(optarg); flag_p = 1; break;
            case 'u': max_usuarios = atoi(optarg); flag_u = 1; break;
            case 'h': mostrar_ayuda(); return 0;
            default: mostrar_ayuda(); return 1;
        }
    }

    if (!flag_a || !flag_p || !flag_u) {
        mostrar_ayuda();
        return 1;
    }

    signal(SIGINT, sigint_handler);
    cargar_frases();

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    struct sockaddr_in servidor;
    servidor.sin_family = AF_INET;
    servidor.sin_addr.s_addr = INADDR_ANY;
    servidor.sin_port = htons(puerto);

    if (bind(sockfd, (struct sockaddr*)&servidor, sizeof(servidor)) < 0) {
        perror("bind"); return 1;
    }

    char hostname[128];
    gethostname(hostname, sizeof(hostname));

    struct hostent *host_entry = gethostbyname(hostname);
    if (host_entry != NULL) {
        char *ip = inet_ntoa(*(struct in_addr*)host_entry->h_addr_list[0]);
        printf("Servidor escuchando en IP: %s, puerto: %d\n", ip, puerto);
    }

    if (listen(sockfd, max_usuarios) < 0) {
        perror("listen"); return 1;
    }

    while (1) {
        if (usuarios_conectados < max_usuarios) {
            struct sockaddr_in cliente_addr;
            socklen_t addr_len = sizeof(cliente_addr);
            int cliente_fd = accept(sockfd, (struct sockaddr*)&cliente_addr, &addr_len);
            if (cliente_fd < 0) continue;

            Cliente* nuevo = malloc(sizeof(Cliente));
            nuevo->socket = cliente_fd;

            if (recv(cliente_fd, nuevo->juego.nickname, sizeof(nuevo->juego.nickname), 0) <= 0) {
                close(cliente_fd);
                free(nuevo);
                continue;
            }

            printf("Cliente %s conectado.\n", nuevo->juego.nickname);

            pthread_mutex_lock(&mutex_clientes);
            clientes[usuarios_conectados++] = *nuevo;
            pthread_mutex_unlock(&mutex_clientes);

            pthread_t hilo;
            pthread_create(&hilo, NULL, atender_cliente, nuevo);
            pthread_detach(hilo);
        } else {
            sleep(1);
        }
    }

    close(sockfd);
    return 0;
}