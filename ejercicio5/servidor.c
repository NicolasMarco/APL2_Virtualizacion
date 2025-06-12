/*------------------------------------------------------------
# APL2.
# Materia: Virtualizacion de hardware
# Ingeniería en Informática
# Universidad Nacional de La Matanza (UNLaM)
# Año: 2025
#
# Integrantes del grupo:
# - De Luca, Leonel Maximiliano DNI: 42.588.356
# - La Giglia, Rodrigo Ariel DNI: 33334248
# - Marco, Nicolás Agustín DNI: 40885841
# - Marrone, Micaela Abril DNI: 45683584
#-------------------------------------------------------------
*/

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

// Estructura que almacena el estado de una partida de un cliente
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

// Estructura para mantener el ranking de jugadores
typedef struct {
    char nickname[64];
    int aciertos;
} Ranking;

// Estructura que representa un cliente conectado
typedef struct {
    int socket;
    Juego juego;
} Cliente;

// Variables globales
Cliente clientes[MAX_CLIENTES];          // Lista de clientes conectados
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

// Oculta la frase reemplazando letras por guiones bajos
void ocultar_frase(const char *frase, char *oculta) {
    for (int i = 0; frase[i]; i++)
        oculta[i] = frase[i] == ' ' ? ' ' : '_';
    oculta[strlen(frase)] = 0;
}

// Actualiza la frase oculta con la letra ingresada y marca si hubo acierto
void actualizar_oculta(const char *frase, char *oculta, char letra, int *acierto) {
    *acierto = 0;
    for (int i = 0; frase[i]; i++) {
        if (frase[i] == letra && oculta[i] == '_') {
            oculta[i] = letra;
            *acierto = 1;
        }
    }
}

// Actualiza el ranking de un jugador (sumando un acierto si ganó)
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
// Función para verificar si un nickname ya existe
int nickname_existe(const char *nickname) {
    pthread_mutex_lock(&mutex_clientes);  // Protegemos el acceso a clientes
    for (int i = 0; i < usuarios_conectados; i++) {
        if (strcmp(clientes[i].juego.nickname, nickname) == 0) {
            pthread_mutex_unlock(&mutex_clientes);
            return 1;  // Ya existe
        }
    }
    pthread_mutex_unlock(&mutex_clientes);
    return 0;  // No existe
}

// Comparador para ordenar el ranking de mayor a menor aciertos
int comparar_ranking(const void *a, const void *b) {
    return ((Ranking*)b)->aciertos - ((Ranking*)a)->aciertos;
}

void ordenar_ranking() {
    pthread_mutex_lock(&mutex_ranking);
    qsort(ranking, ranking_count, sizeof(Ranking), comparar_ranking);
    pthread_mutex_unlock(&mutex_ranking);
}

