#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_ACTIVIDADES 1000 // Soporta hasta 1000 actividades según la rúbrica
#define MAX_DEPENDENCIAS 50

typedef struct {
    char id[50];
    char nombre[100];
    int tiempo_ms;
    char dependencias[50][50];
    int num_dependencias;
    int estado; // 0 = pendiente, 1 = corriendo, 2 = terminado
    pid_t pid; // PID del proceso que ejecuta esta actividad
    int pipe_fd[2];
} Actividad;

int buscar_actividad(Actividad lista[], int total, const char *id) { // Busca la actividad por ID y devuelve su índice, o -1 si no se encuentra

    for (int i = 0; i < total; i++) { // Itera sobre la lista de actividades

        if (strcmp(lista[i].id, id) == 0) { // Compara el ID de la actividad actual con el ID buscado
            return i; // Si encuentra la actividad, devuelve su índice
        }
    }

    return -1; // Si no encuentra la actividad, devuelve -1 (toma tu cosa horrorosa KSDKDS)
}

int dependencias_terminadas(Actividad lista[], int total, int indice) { // Verifica si todas las dependencias de la actividad en el índice dado han terminado

    Actividad *actual = &lista[indice]; // Obtiene un puntero a la actividad actual

    printf("[DEBUG] Revisando actividad %s\n", actual->id); // Muestra un mensaje de depuración indicando que se está revisando la actividad actual

    for (int i = 0; i < actual->num_dependencias; i++) { // Itera sobre todas las dependencias de la actividad actual

        printf("[DEBUG]   Dependencia requerida: %s\n", // Muestra un mensaje de depuración indicando la dependencia que se está revisando
               actual->dependencias[i]); // Muestra un mensaje de depuración indicando la dependencia que se está revisando

        int posicion = buscar_actividad( // Busca la posición de la dependencia en la lista de actividades
            lista,
            total,
            actual->dependencias[i]
        );

        if (posicion == -1) { // Si no encuentra la dependencia en la lista de actividades, muestra un mensaje de error y retorna 0 (falso)
            printf("[DEBUG]   ERROR: dependencia no encontrada\n");
            return 0;
        }

        printf("[DEBUG]   Actividad encontrada: %s, estado = %d\n", // Muestra un mensaje de depuración indicando que se encontró la actividad de la dependencia y su estado
               lista[posicion].id,
               lista[posicion].estado);

        if (lista[posicion].estado != 2) { // Si la actividad de la dependencia no ha terminado (estado != 2), muestra un mensaje de depuración y retorna 0 (falso)
            printf("[DEBUG]   Dependencia todavía no terminada\n");
            return 0;
        }
    }

    printf("[DEBUG] Todas las dependencias de %s terminaron\n", // Muestra un mensaje de depuración indicando que todas las dependencias de la actividad actual han terminado
           actual->id);

    return 1;
}

void trim(char *cadena) { // Función para eliminar espacios al principio y al final de una cadena
    char *inicio = cadena;

    // Avanza mientras haya espacios al principio
    while (isspace((unsigned char)*inicio)) {
        inicio++;
    }

    // Mueve el contenido hacia el inicio
    memmove(cadena, inicio, strlen(inicio) + 1);

    // Elimina espacios al final
    int largo = strlen(cadena);

    while (largo > 0 && isspace((unsigned char)cadena[largo - 1])) {
        cadena[largo - 1] = '\0';
        largo--;
    }
}

