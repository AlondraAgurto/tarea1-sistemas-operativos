# tarea1-sistemas-operativos
Tarea 1 Sistemas Operativos - Planificador Dieciochero

Este repositorio contiene la primera tarea del curso de Sistemas Operativos. El programa es un simulador de actividades
modelado como un Grafo Acrílico (DAG) ¿, desarrollado en C utilizando procesos, tuberias (pipes) y manejo de señales,
cumpliendo con la regla de no usar hilos.

## Descripción general

El objetivo del programa es simular la ejecución de un conjunto de actividades interdependientes. Cada tarea cuenta con una duración específica (o asignada de forma aleatoria si no se especifica) y una lista de predecesores que deben concluir obligatoriamente antes de que dicha tarea pueda iniciar.

## Modo de uso
Requisitos:
* Compilador de C compatible con C17 (gcc).
* Sistema operativo Linux o similar.
## Compilación
Para compilar el programa de manera correcta, se debe ejecutar el siguiente comando:
  gcc -Wall -Wextra -std=c17 -o planificador main.c -lpthread
## Ejecución
El programa recibe como argumentos el arhivo de plan y el limite de concurrencia K:
  ./planificador <archivo.txt> <K>
Por ejemplo: ./planificador plan.txt 3
## Funciones implementadas:
  1. Lector del archivo (Parser): Lee el archivo de texto línea por línea para extraer el identificador, el nombre, el tiempo y las dependencias de cada actividad. Si una actividad no tiene un tiempo definido, el programa le asigna un número aleatorio entre 100 y 5000 milisegundos.
  2. Estructura del grafo: Almacena las tareas y sus relaciones para saber qué dependencias deben terminar antes de que un proceso pueda comenzar a ejecutarse.
  3. Control de concurrencia: Administra la creación de procesos hijos para asegurar que nunca haya más de $K$ actividades ejecutándose al mismo tiempo.
  4. Comunicación con pipes: Cada proceso utiliza tuberías para enviar una notificación a las actividades que dependen de él una vez que finaliza su trabajo.
  5. Manejo de señales y errores:
    * Si se presiona Ctrl+C (SIGINT), el programa intercepta la señal para detener todas las actividades de forma ordenada.
    * Si una tarea falla, el programa aísla el error y detiene únicamente la rama de dependencias afectada sin cerrar todo el simulador.
Decisiones de diseño:
  1. Uso de procesos en vez de hilos: Como lo exigía el enunciado, se trabajó únicamente con procesos (fork) y no con hilos. Esto evita problemas de memoria compartida y asegura que cada tarea funcione de forma aislada.
  2. Evitar espera activa: Para no gastar recursos de CPU innecesarios mientras se espera que se liberen cupos del límite $K$, el programa se apoya en el bloqueo natural de las lecturas en las tuberías y la espera de procesos.
  3. Comunicación por tuberías: Se eligieron los pipes porque permiten enviar mensajes directos y sencillos entre los procesos que tienen relación de dependencia, manteniendo el flujo del grafo de forma ordenada.
