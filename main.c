#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define DEBUG 0 // Cambiar a 1 para activar mensajes de depuración
#define DEBUG_PRINT(...) do { if (DEBUG) printf(__VA_ARGS__); } while (0) // Macro para imprimir mensajes de depuración

#define TIEMPO_MIN_MS 100
#define TIEMPO_MAX_MS 5000
#define MAX_INSUMOS 4096 // Tamaño máximo del buffer con los insumos que recibe un hijo

// Estados de una actividad
enum {
    PENDIENTE = 0, // Todavía no se ejecuta
    CORRIENDO = 1, // Hay un proceso hijo ejecutándola
    TERMINADO = 2, // Terminó bien
    FALLIDO   = 3, // Falló internamente
    ABORTADO  = 4  // No se ejecutó porque una dependencia falló (o llegó la Seremi)
};

// Resultado de intentar lanzar una actividad
enum { LANZAR_OK = 0, LANZAR_REINTENTAR = 1, LANZAR_FALLO = -1 };

typedef struct {
    char id[50];
    char nombre[100];
    int tiempo_ms; // Tiempo de ejecución simulado en milisegundos (negativo si es inválido)
    char *deps_texto; // Texto original de las dependencias (se libera después de procesarlas)
    int *dependencias;// Índices de las actividades de las que depende
    int num_dependencias; // Cuántas dependencias tiene
    int *dependientes; // Índices de las actividades que dependen de esta
    int num_dependientes;  // Cuántos dependientes tiene
    int pendientes; // Cuántas dependencias le faltan por terminar
    int invalido;  // Si es 1, la actividad tiene dependencias inválidas y no se ejecutará
    int estado;  // Estado actual de la actividad (PENDIENTE, CORRIENDO, TERMINADO, FALLIDO, ABORTADO)
    pid_t pid; // PID del proceso hijo que ejecuta la actividad (si está corriendo)
    int fd_lectura; // Pipe de lectura del hijo al padre (para recibir el mensaje de fin de actividad)
    char mensaje[100];  // Mensaje que el hijo envía al padre al terminar (insumo para los dependientes)
} Actividad;

// Variables globales
static Actividad *lista = NULL;
static int total = 0;
static int K = 1;

static int *cola = NULL;          // Cola de actividades listas para ejecutarse
static int cabeza = 0, fin_cola = 0;
static int *en_ejecucion = NULL;  // Índices de las actividades que están corriendo
static int activos = 0;
static int *pila_aborto = NULL;   // Pila auxiliar para abortar ramas sin recursión

static int *tabla_hash = NULL;    // ID -> índice de actividad
static int mascara_hash = 0;

static int n_ok = 0, n_fallidas = 0, n_abortadas = 0;

static sigset_t mascara_original;
static volatile sig_atomic_t interrumpido = 0;

/* ---------- Señales ---------- */
static void manejador_sigint(int sig) {
    (void)sig;
    interrumpido = 1; // Solo levantamos la bandera; el planificador actúa en su loop
}

static void manejador_sigchld(int sig) {
    (void)sig; // Solo existe para despertar a sigsuspend cuando termina un hijo
}

/* ---------- Utilidades ---------- */
static void trim(char *cadena) { // Elimina espacios al principio y al final de una cadena
    if (cadena == NULL) {
        return;
    }

    char *inicio = cadena;

    while (isspace((unsigned char)*inicio)) { // Avanza mientras haya espacios al principio
        inicio++;
    }

    memmove(cadena, inicio, strlen(inicio) + 1); // Mueve el contenido hacia el inicio

    size_t largo = strlen(cadena); // Elimina espacios al final

    while (largo > 0 && isspace((unsigned char)cadena[largo - 1])) {
        cadena[largo - 1] = '\0';
        largo--;
    }
}

static unsigned long hash_id(const char *s) {
    unsigned long h = 5381;
    int c;

    while ((c = (unsigned char)*s++)) {
        h = h * 33 + (unsigned long)c;
    }

    return h;
}

