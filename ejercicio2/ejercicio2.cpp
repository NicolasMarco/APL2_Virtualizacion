#include <iostream>          
#include <fstream>           
#include <string>            
#include <thread>            
#include <vector>            
#include <map>               
#include <mutex>             
#include <semaphore.h>       
#include <fcntl.h>           
#include <unistd.h>          
#include <filesystem>        
#include <csignal>

using namespace std;
namespace fs = std::filesystem;
int paquetes = 0;
int totalPaquetes = 0;  // cantidad total de paquetes
int contadorIds = 0; // contador de ids de paquetes

std::map<int, int> cantidadPaquetesPorSucursal;
std::map<int, int> pesoPorSucursal;
std::map<int, mutex> mutexPorID;

std::mutex mutexContador;
std::mutex mutexPaquetes;
std::mutex mutexPaquetes2;
std::mutex mutexMapas;
std::mutex mutexDirectorios;

sem_t* semaforoLimiteBuffer = nullptr;
sem_t* semaforoConsumidor = nullptr;

std::string directorio;
void limpiarRecursos( ) {
    if (semaforoLimiteBuffer != nullptr) {
        sem_close(semaforoLimiteBuffer);
        sem_unlink("BufferVirtualLimite");
    }
    if (semaforoConsumidor != nullptr) {
        sem_close(semaforoConsumidor);
        sem_unlink("BufferVirtualConsumidor");
    }

    exit(EXIT_SUCCESS);  // Terminar proceso limpiamente
}
void manejarSenales(int signal) {
    if (signal == SIGINT) {
        cout << "\nSIGINT  recibido." << endl;
    } else if (signal == SIGTERM) {
        cout << "\nSIGTERM recibido." << endl;
    }
    limpiarRecursos();
}
void trabajoHiloConsumidor()
{
    /*aaaaa*/
    while(paquetes > 0) {
        mutexPaquetes.lock();
        if (paquetes <= 0) {
            mutexPaquetes.unlock();
            break;
        }
        paquetes--;
        mutexPaquetes.unlock();
        sem_wait(semaforoConsumidor); // P(semaforoConsumidor)
        
        
        int peso = 0;
        int destino = 0;
        //busco un archivo .paq en el directorio
        fs::path directorioPath(directorio);
        fs::path archivo;
		{
			std::lock_guard<std::mutex> lock(mutexDirectorios);
			for (const auto& entry : fs::directory_iterator(directorioPath)) {
				if (entry.path().extension() == ".paq") {
					// Renombrarlo inmediatamente para marcarlo como "en proceso"
					archivo = entry.path();
					fs::path nuevoNombre = archivo;
					nuevoNombre += ".tmp"; // por ejemplo: 123.paq.tmp
					fs::rename(archivo, nuevoNombre);
					archivo = nuevoNombre;
					break;
				}
			}
        }
        //lo muevo a la carpeta procesados
        if (archivo.empty()) {
            cerr << "No se encontró ningún archivo .paq en el directorio." << endl;
            continue;
        }
        ifstream archivoEntrada(archivo);
		if (archivoEntrada.is_open()) {
			string linea;
			getline(archivoEntrada, linea);
			archivoEntrada.close();

			stringstream ss(linea);
			string id_str, peso_str, destino_str;
			getline(ss, id_str, ';');
			getline(ss, peso_str, ';');
			getline(ss, destino_str, ';');

			peso = stoi(peso_str);
			destino = stoi(destino_str);
		}

         
            
            
		fs::path nombreOriginal = archivo.stem(); // Esto da "123.paq"
		fs::path destinoPath = fs::path(directorio) / "procesados" / nombreOriginal;
		fs::rename(archivo, destinoPath);
		
	   
		//actualizo el mapa de sucursales
		mutexMapas.lock();
		cantidadPaquetesPorSucursal[destino]++;
		pesoPorSucursal[destino] += peso;
		mutexMapas.unlock();		
		
		sem_post(semaforoLimiteBuffer); // V(semaforoLimiteBuffer)       
        
        
    }
}

