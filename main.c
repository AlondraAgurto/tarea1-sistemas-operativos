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
    }
    
    // relleno xd
    fclose(archivo);
    return 0;
}