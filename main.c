#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_ACTIVIDADES 1000 // Soporta hasta 1000 actividades según la rúbrica

typedef struct {
    char id[50];
    char nombre[100];
    int tiempo_ms;
    char dependencias[50][50];
    int num_dependencias;
    int estado; // 0 = pendiente, 1 = corriendo, 2 = terminado
} Actividad;

int main(int argc, char *argv[]){
    Actividad lista_actividades[MAX_ACTIVIDADES];
    int total_actividades = 0;
    
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

        char* id_actividad = strtok(buffer, ":");
        char* nombre_actividad= strtok(NULL, ":");
        char* tiempo_str= strtok(NULL, ":"); // porque es texto aún

        int tiempo_ms;
        
        if (tiempo_str == NULL || tiempo_str[0] == '\0') { // por si es null o 0
            tiempo_ms = rand() % 4901 + 100;
        } else {
            tiempo_ms = atoi(tiempo_str);
        }

        char* dependencias_str = strtok(NULL, ":"); // formato: 1,2,3...

        char dependencias[50][50];
        int num_dependencias = 0;

        if (dependencias_str != NULL && dependencias_str[0] != '\n' && dependencias_str[0] != '\0') {
            char* dep = strtok(dependencias_str, ","); 
            while (dep != NULL) {
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

    for (int i = 0; i < total_actividades; i++) { // Itera sobre todas las actividades
        pid_t pid = fork(); // Creamos un proceso hijo

        if (pid < 0) {
            // Error al crear el fork
            perror("Error en fork");
            exit(1);
        } 
        else if (pid == 0) {
            // Código del proceso hijo
            printf("  [Hijo] Actividad %s (%s) iniciada (PID: %d). Durmiendo %d ms...\n", 
                   lista_actividades[i].id, // Muestra el ID de la actividad
                   lista_actividades[i].nombre, // Muestra el nombre de la actividad
                   getpid(), 
                   lista_actividades[i].tiempo_ms);// Muestra el tiempo de trabajo en milisegundos
            
            // Simula el trabajo de la actividad durmiendo el tiempo especificado
            usleep(lista_actividades[i].tiempo_ms * 1000);

            printf("  [Hijo] Actividad %s finalizada.\n", lista_actividades[i].id); // Indica que la actividad ha finalizado
            exit(0); // El hijo termina su trabajo aquí para que no me deje cachos
        }
    }

    // El padre espera a que terminen todos sus hijos creados
    for (int i = 0; i < total_actividades; i++) {
        wait(NULL);
    }
    
    printf("⡞⠳⣄⣀⣠⠞SIMULACION FINALIZADA⡞⠳⣄⣀⣠⠞\n");

    // relleno xd
    fclose(archivo);
    return 0;
}