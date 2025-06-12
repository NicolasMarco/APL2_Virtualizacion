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
#include <signal.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>

#define MAX_LEN 512

int sockfd;

void sigint_handler(int sig) {
    printf("\nDesconectando del servidor...\n");
    close(sockfd);
    exit(0);
}

void mostrar_ayuda() {
    printf("Uso: ./cliente -n NICKNAME -p PUERTO -s SERVIDOR\n");
    printf("Parámetros obligatorios:\n");
    printf("  -n, --nickname   Nombre de usuario (nickname) para identificar al jugador.\n");
    printf("  -p, --puerto     Puerto del servidor al que se conectará el cliente.\n");
    printf("  -s, --servidor   Dirección IP o nombre del servidor.\n");
    printf("  -h, --help       Muestra esta ayuda.\n");
}

int main(int argc, char *argv[]) {
    int opt, flag_n = 0, flag_p = 0, flag_s = 0;
    char nickname[64];
    char servidor_ip[64];
    int puerto;

    static struct option long_options[] = {
        {"nickname", required_argument, 0, 'n'},
        {"puerto",   required_argument, 0, 'p'},
        {"servidor", required_argument, 0, 's'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "n:p:s:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'n': strncpy(nickname, optarg, sizeof(nickname)); flag_n = 1; break;
            case 'p': puerto = atoi(optarg); flag_p = 1; break;
            case 's': strncpy(servidor_ip, optarg, sizeof(servidor_ip)); flag_s = 1; break;
            case 'h': mostrar_ayuda(); return 0;
            default: fprintf(stderr, "Error: parametros incorrectos.\n"); return 1;
        }
    }

    if (!flag_n || !flag_p || !flag_s) {
        fprintf(stderr, "Faltan opciones obligatorias -n <nickname> -p <puerto> -s <servidor_ip/nombre_servidor>.\n");
        return 1;
    }

    signal(SIGINT, sigint_handler);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error: No se pudo crear la conexión de red.\n");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(puerto);
    
	// Primero intentamos inet_pton (para ver si es una IP numérica)
	if (inet_pton(AF_INET, servidor_ip, &server_addr.sin_addr) <= 0) {
    // Si falla, intentamos con gethostbyname()
		struct hostent *he = gethostbyname(servidor_ip);
		if (he == NULL) {
			perror("Error: No se pudo resolver la dirección del servidor.\n");
			return 1;
		}
		// Copiamos la dirección resuelta al sockaddr_in
		memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
	}
	
	

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error: No se pudo conectar con el servidor.\n");
        return 1;
    }

    // Enviar nickname
    if (send(sockfd, nickname, strlen(nickname), 0) <= 0) {
        perror("Error: No se pudo enviar el nombre de usuario al servidor.\n");
        close(sockfd);
        return 1;
    }

    printf("Conectado al servidor como '%s'.\n", nickname);

    char buffer[MAX_LEN];
    char input[8]; // para letra o respuesta S/N
    int n;

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            printf("Servidor cerrado o conexión perdida.\n");
            break;
        }

        printf("%s", buffer);

        if (strstr(buffer, "Ingrese letra:") || strstr(buffer, "volver a jugar")) {
            fgets(input, sizeof(input), stdin);
            if (send(sockfd, input, 1, 0) <= 0) {
                printf("Error enviando respuesta al servidor.\n");
                break;
            }
        }
    }

    close(sockfd);
    return 0;
}