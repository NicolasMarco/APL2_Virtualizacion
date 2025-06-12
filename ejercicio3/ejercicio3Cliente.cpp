#include <iostream>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string>
#include <cstdlib>
#include <limits.h>
#include <cstring>
#include <csignal>

#define FIFO_PATH "/tmp/cola_impresion"

std::string fifo_privado;
void limpiar_y_salir(int signo) {
    std::cerr << "\nInterrumpido con señal " << signo << ", limpiando FIFO privado...\n";
    if (!fifo_privado.empty()) {
        unlink(fifo_privado.c_str());
    }
    exit(1);
}

int main(int argc, char* argv[]) {
    std::string archivo;
    signal(SIGINT, limpiar_y_salir);
    signal(SIGTERM, limpiar_y_salir);
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-a" || arg == "--archivo") {
            if (i + 1 < argc) {
                archivo = argv[++i];
            } else {
                std::cerr << "Falta el valor para " << arg << std::endl;
                return EXIT_FAILURE;
            }
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Uso: ./cliente -a <archivo>" << std::endl;
            std::cout << "Opciones:" << std::endl;
            std::cout << "  -a, --archivo <archivo>    Archivo a imprimir. (Requerido)" << std::endl;
            std::cout << "  -h, --help                  Muestra esta ayuda." << std::endl;
            std::cout << "Ejemplo: ./cliente -a documento.txt" << std::endl;
            std::cout << "Este programa envía un archivo al servidor de impresión usando un FIFO compartido." << std::endl;
            std::cout << "El servidor imprimirá el contenido y devolverá la confirmación al cliente." << std::endl;
            return EXIT_SUCCESS;
        } else {
            std::cerr << "Argumento desconocido: " << arg << std::endl;
            return EXIT_FAILURE;
        }
    }

    if (archivo.empty()) {
        std::cerr << "Error: el archivo a imprimir es obligatorio.\n";
        return EXIT_FAILURE;
    }

    // Convertir a ruta absoluta
    char ruta_absoluta[PATH_MAX];
    if (realpath(archivo.c_str(), ruta_absoluta) == nullptr) {
        std::cerr << "Error: no se pudo resolver la ruta '" << archivo << "'. " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }

    // Validar existencia y que no esté vacío
    struct stat st;
    if (stat(ruta_absoluta, &st) != 0) {
        std::cerr << "Error: el archivo no existe o no se puede acceder: " << ruta_absoluta << std::endl;
        return EXIT_FAILURE;
    }

    std::ifstream input(ruta_absoluta, std::ios::binary);
    if (!input) {
        std::cerr << "Error: no se pudo abrir el archivo '" << ruta_absoluta << "' para lectura.\n";
        return EXIT_FAILURE;
    }

    pid_t pid = getpid();
    fifo_privado = "/tmp/FIFO_" + std::to_string(pid);
    mkfifo(fifo_privado.c_str(), 0666);

    int fd = open(FIFO_PATH, O_WRONLY);
    if (fd < 0) {
        perror("No existe servidor para imprimir el archivo.");
        unlink(fifo_privado.c_str());
        return 1;
    }

    // Enviar encabezado
    std::string encabezado = std::to_string(pid) + ":" + ruta_absoluta + "\n";
    write(fd, encabezado.c_str(), encabezado.size());

    // Enviar contenido
    char buffer[4096];
    while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
        write(fd, buffer, input.gcount());
    }

    input.close();
    close(fd);

    // Esperar respuesta
    int fd_resp = open(fifo_privado.c_str(), O_RDONLY);
    if (fd_resp < 0) {
        perror("open FIFO privado");
        unlink(fifo_privado.c_str());
        return 1;
    }

    char respuesta[512] = {0};
    read(fd_resp, respuesta, sizeof(respuesta));
    std::cout << "Respuesta del servidor:\n" << respuesta;

    close(fd_resp);
    unlink(fifo_privado.c_str());

    return 0;
}