// Busca la actividad por ID y devuelve su índice, o -1 si no se encuentra (O(1) promedio)
static int buscar_actividad(const char *id) {
    unsigned long pos = hash_id(id) & (unsigned long)mascara_hash;

    while (tabla_hash[pos] != -1) {
        if (strcmp(lista[tabla_hash[pos]].id, id) == 0) {
            return tabla_hash[pos];
        }
        pos = (pos + 1) & (unsigned long)mascara_hash;
    }

    return -1;
}

static void dormir_ms(int ms) { // Simula el tiempo de ejecución sin usar usleep
    struct timespec req = { ms / 1000, (long)(ms % 1000) * 1000000L };
    struct timespec resto;

    while (nanosleep(&req, &resto) == -1 && errno == EINTR) {
        req = resto;
    }
}

static void escribir_todo(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t w = write(fd, buf, n);

        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return; // EPIPE u otro error: el lector ya no está
        }

        buf += w;
        n -= (size_t)w;
    }
}

static ssize_t leer_mensaje(int fd, char *destino, size_t max) {
    size_t leidos = 0;

    while (leidos < max - 1) {
        ssize_t r = read(fd, destino + leidos, max - 1 - leidos);

        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (r == 0) {
            break; // EOF: el hijo cerró su extremo
        }

        leidos += (size_t)r;
    }

    destino[leidos] = '\0';
    return (ssize_t)leidos;
}

static int resueltas(void) {
    return n_ok + n_fallidas + n_abortadas;
}

/* ---------- Carga del plan ---------- */
static int cargar_plan(FILE *archivo) {
    int capacidad = 100; // Capacidad inicial
    lista = malloc(capacidad * sizeof(Actividad));

    if (lista == NULL) {
        perror("Error al asignar memoria inicial");
        return -1;
    }

    char *linea = NULL;
    size_t cap_linea = 0;
    char vacio[1] = { '\0' };

    while (getline(&linea, &cap_linea, archivo) != -1) { // getline soporta líneas de cualquier largo
        linea[strcspn(linea, "\r\n")] = '\0'; // Elimina el salto de línea
        trim(linea);

        if (linea[0] == '\0' || linea[0] == '#') { // Líneas vacías o comentarios
            continue;
        }

        DEBUG_PRINT("Linea leida: %s\n", linea);

        // Divide la línea a mano (strtok se salta los campos vacíos, y el tiempo puede venir vacío)
        char *id = linea;
        char *c1 = strchr(linea, ':');

        if (c1 == NULL) {
            fprintf(stderr, "Advertencia: línea ignorada (formato inválido): %s\n", linea);
            continue;
        }

        *c1 = '\0';
        char *nombre = c1 + 1;
        char *c2 = strchr(nombre, ':');

        if (c2 == NULL) {
            fprintf(stderr, "Advertencia: línea ignorada (formato inválido): %s\n", id);
            continue;
        }

        *c2 = '\0';
        char *tiempo_str = c2 + 1;
        char *deps_str = vacio;
        char *c3 = strchr(tiempo_str, ':');

        if (c3 != NULL) {
            *c3 = '\0';
            deps_str = c3 + 1;
        }

        for (char *p = deps_str; *p; p++) { // Acepta el formato [1, 2, 3] o 1, 2, 3
            if (*p == '[' || *p == ']') {
                *p = ' ';
            }
        }

        trim(id);
        trim(nombre);
        trim(tiempo_str);
        trim(deps_str);

        if (id[0] == '\0') {
            fprintf(stderr, "Advertencia: línea ignorada (ID vacío)\n");
            continue;
        }

        int tiempo_ms;

        if (tiempo_str[0] == '\0') { // Sin tiempo: aleatorio entre 100 y 5000 ms
            tiempo_ms = rand() % (TIEMPO_MAX_MS - TIEMPO_MIN_MS + 1) + TIEMPO_MIN_MS;
        } else {
            char *fin;
            errno = 0;
            long v = strtol(tiempo_str, &fin, 10);

            if (*fin != '\0' || errno != 0 || v < 0 || v > INT_MAX) {
                fprintf(stderr, "Advertencia: actividad %s tiene un tiempo inválido (%s), fallará al ejecutarse\n",
                        id, tiempo_str);
                tiempo_ms = -1;
            } else {
                tiempo_ms = (int)v;
            }
        }

        if (total >= capacidad) { // Por si el espacio x.x
            capacidad *= 2;
            Actividad *temp = realloc(lista, capacidad * sizeof(Actividad));

            if (temp == NULL) {
                perror("Error al redimensionar memoria");
                free(linea);
                return -1;
            }

            lista = temp;
        }

        Actividad *act = &lista[total];
        memset(act, 0, sizeof(Actividad));
        snprintf(act->id, sizeof(act->id), "%s", id);
        snprintf(act->nombre, sizeof(act->nombre), "%s", nombre);
        act->tiempo_ms = tiempo_ms;
        act->estado = PENDIENTE;
        act->fd_lectura = -1;

        if (deps_str[0] != '\0') {
            act->deps_texto = strdup(deps_str);

            if (act->deps_texto == NULL) {
                perror("Error al guardar dependencias");
                free(linea);
                return -1;
            }
        }

        DEBUG_PRINT("ID: %s | Nombre: %s | Tiempo: %d | Dependencias: %s\n",
                    act->id, act->nombre, act->tiempo_ms,
                    act->deps_texto ? act->deps_texto : "(ninguna)");

        total++;
    }

    free(linea);
    return 0;
}

