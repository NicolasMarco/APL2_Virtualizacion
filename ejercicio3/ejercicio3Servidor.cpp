#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctime>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <csignal>

#define FIFO_PATH "/tmp/cola_impresion"
#define LOG_PATH "/tmp/impresiones.log"

int fifo_fd = -1;

void limpiar_y_salir(int signo) {
    std::cerr << "\nServidor detenido con señal " << signo << ", limpiando...\n";
    if (fifo_fd != -1) {
        close(fifo_fd);
        fifo_fd = -1;
    }
    unlink(FIFO_PATH);
    exit(0);
}


std::string obtener_fecha_hora_actual() {
    std::time_t t = std::time(nullptr);
    std::tm* tm_ptr = std::localtime(&t);
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%d/%m/%Y a las %H:%M:%S", tm_ptr);
    return std::string(buffer);
}

int main(int argc, char* argv[]) {
    int cantidad_impresiones = -1;

    std::signal(SIGINT, limpiar_y_salir);
    std::signal(SIGTERM, limpiar_y_salir);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-i" || arg == "--impresiones") {
            if (i + 1 < argc) {
                try {
                    cantidad_impresiones = std::stoi(argv[++i]);
                    if (cantidad_impresiones <= 0) {
                        std::cerr << "no positivo" << std::endl;
                        return EXIT_FAILURE;
                    }
                } catch (...) {
                    std::cerr << "Valor inválido para " << arg << ": debe ser un entero positivo.\n";
                    return EXIT_FAILURE;
                }
            } else {
                std::cerr << "Falta el valor para " << arg << std::endl;
                return EXIT_FAILURE;
            }
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Uso: ./servidor -i <cantidad>" << std::endl;
            std::cout << "Opciones:" << std::endl;
            std::cout << "  -i, --impresiones <número>   Cantidad de archivos a imprimir. (Requerido, entero positivo)" << std::endl;
            std::cout << "  -h, --help                    Muestra esta ayuda." << std::endl;
            std::cout << "Ejemplo: ./servidor -i 5" << std::endl;
            std::cout << "Este programa actúa como un servidor de impresión que espera trabajos enviados por clientes." << std::endl;
            std::cout << "Procesa la cantidad de trabajos indicada, registrando cada impresión en '/tmp/impresiones.log'." << std::endl;
            return EXIT_SUCCESS;
        } else {
            std::cerr << "Argumento desconocido: " << arg << std::endl;
            return EXIT_FAILURE;
        }
    }

    if (cantidad_impresiones == -1) {
        std::cerr << "Error: la cantidad de impresiones es obligatoria.\n";
        return EXIT_FAILURE;
    }

    // Eliminar el FIFO si ya existe
    unlink(FIFO_PATH);

    // Crear FIFO si no existe
    if (mkfifo(FIFO_PATH, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo");
        return 1;
    }

    // Limpiar el log anterior
    std::ofstream limpiarLog(LOG_PATH, std::ios::trunc);
    if (!limpiarLog) {
        std::cerr << "No se pudo limpiar el archivo de log\n";
    }

    std::cout << "Servidor esperando trabajos...\n";

    // Abrir FIFO en modo lectura bloqueante
    int fifo_fd = open(FIFO_PATH, O_RDONLY);
    if (fifo_fd < 0) {
        perror("open");
        return 1;
    }

    while (cantidad_impresiones > 0) {
        // Leer encabezado: PID:RUTA\n
        std::string encabezado;
        char ch;
        while (read(fifo_fd, &ch, 1) == 1 && ch != '\n') {
            encabezado += ch;
        }

        if (encabezado.empty()) {
            continue; // No se recibió nada válido
        }

        std::istringstream iss(encabezado);
        std::string pid_str, ruta;
        getline(iss, pid_str, ':');
        getline(iss, ruta);
        pid_t pid;
        try {
            pid = std::stoi(pid_str);
        } catch (...) {
            std::cerr << "Encabezado inválido: " << encabezado << "\n";
            continue;
        }

        std::ofstream log(LOG_PATH, std::ios::app);
        bool archivoValido = false;

        if (!log) {
            std::cerr << "No se pudo abrir el archivo de log\n";
        } else {
            // Línea de cabecera
            log << "PID {" << pid << "} imprimió el archivo {" << ruta << "} el día {"
                << obtener_fecha_hora_actual() << "}\n";
            char buffer[4096];
            ssize_t n;
            while ((n = read(fifo_fd, buffer, sizeof(buffer))) > 0) {
                log.write(buffer, n);
                archivoValido = true;
            }
            if(!archivoValido)
                log << "Error: Archivo vacío.\n";
            log << "\n";
            log.flush();
        }

        // Enviar respuesta al FIFO privado del cliente
        std::string fifo_cliente = "/tmp/FIFO_" + pid_str;
        int fd_resp = open(fifo_cliente.c_str(), O_WRONLY);
        if (fd_resp >= 0) {
            std::string mensaje = archivoValido ?
                "Impresión completada.\n" :
                "Error: Archivo vacío.\n";
            write(fd_resp, mensaje.c_str(), mensaje.size());
            close(fd_resp);
            cantidad_impresiones--;
        } else {
            std::cerr << "No se pudo abrir FIFO del cliente: " << fifo_cliente << "\n";
        }
    }

    close(fifo_fd);
    unlink(FIFO_PATH);
    return 0;
}