void trabajoHiloProductor()
{
 
    while(totalPaquetes > 0) {
        mutexPaquetes2.lock();
        if (totalPaquetes <= 0) {
            mutexPaquetes2.unlock();
            break;
        }
        totalPaquetes--;
        mutexPaquetes2.unlock();

        sem_wait(semaforoLimiteBuffer); // P(semaforoLimiteBuffer)    
        
 
        //procesar el paquete
        mutexContador.lock();
        int id = contadorIds++;
        mutexContador.unlock();
        // Simular un ID de paquete
        int peso = random() % 300; // Simular un peso de paquete
        int destino = random() % 50; // Simular un destino de paquete
        
		
		//creo archivo temporal en el directorio
		string nombreArchivoTmp = to_string(id) + ".paq.tmp";
		fs::path archivoTmp = fs::path(directorio) / nombreArchivoTmp;
		
		// lo lleno con  id_paquete;peso;destino
		ofstream archivoSalida(archivoTmp);
		if (archivoSalida.is_open()) {
			archivoSalida << id << ";" << peso << ";" << destino;
			archivoSalida.close();
			// Renombrar atómicamente
			fs::rename(archivoTmp, fs::path(directorio) / (to_string(id) + ".paq"));
		} else {
			cerr << "Error al crear el archivo: " << archivoTmp.string() << endl;
		}
		
        sem_post(semaforoConsumidor); // V(semaforoConsumidor)        
    }   
}