// Convierte los IDs de las dependencias en índices y arma las listas de dependientes
static int resolver_dependencias(void) {
    int tam = 16;

    while (tam < total * 2) {
        tam <<= 1;
    }

    tabla_hash = malloc(tam * sizeof(int));

    if (tabla_hash == NULL) {
        perror("Error al crear la tabla hash");
        return -1;
    }

    mascara_hash = tam - 1;

    for (int i = 0; i < tam; i++) {
        tabla_hash[i] = -1;
    }

    for (int i = 0; i < total; i++) { // Inserta todos los IDs y detecta duplicados
        unsigned long pos = hash_id(lista[i].id) & (unsigned long)mascara_hash;

        while (tabla_hash[pos] != -1) {
            if (strcmp(lista[tabla_hash[pos]].id, lista[i].id) == 0) {
                fprintf(stderr, "ERROR: el ID '%s' está duplicado en el plan\n", lista[i].id);
                return -1;
            }
            pos = (pos + 1) & (unsigned long)mascara_hash;
        }

        tabla_hash[pos] = i;
    }

    for (int i = 0; i < total; i++) { // Resuelve las dependencias de cada actividad
        Actividad *act = &lista[i];

        if (act->deps_texto == NULL) {
            continue;
        }

        int max = 1;

        for (char *c = act->deps_texto; *c; c++) {
            if (*c == ',') {
                max++;
            }
        }

        act->dependencias = malloc(max * sizeof(int));

        if (act->dependencias == NULL) {
            perror("Error al asignar dependencias");
            return -1;
        }

        char *p = act->deps_texto;

        while (p != NULL) {
            char *coma = strchr(p, ',');

            if (coma != NULL) {
                *coma = '\0';
            }

            trim(p); // Cada dependencia se limpia por separado ("1, 2" -> "1" y "2")

            if (p[0] != '\0') {
                int j = buscar_actividad(p);

                if (j < 0) {
                    fprintf(stderr, "Advertencia: la actividad %s depende de '%s', que no existe\n", act->id, p);
                    act->invalido = 1;
                } else if (j == i) {
                    fprintf(stderr, "Advertencia: la actividad %s depende de sí misma\n", act->id);
                    act->invalido = 1;
                } else {
                    act->dependencias[act->num_dependencias++] = j;
                }
            }

            p = (coma != NULL) ? coma + 1 : NULL;
        }

        free(act->deps_texto);
        act->deps_texto = NULL;
    }

    for (int i = 0; i < total; i++) { // Cuenta cuántos dependientes tiene cada actividad
        for (int k = 0; k < lista[i].num_dependencias; k++) {
            lista[lista[i].dependencias[k]].num_dependientes++;
        }
    }

    int *llenado = calloc(total, sizeof(int));

    if (llenado == NULL) {
        perror("Error al asignar memoria");
        return -1;
    }

    for (int i = 0; i < total; i++) {
        if (lista[i].num_dependientes > 0) {
            lista[i].dependientes = malloc(lista[i].num_dependientes * sizeof(int));

            if (lista[i].dependientes == NULL) {
                perror("Error al asignar dependientes");
                free(llenado);
                return -1;
            }
        }
    }

    for (int i = 0; i < total; i++) {
        for (int k = 0; k < lista[i].num_dependencias; k++) {
            int j = lista[i].dependencias[k];
            lista[j].dependientes[llenado[j]++] = i;
        }

        lista[i].pendientes = lista[i].num_dependencias;
    }

    free(llenado);
    return 0;
}

