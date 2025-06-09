#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/prctl.h>

void mostrar_info(const char *nombre) {
    printf("Soy el proceso %s con PID %d, mi padre es %d\n\n", nombre, getpid(),getppid());
}

void leyenda_finalizado(const char *nombre) {
    printf("El proceso %s finalizó su ejecución...\n", nombre);
}

int main(int argc, char *argv[]) {
    int status;
    int count = 0;
    int wpid;

    if (argc > 1 && 
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("Uso: %s\n", argv[0]);
        printf("Este programa genera una jerarquía de procesos.\n");
        return 0;
    }

    pid_t pid_h1, pid_h2, pid_n1, pid_zombie, pid_n3, pid_demonio;
    char nombreProceso[15];
    
    strcpy(nombreProceso,"Padre");
    prctl(PR_SET_NAME, nombreProceso, 0, 0, 0); // Cambia el nombre del proceso

    mostrar_info(nombreProceso);

    pid_h1 = fork();
    if (pid_h1 == 0) {
        // Hijo 1
        strcpy(nombreProceso,"Hijo 1");
        prctl(PR_SET_NAME, nombreProceso, 0, 0, 0); // Cambia el nombre del proceso
        mostrar_info(nombreProceso);

        pid_n1 = fork();
        if (pid_n1 == 0) {
            // Nieto 1
            strcpy(nombreProceso,"Nieto 1");
            prctl(PR_SET_NAME, nombreProceso, 0, 0, 0); // Cambia el nombre del proceso
            mostrar_info(nombreProceso);
            getchar();
            leyenda_finalizado(nombreProceso);
            exit(0);
        }

        pid_zombie = fork();
        if (pid_zombie == 0) {
            // Zombie
            strcpy(nombreProceso,"Zombie");
            prctl(PR_SET_NAME, nombreProceso, 0, 0, 0); // Cambia el nombre del proceso
            mostrar_info(nombreProceso);
            getchar();
            leyenda_finalizado(nombreProceso);
            exit(0);
        }

        pid_n3 = fork();
        if (pid_n3 == 0) {
            // Nieto 3
            strcpy(nombreProceso,"Nieto 3");
            prctl(PR_SET_NAME, nombreProceso, 0, 0, 0); // Cambia el nombre del proceso
            mostrar_info(nombreProceso);
            getchar();
            leyenda_finalizado(nombreProceso);
            exit(0);   
        }

        waitpid(pid_n1, &status, 0);
        waitpid(pid_n3, &status, 0);

        getchar();  // Hijo 1 espera
        leyenda_finalizado(nombreProceso);
        exit(0);

    }

    pid_h2 = fork();
    if (pid_h2 == 0) {
        // Hijo 2
        strcpy(nombreProceso,"Hijo 2");
        prctl(PR_SET_NAME, nombreProceso, 0, 0, 0); // Cambia el nombre del proceso
        mostrar_info(nombreProceso);

        pid_demonio = fork();
        if (pid_demonio == 0) {
            // Proceso hijo: vamos a hacerlo daemon
            strcpy(nombreProceso,"Demonio");
            mostrar_info(nombreProceso);
            
            // Crea una nueva sesión (desasocia del terminal)
            if (setsid() < 0) {
                perror("setsid");
                exit(1);
            }

            // Segundo fork para evitar que el daemon sea líder de sesión
            pid_t pid_segundo = fork();
            if (pid_segundo < 0) {
                perror("fork");
                exit(1);
            }

            if (pid_segundo > 0) {
                // Terminamos el primer hijo, el que hizo setsid()
                exit(0);
            }

            prctl(PR_SET_NAME, nombreProceso, 0, 0, 0); // Cambia el nombre del proceso
            mostrar_info(nombreProceso);

            // Loop infinito para simular servicio activo
            while (1) {
                sleep(5);
            }
        }

        while (count < 1) {
            wpid = wait(&status);
            if (wpid > 0) {
                count++;
            }
        }

        getchar();
        leyenda_finalizado(nombreProceso);
        exit(0);
    }

    

    while (count < 2) {
        wpid = wait(&status);

        if (wpid > 0) {
            count++;
        }
    }
    getchar();
    leyenda_finalizado(nombreProceso);
    return 0;
}