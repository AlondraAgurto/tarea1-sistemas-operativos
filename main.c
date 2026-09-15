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

        /*
            char* token = strtok(input, " ");
            while (token)
            {
                puts(token);
                token = strtok(NULL, " ");
            }
        */

        char* id_actividad = strtok(buffer, ":");
        char* nombre_actividad= strtok(NULL, ":");
        char* tiempo_ms= strtok(NULL, ":");
        char* dependencias= strtok(NULL, ":");
        printf("ID: %s | Nombre: %s | Tiempo: %s | Dependencias: %s\n", id_actividad, nombre_actividad, tiempo_ms, dependencias);
    }
    
    // relleno xd
    fclose(archivo);
    return 0;
}