/* ---------- Tolerancia a fallos ---------- */

// Aborta todas las actividades que dependen (directa o indirectamente) de 'origen'.
// Solo se aborta esa rama: el resto del plan sigue ejecutándose.
static void abortar_rama(int origen) {
    int tope = 0;
    pila_aborto[tope++] = origen;

    while (tope > 0) {
        int actual = pila_aborto[--tope];

        for (int k = 0; k < lista[actual].num_dependientes; k++) {
            int d = lista[actual].dependientes[k];

            if (lista[d].estado != PENDIENTE) {
                continue;
            }

            lista[d].estado = ABORTADO;
            n_abortadas++;

            printf("  [Padre] Actividad %s abortada: depende de %s, que no terminó bien\n",
                   lista[d].id, lista[actual].id);

            pila_aborto[tope++] = d;
        }
    }
}

/* ---------- Ejecución de actividades ---------- */
static int clasificar_error(int e) {
    // Falta momentánea de recursos: conviene reintentar cuando termine algún hijo
    if (e == EMFILE || e == ENFILE || e == EAGAIN || e == ENOMEM) {
        return LANZAR_REINTENTAR;
    }

    return LANZAR_FALLO;
}

static int lanzar_actividad(int i) {
    Actividad *act = &lista[i];
    int c2p[2]; // hijo -> padre (mensaje de fin de actividad)
    int p2c[2]; // padre -> hijo (insumos de sus dependencias)

    DEBUG_PRINT("[DEBUG] Intentando crear proceso para actividad %s...\n", act->id);

    if (pipe(c2p) < 0) {
        int e = errno;
        perror("Error al crear el pipe (hijo->padre)");
        return clasificar_error(e);
    }

    if (pipe(p2c) < 0) {
        int e = errno;
        perror("Error al crear el pipe (padre->hijo)");
        close(c2p[0]);
        close(c2p[1]);
        return clasificar_error(e);
    }

    fflush(stdout); // Evita que el hijo herede (y duplique) salida pendiente
    pid_t pid = fork();

    if (pid < 0) {
        int e = errno;
        perror("Error en fork");
        close(c2p[0]);
        close(c2p[1]);
        close(p2c[0]);
        close(p2c[1]);
        return clasificar_error(e);
    }

    if (pid == 0) {
        /*
         * PROCESO HIJO
         *
         * Recibe los insumos de sus dependencias, simula la actividad
         * y avisa al padre por el pipe si terminó bien.
         */
        signal(SIGINT, SIG_IGN);  // La Seremi la atiende el padre; el hijo no debe morir con Ctrl+C
        signal(SIGCHLD, SIG_DFL);
        sigprocmask(SIG_SETMASK, &mascara_original, NULL);

        close(c2p[0]);
        close(p2c[1]);

        for (int k = 0; k < activos; k++) { // Cierra los pipes de los hermanos que heredó
            close(lista[en_ejecucion[k]].fd_lectura);
        }

        char insumos[MAX_INSUMOS];
        char tmp[512];
        size_t usados = 0;
        ssize_t r;

        while ((r = read(p2c[0], tmp, sizeof(tmp))) != 0) { // Lee todos los insumos hasta EOF
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }

            size_t copiar = (size_t)r;
            size_t libre = sizeof(insumos) - 1 - usados;

            if (copiar > libre) {
                copiar = libre;
            }

            memcpy(insumos + usados, tmp, copiar);
            usados += copiar;
        }

        insumos[usados] = '\0';
        close(p2c[0]);

        if (usados > 0) {
            char *guardado;

            for (char *ins = strtok_r(insumos, "\n", &guardado); ins != NULL; ins = strtok_r(NULL, "\n", &guardado)) {
                printf("  [Hijo] Actividad %s recibió insumo -> %s\n", act->id, ins);
            }
        }

        printf(
            "  [Hijo] Actividad %s (%s) iniciada "
            "(PID: %d). Duración: %d ms\n",
            act->id,
            act->nombre,
            getpid(),
            act->tiempo_ms
        );
        fflush(stdout);

        if (act->tiempo_ms > 0) {
            dormir_ms(act->tiempo_ms); // Simula el tiempo de ejecución de la actividad
        }
		if (act->tiempo_ms < 0) {
		printf("  [Hijo] Actividad %s FALLÓ: tiempo inválido\n", act->id);
		fflush(stdout);
		_exit(1);
		}

        // Envía un mensaje al padre indicando que la actividad ha terminado
        char mensaje[100];
        snprintf(mensaje, sizeof(mensaje), "Insumo de [%s] listo", act->id);
        escribir_todo(c2p[1], mensaje, strlen(mensaje));
        close(c2p[1]);

        printf(
            "  [Hijo] Actividad %s finalizada "
            "(PID: %d)\n",
            act->id,
            getpid()
        );
        fflush(stdout);

        _exit(0); // _exit para no volver a ejecutar el planificador del padre
    }

    /*
     * PROCESO PADRE
     *
     * Guarda el PID, actualiza el estado y le entrega al hijo
     * los mensajes (insumos) de las actividades de las que depende.
     */
    close(c2p[1]);
    close(p2c[0]);

    act->pid = pid;
    act->estado = CORRIENDO;
    act->fd_lectura = c2p[0];

    for (int k = 0; k < act->num_dependencias; k++) {
        const char *msg = lista[act->dependencias[k]].mensaje;
        escribir_todo(p2c[1], msg, strlen(msg));
        escribir_todo(p2c[1], "\n", 1);
    }

    close(p2c[1]);

    en_ejecucion[activos++] = i;

    printf(
        "  [Padre] Actividad %s ejecutándose "
        "(PID: %d). Procesos activos: %d/%d\n",
        act->id,
        (int)pid,
        activos,
        K
    );

    return LANZAR_OK;
}