int main(int argc, char* argv[]) {
    srand(time(nullptr));
    signal(SIGINT, manejarSenales);
    signal(SIGTERM, manejarSenales);

    int generadores = 0;
    int consumidores = 0;
    paquetes = 0;
    int paquetesProcesados = 0;
   

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];

        if (arg == "-d" || arg == "--directorio") {
            if (i + 1 < argc)
                directorio = argv[++i];
            else {
                cerr << "Falta el valor para " << arg << endl;
                return EXIT_FAILURE;
            }
        } else if (arg == "-g" || arg == "--generadores") {
            if (i + 1 < argc)
                generadores = atoi(argv[++i]);
            else {
                cerr << "Falta el valor para " << arg << endl;
                return EXIT_FAILURE;
            }
        } else if (arg == "-c" || arg == "--consumidores") {
            if (i + 1 < argc)
                consumidores = atoi(argv[++i]);
            else {
                cerr << "Falta el valor para " << arg << endl;
                return EXIT_FAILURE;
            }
        } else if (arg == "-p" || arg == "--paquetes") {
            if (i + 1 < argc)
                paquetes = atoi(argv[++i]);
            else {
                cerr << "Falta el valor para " << arg << endl;
                return EXIT_FAILURE;
            }
        } else if (arg == "-h" || arg == "--help") {
            cout << "Uso: ./programa -d <directorio> -g <generadores> -c <consumidores> -p <paquetes>" << endl;
            cout << "Opciones:" << endl;
            cout << "  -d, --directorio <directorio>   Directorio donde se guardarán los archivos." << endl;
            cout << "  -g, --generadores <número>      Número de hilos generadores." << endl;
            cout << "  -c, --consumidores <número>     Número de hilos consumidores." << endl;
            cout << "  -p, --paquetes <número>         Número de paquetes a procesar." << endl;
            cout << "  -h, --help                      Mostrar esta ayuda." << endl;
            cout << "Ejemplo: ./programa -d ./datos -g 3 -c 2 -p 10" << endl;
            cout << "Este programa crea un directorio, genera archivos de paquetes y los procesa en paralelo." << endl;
            cout << "Los archivos generados tendrán el formato <id>.paq y se procesarán en un subdirectorio 'procesados'." << endl;
            return EXIT_SUCCESS;
        } else {
            cerr << "Parámetro desconocido: " << arg << endl;
            return EXIT_FAILURE;
        }
    }

    // Validaciones

    if (directorio.empty()) {
        cerr << "Faltan parámetros requeridos o son inválidos." << endl;
        return EXIT_FAILURE;
    }

    fs::path dirPath(directorio);
    if (!fs::exists(dirPath)) {
        cerr << "El directorio especificado no existe: " << directorio << endl;
        return EXIT_FAILURE;
    }
	// Verificar si el directorio es el actual (.) o equivalente
	if (dirPath == "." || dirPath == "./" || dirPath == fs::current_path()) {
		cerr << "No se permite usar el directorio actual (./)." << endl;
		return EXIT_FAILURE;
	}

    if (generadores <= 0) {
        cerr << "El número de generadores debe ser mayor que 0." << endl;
        return EXIT_FAILURE;
    }
    
    if (consumidores <= 0) {
        cerr << "El número de consumidores debe ser mayor que 0." << endl;
        return EXIT_FAILURE;
    }
    
    if (paquetes <= 0) {
        cerr << "El número de paquetes debe ser mayor que 0." << endl;
        return EXIT_FAILURE;
    }
    

    paquetesProcesados = paquetes;
    totalPaquetes = paquetes;

    //borrar contenido del directorio
    try {
        for (const auto& entry : fs::directory_iterator(directorio)) {
            fs::remove_all(entry.path());
        }
      
    } catch (const fs::filesystem_error& e) {
        cerr << "Error al eliminar contenido: " << e.what() << endl;
        return EXIT_FAILURE;
	}

    // Crear subdirectorio "procesados"
    try {
		for (const auto& entry : fs::directory_iterator(directorio)) {
			fs::remove_all(entry.path());
    }
    fs::path subdirectorioProcesados = fs::path(directorio) / "procesados";
    fs::create_directory(subdirectorioProcesados);
    } catch (const fs::filesystem_error& e) {
    	cerr << "Error al manipular el sistema de archivos: " << e.what() << endl;
		return EXIT_FAILURE;
    }
	
    sem_unlink("BufferVirtualLimite");
    sem_unlink("BufferVirtualConsumidor");

	semaforoLimiteBuffer = sem_open("BufferVirtualLimite", O_CREAT, 0600, 10);
	if (semaforoLimiteBuffer == SEM_FAILED) {
		perror("sem_open BufferVirtualLimite");
		exit(EXIT_FAILURE);
	}

	semaforoConsumidor = sem_open("BufferVirtualConsumidor", O_CREAT, 0600, 0);
	if (semaforoConsumidor == SEM_FAILED) {
		perror("sem_open BufferVirtualConsumidor");
		exit(EXIT_FAILURE);
	}

		
	vector<thread> hilosProductores;
	vector<thread> hilosConsumidores;

	for (int i = 0; i < generadores; ++i) {
		hilosProductores.emplace_back(trabajoHiloProductor);
	}
	for (int i = 0; i < consumidores; ++i) {
		hilosConsumidores.emplace_back(trabajoHiloConsumidor);
	}

	for (auto& hilo : hilosProductores) {
		hilo.join();
	}
	for (auto& hilo : hilosConsumidores) {
		hilo.join();
	}
	
	// Mostrar resumen de paquetes procesados
	cout << "Total de paquetes procesados: " << paquetesProcesados << endl;
	cout << "Resumen de paquetes procesados:" << endl;
	for (const auto& sucursal : cantidadPaquetesPorSucursal) {
		cout << "Sucursal " << sucursal.first << ": " << sucursal.second << " paquetes procesados." << endl;
	}
	cout << "Peso total por sucursal:" << endl;
	for (const auto& sucursal : pesoPorSucursal) {
		cout << "Sucursal " << sucursal.first << ": " << sucursal.second << " kg." << endl;
	}
	limpiarRecursos();

	return EXIT_SUCCESS;
}