// Handler para Ctrl+C (SIGINT) que finaliza el servidor
void sigint_handler(int sig) {
    printf("\nServidor detenido. Notificando a los clientes...\n");

    for (int i = 0; i < usuarios_conectados; i++) {
        send(clientes[i].socket, "Servidor cerrado\n", 18, 0);
        close(clientes[i].socket);
    }
	
	// Muestra el ranking final
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

// Hilo que atiende a un cliente (juego)
void* atender_cliente(void* arg) {
    Cliente* cliente = (Cliente*)arg;
    Juego* juego = &cliente->juego;
    char buffer[512];
    char nuevaRonda;
    int letraInvalida = 0;

	 // Inicializa el juego
    juego->intentos_restantes = 10;
    juego->partida_en_curso = 1;

	 // Selecciona frase aleatoria
    int idx = rand() % total_frases;
    strncpy(juego->frase_secreta, frases[idx], MAX_LEN);
    ocultar_frase(juego->frase_secreta, juego->frase_oculta);

	// Bucle principal del juego
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
				snprintf(buffer, sizeof(buffer), "Ganaste! Frase: %s\n", juego->frase_secreta);
                send(cliente->socket, buffer, strlen(buffer), 0);
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
                        ordenar_ranking();
						char rankingBuffer[2048];  // Aumentar el tamaño si hace falta
						snprintf(rankingBuffer, sizeof(rankingBuffer),
								 "\nGracias por jugar! Ranking actual:\n");

						for (int i = 0; i < ranking_count; i++) {
							char linea[128];
							snprintf(linea, sizeof(linea), "%s - %d aciertos\n", ranking[i].nickname, ranking[i].aciertos);
							strncat(rankingBuffer, linea, sizeof(rankingBuffer) - strlen(rankingBuffer) - 1);
						}

						send(cliente->socket, rankingBuffer, strlen(rankingBuffer), 0);
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
    printf("\n");
    printf("Uso: servidor [OPCIONES]\n");
    printf("--------------------------------------\n");
    printf("Este programa inicia un servidor para un juego multicliente.\n");
    printf("Requiere un archivo de frases, un puerto y el número máximo de usuarios.\n\n");
    printf("Opciones:\n");
    printf("  -a, --archivo <archivo>\n");
    printf("        Archivo con las frases del juego. (Requerido)\n");
    printf("\n");
    printf("  -p, --puerto <puerto>\n");
    printf("        Número de puerto donde el servidor escuchará las conexiones. (Requerido)\n");
    printf("\n");
    printf("  -u, --usuarios <max_usuarios>\n");
    printf("        Cantidad máxima de usuarios concurrentes permitidos. (Requerido)\n");
    printf("\n");
    printf("  -h, --help\n");
    printf("        Muestra esta ayuda y termina la ejecución.\n");
    printf("\n");
    printf("Ejemplo de uso:\n");
    printf("  ./servidor -a frases.txt -p 8080 -u 5\n");
    printf("\n");
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
	
	// Procesamiento de argumentos de línea de comandos con getopt_long
    while ((opt = getopt_long(argc, argv, "a:p:u:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a': strncpy(archivo_frases, optarg, MAX_LEN); flag_a = 1; break;
            case 'p': puerto = atoi(optarg); flag_p = 1; break;
            case 'u': max_usuarios = atoi(optarg); flag_u = 1; break;
            case 'h': mostrar_ayuda(); return 0;
            default: fprintf(stderr, "Error: parametros incorrectos.\n"); return 1;
        }
    }
	
	// Verifica que se hayan pasado todas los parametros requeridos
    if (!flag_a || !flag_p || !flag_u) {
        fprintf(stderr, "Faltan opciones obligatorias -a <archivo> -p <puerto> -u <max_usuarios>.\n");
        return 1;
    }

    signal(SIGINT, sigint_handler);
	
    cargar_frases();

	// Crea el socket del servidor
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { 
		perror("Error: No se pudo iniciar el servidor. "
                    "Por favor, intente de nuevo más tarde.\n"); 
		return 1; }

	// Configura la estructura de dirección del servidor
    struct sockaddr_in servidor;
    servidor.sin_family = AF_INET;
    servidor.sin_addr.s_addr = INADDR_ANY;
    servidor.sin_port = htons(puerto);
	int optval = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
		perror("Error: No se pudo iniciar el servidor. Por favor, intente de nuevo más tarde.\n");
	}
    if (bind(sockfd, (struct sockaddr*)&servidor, sizeof(servidor)) < 0) {
        perror("Error: No se pudo asociar el servidor al puerto especificado. "
               "Verifique que el puerto no esté en uso o que tenga permisos suficientes.\n");
		return 1;
    }

	// Obtiene el nombre del host y lo imprime junto con la IP y el puerto
	
    char hostname[128];
    gethostname(hostname, sizeof(hostname));

    struct hostent *host_entry = gethostbyname(hostname);
    if (host_entry != NULL) {
        char *ip = inet_ntoa(*(struct in_addr*)host_entry->h_addr_list[0]);
        printf("Servidor escuchando en IP: %s, puerto: %d\n", ip, puerto);
    }

	// Inicia la escucha en el socket con la cantidad máxima de usuarios permitida
    if (listen(sockfd, max_usuarios) < 0) {
        perror("listen"); return 1;
    }
	
	// Bucle principal del servidor
    while (1) {
        
		struct sockaddr_in cliente_addr;
		socklen_t addr_len = sizeof(cliente_addr);
		
		// Espera y acepta una nueva conexión de cliente
		int cliente_fd = accept(sockfd, (struct sockaddr*)&cliente_addr, &addr_len);
		if (cliente_fd < 0) continue;
		
		if (usuarios_conectados < max_usuarios) {
			// Reserva memoria para un nuevo cliente
            Cliente* nuevo = malloc(sizeof(Cliente));
            nuevo->socket = cliente_fd;
			
			 // Recibe el nickname del cliente
            if (recv(cliente_fd, nuevo->juego.nickname, sizeof(nuevo->juego.nickname), 0) <= 0) {
				// Si hubo error o se desconectó, cierra y libera
                close(cliente_fd);
                free(nuevo);
                continue;
            }
			
			// Validar que el nickname no exista ya
			if (nickname_existe(nuevo->juego.nickname)) {
				char *msg = "El nickname ya está en uso. Conéctese con otro.\n";
				send(cliente_fd, msg, strlen(msg), 0);
				close(cliente_fd);
				free(nuevo);
				continue;
			}

            printf("Cliente %s conectado.\n", nuevo->juego.nickname);

			// Agrega al nuevo cliente a la lista
            pthread_mutex_lock(&mutex_clientes);
            clientes[usuarios_conectados++] = *nuevo;
            pthread_mutex_unlock(&mutex_clientes);

			// Crea un hilo para atender al nuevo cliente 
            pthread_t hilo;
            pthread_create(&hilo, NULL, atender_cliente, nuevo);
            pthread_detach(hilo);
		} else {
			// Si se alcanzó el límite, informa
			char *msg = "Servidor lleno: demasiados clientes conectados. Intente más tarde.\n";
			send(cliente_fd, msg, strlen(msg), 0);
			close(cliente_fd);
			continue;
        }
    }

    close(sockfd);
    return 0;
}