static void procesar_terminacion(pid_t pid, int estado_hijo) {
    int pos = -1;

    for (int k = 0; k < activos; k++) { // Buscamos qué actividad corresponde al PID que terminó
        if (lista[en_ejecucion[k]].pid == pid) {
            pos = k;
            break;
        }
    }

    if (pos < 0) {
        return;
    }

    int i = en_ejecucion[pos];
    en_ejecucion[pos] = en_ejecucion[--activos];
    Actividad *act = &lista[i];

    ssize_t bytes_leidos = leer_mensaje(act->fd_lectura, act->mensaje, sizeof(act->mensaje));
    close(act->fd_lectura);
    act->fd_lectura = -1;

    if (WIFEXITED(estado_hijo) && WEXITSTATUS(estado_hijo) == 0) {
        act->estado = TERMINADO;
        n_ok++;

        if (bytes_leidos <= 0) {
            snprintf(act->mensaje, sizeof(act->mensaje), "Insumo de [%s] listo", act->id);
        }

        printf("  [Padre] Mensaje recibido -> %s\n", act->mensaje);
        printf(
            "  [Padre] Actividad %s terminó. "
            "Procesos activos: %d/%d\n",
            act->id,
            activos,
            K
        );

        // Avisamos a los dependientes: si ya no les falta nada, quedan listos
        for (int k = 0; k < act->num_dependientes; k++) {
            int d = act->dependientes[k];

            if (--lista[d].pendientes == 0 && lista[d].estado == PENDIENTE) {
                cola[fin_cola++] = d;
            }
        }
    } else {
        act->estado = FALLIDO;
        n_fallidas++;

        if (WIFEXITED(estado_hijo)) {
            printf("  [Padre] Actividad %s FALLÓ (código de salida %d). Procesos activos: %d/%d\n",
                   act->id, WEXITSTATUS(estado_hijo), activos, K);
        } else if (WIFSIGNALED(estado_hijo)) {
            printf("  [Padre] Actividad %s FALLÓ (señal %d). Procesos activos: %d/%d\n",
                   act->id, WTERMSIG(estado_hijo), activos, K);
        } else {
            printf("  [Padre] Actividad %s FALLÓ. Procesos activos: %d/%d\n", act->id, activos, K);
        }

        abortar_rama(i); // Solo se cancela lo que dependía de esta actividad
    }
}

