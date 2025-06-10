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
            default: mostrar_ayuda(); return 1;
        }
    }

    if (!flag_n || !flag_p || !flag_s) {
        mostrar_ayuda();
        return 1;
    }

    signal(SIGINT, sigint_handler);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(puerto);
    if (inet_pton(AF_INET, servidor_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return 1;
    }

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        return 1;
    }

    // Enviar nickname
    if (send(sockfd, nickname, strlen(nickname), 0) <= 0) {
        perror("send nickname");
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