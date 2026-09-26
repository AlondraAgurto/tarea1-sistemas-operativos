#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ACTIVIDADES 1000 // Soporta hasta 1000 actividades según la rúbrica

typedef struct {
    char id[50];
    char nombre[100];
    int tiempo_ms;
    char dependencias[50][50];
    int num_dependencias;
} Actividad;

int main(int argc, char *argv[]){
    Actividad lista_actividades[MAX_ACTIVIDADES];
    int total_actividades = 0;
    //Por si ejecuta sin args
    if (argc < 2) {
        printf("Uso: %s <archivo_plan.txt>\n", argv[0]);
        return 1;
    }
    srand(time(NULL)); //semilla aleatoria
    // argv[1] toma el 1er argumento  al ejecutar programa:d
    // r para leer 
    // fopen para abrir
    // *archivo es un puntero
    FILE *archivo = fopen(argv[1], "r");

    char buffer[256]; // arreglo caracteres

    while (fgets(buffer, sizeof(buffer), archivo) != NULL) {
        printf("Linea leida: %s", buffer);

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

        // Copia dependencias a la estructura
        for (int i = 0; i < num_dependencias; i++) {
            strcpy(lista_actividades[total_actividades].dependencias[i], dependencias[i]);
        }

        total_actividades++;
    }
    
    // relleno xd
    fclose(archivo);
    return 0;
}