#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]){
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

        int dependencias[50];
        int num_dependencias = 0;

        // Validacioness
        if (dependencias_str != NULL && dependencias_str[0] != '\n' && dependencias_str[0] != '\0') {
            char* dep = strtok(dependencias_str, ","); // x ej nos quedamos con el formato:1,2,3... solo con el 1
            while (dep != NULL) {
                dependencias[num_dependencias] = atoi(dep); 
                num_dependencias++;
                dep = strtok(NULL, ","); // permite pasar al siguiente!
            }
        }

        printf("ID: %s | Nombre: %s | Tiempo: %d | Num Dependencias: %d\n", id_actividad, nombre_actividad, tiempo_ms, num_dependencias);
    }
    
    // relleno xd
    fclose(archivo);
    return 0;
}