int main(int argc, char *argv[]){ // Función principal del programa, recibe los argumentos de línea de comandos
    Actividad lista_actividades[MAX_ACTIVIDADES]; // Arreglo para almacenar las actividades leídas del archivo
    int total_actividades = 0; // Contador de actividades leídas
    
    // Verifica que se proporcionen los argumentos necesarios
    if (argc < 3) { // Se espera al menos 2 argumentos: archivo de plan y K
        printf("Uso: %s <archivo_plan.txt> <K>\n", argv[0]); // Muestra cómo usar el programa
        return 1; // Salida con error si no se proporcionan los argumentos
    }
    
    int K = atoi(argv[2]); // Límite de concurrencia permitido
    printf("Límite de concurrencia K = %d\n", K);

    srand(time(NULL)); //semilla aleatoria
    // argv[1] toma el 1er argumento  al ejecutar programa:d
    // r para leer 
    // fopen para abrir
    // *archivo es un puntero
    FILE *archivo = fopen(argv[1], "r");

    char buffer[256]; // arreglo caracteres

    while (fgets(buffer, sizeof(buffer), archivo) != NULL) {
        buffer[strcspn(buffer, "\r\n")] = 0; // Elimina el salto de línea al final de la línea leída

        printf("Linea leida: %s\n", buffer); // Muestra la línea leída para depuración
        
        // Divide la línea en partes usando ":" como delimitador
        char* id_actividad = strtok(buffer, ":"); 
        char* nombre_actividad = strtok(NULL, ":");
        char* tiempo_str = strtok(NULL, ":");

        // Elimina espacios al principio y al final de cada parte
        trim(id_actividad);
        trim(nombre_actividad);
        trim(tiempo_str);

        int tiempo_ms; // Variable para almacenar el tiempo en milisegundos
        
        if (tiempo_str == NULL || tiempo_str[0] == '\0') { // por si es null o 0
            tiempo_ms = rand() % 4901 + 100; // Genera un tiempo aleatorio entre 100 y 5000 ms si no se proporciona un tiempo específico
        } else { // Si se proporciona un tiempo específico
            tiempo_ms = atoi(tiempo_str); // Convierte la cadena de tiempo a un entero
        }

        char* dependencias_str = strtok(NULL, ":"); // formato: 1,2,3...
        if (dependencias_str != NULL) { // Si hay dependencias, elimina espacios al principio y al final
            trim(dependencias_str);
        }
        
        char dependencias[50][50];
        int num_dependencias = 0;

        if (dependencias_str != NULL && dependencias_str[0] != '\n' && dependencias_str[0] != '\0') { // Si hay dependencias, las divide usando "," como delimitador
            char* dep = strtok(dependencias_str, ",");  // Divide las dependencias en partes usando "," como delimitador
            while (dep != NULL) { // Mientras haya dependencias, elimina espacios al principio y al final de cada dependencia y las almacena en el arreglo de dependencias
                strcpy(dependencias[num_dependencias], dep);
                num_dependencias++;
                dep = strtok(NULL, ","); 
            }
        }

        printf("ID: %s | Nombre: %s | Tiempo: %d | Num Dependencias: %d\n", id_actividad, nombre_actividad, tiempo_ms, num_dependencias);
        // Guardar actividad en la lista
        strcpy(lista_actividades[total_actividades].id, id_actividad); // Copia ID a la estructura
        strcpy(lista_actividades[total_actividades].nombre, nombre_actividad); // Copia nombre a la estructura
        lista_actividades[total_actividades].tiempo_ms = tiempo_ms; // Asigna tiempo a la estructura
        lista_actividades[total_actividades].num_dependencias = num_dependencias; // Asigna número de dependencias a la estructura
        lista_actividades[total_actividades].estado = 0; // Inicializa estado como pendiente

        // Copia dependencias a la estructura
        for (int i = 0; i < num_dependencias; i++) {
            strcpy(lista_actividades[total_actividades].dependencias[i], dependencias[i]);
        }

        total_actividades++;
    }
    
    printf("────୨ৎ────────\n");
    printf("\n⡞⠳⣄⣀⣠⠞INICIANDO SIMULACION DE PROCESOS \n");

    int procesos_activos = 0;
    int actividades_terminadas = 0;

    while (actividades_terminadas < total_actividades) {
        
       printf("\n[DEBUG] Nueva iteracion. Terminadas: %d/%d | Activos: %d\n",
       actividades_terminadas,
       total_actividades,
       procesos_activos);

        // PRIMERA PARTE:
        for (int i = 0; i < total_actividades; i++) { // Itera sobre todas las actividades para verificar cuáles pueden ejecutarse

            // Si ya está terminada o ejecutándose, la ignoramos.
            if (lista_actividades[i].estado != 0) {
                continue;
            }

            // Si ya alcanzamos el límite K, no podemos crear
            // más procesos por ahora.
            if (procesos_activos >= K) {
                break;
            }

            // Revisamos si todas sus dependencias terminaron.
          /*  if (!dependencias_terminadas(lista_actividades,
                                        total_actividades,
                                        i)) {
                continue;
            } */
            if (!dependencias_terminadas(lista_actividades,
                            total_actividades,
                            i)) {

            printf("[DEBUG] Actividad %s NO puede ejecutarse. Dependencias pendientes.\n", // Muestra un mensaje de depuración indicando que la actividad no puede ejecutarse debido a dependencias pendientes
                lista_actividades[i].id);

                continue;
            }

            printf("[DEBUG] Actividad %s puede ejecutarse.\n",
                lista_actividades[i].id);
            /*
            * La actividad está lista y existe espacio
            * dentro del límite de concurrencia.
            */
            printf("[DEBUG] Intentando crear proceso para actividad %s...\n", // Muestra un mensaje de depuración indicando que se intentará crear un proceso para la actividad
            lista_actividades[i].id);// Muestra el ID de la actividad

            if (pipe(lista_actividades[i].pipe_fd) < 0) { // Crea un pipe para la actividad actual y verifica si hubo un error al crearlo
                perror("Error al crear el pipe");
                exit(1);
            } 
            pid_t pid = fork();

            if (pid < 0) {

                perror("Error en fork");
                exit(1);

            } else if (pid == 0) {

                /*
                * PROCESO HIJO
                *
                * El hijo se encarga de simular la ejecución
                * de la actividad.
                */
                close(lista_actividades[i].pipe_fd[0]); // Cierra el extremo de lectura del pipe en el proceso hijo

                printf(
                    "  [Hijo] Actividad %s (%s) iniciada "
                    "(PID: %d). Duración: %d ms\n",
                    lista_actividades[i].id,
                    lista_actividades[i].nombre,
                    getpid(),
                    lista_actividades[i].tiempo_ms
                );

                // Simula el tiempo de ejecución de la actividad.
                usleep(lista_actividades[i].tiempo_ms * 1000);

                // Envía un mensaje al proceso padre indicando que la actividad ha terminado.
                char mensaje[100]; // Buffer para el mensaje que se enviará al proceso padre
                snprintf(mensaje, sizeof(mensaje), "Insumo de [%s] listo", lista_actividades[i].id); // Crea un mensaje indicando que la actividad ha terminado
                write(lista_actividades[i].pipe_fd[1], mensaje, strlen(mensaje) + 1); // Escribe el mensaje en el pipe para que el proceso padre lo reciba
                close(lista_actividades[i].pipe_fd[1]); // Cierra escritura

                printf( //  Muestra un mensaje indicando que la actividad ha finalizadooo
                    "  [Hijo] Actividad %s finalizada "
                    "(PID: %d)\n",
                    lista_actividades[i].id,
                    getpid()
                );

                // El hijo termina para no continuar ejecutando
                // el planificador del proceso padre.
                exit(0);

            } else {

                /*
                * PROCESO PADRE
                *
                * Guarda el PID y actualiza el estado
                * de la actividad.
                */

                close(lista_actividades[i].pipe_fd[1]); // Cierra el extremo de escritura del pipe en el proceso padre

                lista_actividades[i].pid = pid;
                lista_actividades[i].estado = 1;

                procesos_activos++;

                printf(
                    "  [Padre] Actividad %s ejecutándose "
                    "(PID: %d). Procesos activos: %d/%d\n",
                    lista_actividades[i].id,
                    pid,
                    procesos_activos,
                    K
                );
            }
        }

        /*
        * SEGUNDA PARTE:
        * Si existe al menos un proceso ejecutándose,
        * esperamos a que termine uno.
        */
        if (procesos_activos > 0) {

            int estado_hijo;

            pid_t pid_terminado = waitpid(
                -1,
                &estado_hijo,
                0
            );

            if (pid_terminado == -1) {
                perror("Error en waitpid");
                exit(1);
            }

            /*
            * Buscamos qué actividad corresponde
            * al PID que acaba de terminar.
            */
            for (int i = 0; i < total_actividades; i++) {

                if (lista_actividades[i].pid == pid_terminado) { // Si encontramos la actividad correspondiente al PID que terminó, actualizamos su estado y contamos el número de procesos activos y actividades terminadas

                    lista_actividades[i].estado = 2; // Marcamos la actividad como terminada

                    procesos_activos--;  // Decrementamos el contador de procesos activos
                    actividades_terminadas++; // Incrementamos el contador de actividades terminadas

                    // Leemos el mensaje del pipe del hijo
                    char buffer_pipe[100]; // Buffer para almacenar el mensaje recibido del proceso hijo
                    ssize_t bytes_leidos = read(lista_actividades[i].pipe_fd[0], buffer_pipe, sizeof(buffer_pipe)); // Leemos el mensaje del pipe del hijo y almacenamos el número de bytes leídos
                    if (bytes_leidos > 0) { // Si se leyeron bytes del pipe, mostramos el mensaje recibido
                        printf("  [Padre] Mensaje recibido -> %s\n", buffer_pipe);
                    }
                    close(lista_actividades[i].pipe_fd[0]); // Cierra lectura

                    printf( // Muestra un mensaje indicando que la actividad ha terminado y el número de procesos activos y actividades terminadas
                        "  [Padre] Actividad %s terminó. "
                        "Procesos activos: %d/%d\n",
                        lista_actividades[i].id,
                        procesos_activos,
                        K
                    );

                    break;
                }
            }
            printf("[DEBUG] waitpid termino. Actividades terminadas: %d/%d\n",
            actividades_terminadas,
            total_actividades);
        }
    }
    
    printf("⡞⠳⣄⣀⣠⠞SIMULACION FINALIZADA⡞⠳⣄⣀⣠⠞\n");

    // relleno xd
    fclose(archivo);
    return 0;
}