// Aborta todas las actividades (llegó la Seremi, o hubo un error irrecuperable)
static void abortar_todo(int por_seremi) {
    if (por_seremi) {
        printf("\n[Seremi] ¡¡Llegó la inspección (SIGINT)!! Abortando todas las actividades...\n");
    } else {
        printf("\n[Padre] Error irrecuperable. Abortando todas las actividades...\n");
    }

    for (int k = 0; k < activos; k++) {
        kill(lista[en_ejecucion[k]].pid, SIGTERM);
    }

    for (int k = 0; k < activos; k++) {
        Actividad *act = &lista[en_ejecucion[k]];
        int st;

        while (waitpid(act->pid, &st, 0) == -1 && errno == EINTR) {
            /* reintentar */
        }

        if (act->fd_lectura >= 0) {
            close(act->fd_lectura);
            act->fd_lectura = -1;
        }

        act->estado = ABORTADO;
        n_abortadas++;
        printf("  [Padre] Actividad %s (PID: %d) abortada\n", act->id, (int)act->pid);
    }

    activos = 0;
    int canceladas = 0;

    for (int i = 0; i < total; i++) {
        if (lista[i].estado == PENDIENTE) {
            lista[i].estado = ABORTADO;
            n_abortadas++;
            canceladas++;
        }
    }

    printf("  [Padre] %d actividades pendientes canceladas\n", canceladas);
}

static void liberar_memoria(void) {
    if (lista != NULL) {
        for (int i = 0; i < total; i++) {
            free(lista[i].deps_texto);
            free(lista[i].dependencias);
            free(lista[i].dependientes);
        }
    }

    free(lista);
    free(cola);
    free(en_ejecucion);
    free(pila_aborto);
    free(tabla_hash);
}

