#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_ACTIVIDADES 10000 // Soporta hasta 1000 actividades según la rúbrica
#define MAX_DEPENDENCIAS 50

typedef struct { // Estructura que representa una actividad en el planificador
    char id[50];
    char nombre[100];
    int tiempo_ms;
    char dependencias[50][50];
    int num_dependencias;
    int estado; // 0 = pendiente, 1 = corriendo, 2 = terminado
    pid_t pid; 
    int pipe_c2p[2]; // Pipe para comunicación del hijo al padre
    int pipe_p2c[2]; // Pipe para comunicación del padre al hijo
    char mensaje[100]; // Mensaje que el hijo enviará al padre al terminar
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
    int capacidad_actividades = 100; // Capacidad inicial
    int total_actividades = 0; // Contador de actividades leídas
    Actividad *lista_actividades = malloc(capacidad_actividades * sizeof(Actividad)); // Asigna memoria dinámica para la lista de actividades

    if (lista_actividades == NULL) {
        perror("Error al asignar memoria inicial");
        return 1;
    }
    
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
    if (archivo == NULL) {
        perror("Error al abrir el archivo");
        return 1;
    }

    char *linea = NULL;
    size_t cap_linea = 0;

    while (getline(&linea, &cap_linea, archivo) != -1) {
        linea[strcspn(linea, "\r\n")] = '\0';
        trim(linea);

        if (linea[0] == '\0' || linea[0] == '#') {
            continue; // Ignorar líneas vacías o comentarios
        }

        // Copiamos la línea a un buffer temporal para no romper el original con strtok
        char buffer_copia[512];
        snprintf(buffer_copia, sizeof(buffer_copia), "%s", linea);

        char *id_actividad = strtok(buffer_copia, ":");
        char *nombre_actividad = strtok(NULL, ":");
        char *tiempo_str = strtok(NULL, ":");
        char *dependencias_str = strtok(NULL, ":");

        if (!id_actividad || !nombre_actividad) continue;

        trim(id_actividad);
        trim(nombre_actividad);

        int tiempo_ms;
        if (!tiempo_str || tiempo_str[0] == '\0') {
            tiempo_ms = rand() % 4901 + 100;
        } else {
            trim(tiempo_str);
            tiempo_ms = atoi(tiempo_str);
        }

        char dependencias[50][50];
        int num_dependencias = 0;

        if (dependencias_str != NULL) {
            trim(dependencias_str);
            // Limpiar corchetes si vienen como [1,2]
            for (char *p = dependencias_str; *p; p++) {
                if (*p == '[' || *p == ']') *p = ' ';
            }
            trim(dependencias_str);

            char *dep = strtok(dependencias_str, ",");
            while (dep != NULL && num_dependencias < 50) {
                trim(dep);
                if (dep[0] != '\0') {
                    strcpy(dependencias[num_dependencias], dep);
                    num_dependencias++;
                }
                dep = strtok(NULL, ",");
            }
        }

        // Guardar en la lista (manteniendo tu lógica de realloc)
        strcpy(lista_actividades[total_actividades].id, id_actividad);
        strcpy(lista_actividades[total_actividades].nombre, nombre_actividad);
        lista_actividades[total_actividades].tiempo_ms = tiempo_ms;
        lista_actividades[total_actividades].num_dependencias = num_dependencias;
        lista_actividades[total_actividades].estado = 0;

        for (int i = 0; i < num_dependencias; i++) {
            strcpy(lista_actividades[total_actividades].dependencias[i], dependencias[i]);
        }

        total_actividades++;

        if (total_actividades >= capacidad_actividades) {
            capacidad_actividades *= 2;
            Actividad *temp = realloc(lista_actividades, capacidad_actividades * sizeof(Actividad));
            if (temp == NULL) {
                perror("Error al reasignar memoria");
                free(linea);
                fclose(archivo);
                return 1;
            }
            lista_actividades = temp;
        }
    }
    free(linea);
    
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

            if (pipe(lista_actividades[i].pipe_c2p) < 0 || pipe(lista_actividades[i].pipe_p2c) < 0) {
                perror("Error al crear los pipes");
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
                close(lista_actividades[i].pipe_c2p[0]); // Cierra lectura p2c si no se usa
                close(lista_actividades[i].pipe_p2c[1]); // Cierra escritura p2c si no se usa

                char buffer_insumos[512];
                ssize_t r = read(lista_actividades[i].pipe_p2c[0], buffer_insumos, sizeof(buffer_insumos) - 1);
                if (r > 0) {
                    buffer_insumos[r] = '\0';
                    printf("  [Hijo] Actividad %s recibió insumos:\n%s", lista_actividades[i].id, buffer_insumos);
                }
                close(lista_actividades[i].pipe_p2c[0]);

                printf("  [Hijo] Actividad %s (%s) iniciada (PID: %d). Duración: %d ms\n",
                    lista_actividades[i].id,
                    lista_actividades[i].nombre,
                    getpid(),
                    lista_actividades[i].tiempo_ms);

                // Simula el tiempo de ejecución
                usleep(lista_actividades[i].tiempo_ms * 1000);

                // Prepara y envía el mensaje de término al padre
                char mensaje[100];
                snprintf(mensaje, sizeof(mensaje), "Insumo de [%s] listo", lista_actividades[i].id);
                write(lista_actividades[i].pipe_c2p[1], mensaje, strlen(mensaje) + 1);
                close(lista_actividades[i].pipe_c2p[1]); // Cierra escritura tras enviar

                printf("  [Hijo] Actividad %s finalizada (PID: %d)\n",
                    lista_actividades[i].id,
                    getpid());

                exit(0);

            } else {

                /*
                * PROCESO PADRE
                *
                * Guarda el PID y actualiza el estado
                * de la actividad.
                */

                close(lista_actividades[i].pipe_c2p[1]); // Cierra escritura en padre
                close(lista_actividades[i].pipe_p2c[0]); // Cierra lectura del pipe padre->hijo si no lo usas
                
                for (int d = 0; d < lista_actividades[i].num_dependencias; d++) {
                    int pos_dep = buscar_actividad(lista_actividades, total_actividades, lista_actividades[i].dependencias[d]);
                    if (pos_dep != -1) {
                        write(lista_actividades[i].pipe_p2c[1], lista_actividades[pos_dep].mensaje, strlen(lista_actividades[pos_dep].mensaje));
                        write(lista_actividades[i].pipe_p2c[1], "\n", 1);
                    }
                }

                close(lista_actividades[i].pipe_p2c[1]); // Cierra el extremo de escritura del pipe en el proceso padre

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

                    // Leemos el mensaje del pipe del hijo y lo guardamos en la estructura
                    ssize_t bytes_leidos = read(lista_actividades[i].pipe_c2p[0], lista_actividades[i].mensaje, sizeof(lista_actividades[i].mensaje) - 1);
                    if (bytes_leidos > 0) {
                        lista_actividades[i].mensaje[bytes_leidos] = '\0';
                        printf("  [Padre] Mensaje recibido -> %s\n", lista_actividades[i].mensaje);
                    } else {
                        snprintf(lista_actividades[i].mensaje, sizeof(lista_actividades[i].mensaje), "Insumo de [%s] listo", lista_actividades[i].id);
                    }
                    close(lista_actividades[i].pipe_c2p[0]); // Cierra lectura

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
        } else {
            // Protección contra bloqueos si no hay procesos activos ni terminados pero falta avanzar
            printf("  [Padre] ERROR: No hay procesos activos y quedan actividades sin resolver.\n");
            break;
        }
    }
    
    printf("⡞⠳⣄⣀⣠⠞SIMULACION FINALIZADA⡞⠳⣄⣀⣠⠞\n");

    free(lista_actividades);
    fclose(archivo);
    return 0;
}