int main(int argc, char *argv[]) {
	if (argc != 3) {
		fprintf(stderr, "Uso: %s <archivo_plan.txt> <K>\n", argv[0]);
		return EXIT_FAILURE;
	}

    char *fin;
    errno = 0;
    long k_arg = strtol(argv[2], &fin, 10);

    if (*fin != '\0' || errno != 0 || k_arg < 1 || k_arg > INT_MAX) {
        fprintf(stderr, "Error: K debe ser un entero mayor o igual a 1\n");
        return 1;
    }

    K = (int)k_arg; // Límite de concurrencia permitido

    printf("Límite de concurrencia K = %d\n", K);

    srand(time(NULL)); // Semilla aleatoria

    FILE *archivo = fopen(argv[1], "r");

    if (archivo == NULL) {
        perror("Error al abrir el archivo del plan");
        return 1;
    }

    if (cargar_plan(archivo) < 0) {
        fclose(archivo);
        liberar_memoria();
        return 1;
    }

    fclose(archivo);

    if (total == 0) {
        printf("El plan no tiene actividades.\n");
        liberar_memoria();
        return 0;
    }

    cola = malloc(total * sizeof(int));
    en_ejecucion = malloc(total * sizeof(int));
    pila_aborto = malloc((total + 1) * sizeof(int));

    if (cola == NULL || en_ejecucion == NULL || pila_aborto == NULL) {
        perror("Error al asignar memoria");
        liberar_memoria();
        return 1;
    }

    if (resolver_dependencias() < 0) {
        liberar_memoria();
        return 1;
    }

    // Actividades con dependencias inválidas: se abortan junto con su rama
    for (int i = 0; i < total; i++) {
        if (lista[i].invalido && lista[i].estado == PENDIENTE) {
            lista[i].estado = ABORTADO;
            n_abortadas++;
            printf("  [Padre] Actividad %s abortada: plan inválido (dependencia inexistente)\n", lista[i].id);
            abortar_rama(i);
        }
    }

    // Las actividades sin dependencias pendientes quedan listas desde el inicio
    for (int i = 0; i < total; i++) {
        if (lista[i].estado == PENDIENTE && lista[i].pendientes == 0) {
            cola[fin_cola++] = i;
        }
    }

    // Manejo de señales. SIGINT y SIGCHLD se bloquean y solo se reciben dentro de
    // sigsuspend: así no se pierde ninguna señal entre revisar la bandera y esperar.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = manejador_sigint;
    sigaction(SIGINT, &sa, NULL);
    sa.sa_handler = manejador_sigchld;
    sigaction(SIGCHLD, &sa, NULL);
    signal(SIGPIPE, SIG_IGN); // Si un hijo muere antes de leer sus insumos, no matar al padre

    sigset_t bloqueadas;
    sigemptyset(&bloqueadas);
    sigaddset(&bloqueadas, SIGINT);
    sigaddset(&bloqueadas, SIGCHLD);
    sigprocmask(SIG_BLOCK, &bloqueadas, &mascara_original);

    printf("────୨ৎ────────\n");
    printf("\n⡞⠳⣄⣀⣠⠞INICIANDO SIMULACION DE PROCESOS \n");

    int error_interno = 0;

    while (resueltas() < total) {

        DEBUG_PRINT("\n[DEBUG] Nueva iteracion. Resueltas: %d/%d | Activos: %d\n", resueltas(), total, activos);

        if (interrumpido) {
            break;
        }

        // PRIMERA PARTE:
        // Lanzamos actividades listas mientras haya espacio dentro del límite K.
        while (activos < K && cabeza < fin_cola) {
            int i = cola[cabeza++];
            int r = lanzar_actividad(i);

            if (r == LANZAR_REINTENTAR) {
                if (activos > 0) { // Reintentamos cuando termine algún hijo y libere recursos
                    cola[--cabeza] = i;
                    break;
                }
                r = LANZAR_FALLO; // No hay a quién esperar: falla definitiva
            }

            if (r == LANZAR_FALLO) {
                lista[i].estado = FALLIDO;
                n_fallidas++;
                printf("  [Padre] Actividad %s FALLÓ al crearse. Se aborta su rama\n", lista[i].id);
                abortar_rama(i);
            }
        }

        /*
         * SEGUNDA PARTE:
         * Si existe al menos un proceso ejecutándose, esperamos a que termine uno.
         */
        if (activos > 0) {
            int estado_hijo;
            pid_t pid_terminado = waitpid(-1, &estado_hijo, WNOHANG);

            if (pid_terminado > 0) {
                procesar_terminacion(pid_terminado, estado_hijo);
            } else if (pid_terminado == 0) {
                sigsuspend(&mascara_original); // Duerme hasta SIGCHLD o SIGINT
            } else if (errno != EINTR) {
                perror("Error en waitpid");
                error_interno = 1;
                break;
            }
        } else if (cabeza == fin_cola) {
            // Nada corriendo ni listo, pero quedan pendientes: el plan tiene un ciclo
            printf("  [Padre] ERROR: el plan tiene dependencias circulares. Se abortan las actividades bloqueadas\n");

            for (int i = 0; i < total; i++) {
                if (lista[i].estado == PENDIENTE) {
                    lista[i].estado = ABORTADO;
                    n_abortadas++;
                    printf("  [Padre] Actividad %s abortada: dependencias que nunca se cumplen\n", lista[i].id);
                }
            }

            break;
        }
    }

    if (resueltas() < total) { // Salimos antes de terminar: Seremi o error
        abortar_todo(interrumpido && !error_interno);
    }

    printf("⡞⠳⣄⣀⣠⠞SIMULACION FINALIZADA⡞⠳⣄⣀⣠⠞\n");
    printf("Resumen: %d exitosas | %d fallidas | %d abortadas | %d en total\n",
           n_ok, n_fallidas, n_abortadas, total);

    int codigo = interrumpido ? 130 : 0;
    liberar_memoria();
    return codigo;
}
