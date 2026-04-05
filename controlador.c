
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include "../include/protocol.h"

#define MAX_AGENTES 64
#define MAX_RESERVAS 1024

// Parámetros de simulación
int horaActual = 0;
int horaFin = 0;
int segHoras = 1;
int aforo = 0;
char pipeRecibe[MAX_PIPE];

// Mutex para estructuras compartidas
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Estructuras
typedef struct {
    char familia[MAX_FAMILY];
    int horaInicio; // hora de inicio (W)
    int personas;
    char agente[MAX_NAME];
} Reserva;

Reserva reservas[MAX_RESERVAS];
int reservas_count = 0;


int ocupacion[25]; // ocupacion[h] = personas en hora h

// Registro de agentes
typedef struct {
    char nombre[MAX_NAME];
    char pipeRespuesta[MAX_PIPE];
} Agente;

Agente agentes[MAX_AGENTES];
int totalAgentes = 0;

// Estadísticas
int total_solicitudes = 0;
int total_aceptadas_en_su_hora = 0;
int total_reprogramadas = 0;
int total_negadas = 0;

// Flag para terminar receptor cuando reloj termine
int finalizar = 0;


void safe_write_to_pipe(const char *pipeName, void *buf, size_t sz) {
    int fd = open(pipeName, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
       
        return;
    }
    write(fd, buf, sz);
    close(fd);
}

void registrar_agente(const RegistroAgente* reg) {
    pthread_mutex_lock(&mutex);
    if (totalAgentes < MAX_AGENTES) {
        strncpy(agentes[totalAgentes].nombre, reg->nombreAgente, MAX_NAME);
        strncpy(agentes[totalAgentes].pipeRespuesta, reg->pipeRespuesta, MAX_PIPE);
        totalAgentes++;
        printf("[Controlador] Agente registrado: %s -> pipe %s\n",
               reg->nombreAgente, reg->pipeRespuesta);
    } else {
        fprintf(stderr, "[Controlador] Maximo agentes alcanzado, no registro %s\n", reg->nombreAgente);
    }
    pthread_mutex_unlock(&mutex);
}

Agente* buscar_agente_por_nombre(const char* nombre) {
    for (int i = 0; i < totalAgentes; i++) {
        if (strcmp(agentes[i].nombre, nombre) == 0) return &agentes[i];
    }
    return NULL;
}

int disponible_en_bloque(int hora) {
    // verifica que hora y hora+1 estén dentro de rango
    if (hora < 7 || hora+1 > 19) return 0;
    if (ocupacion[hora] + 0 /*placeholder*/ >= 0) {
        
        return 1;
    }
    return 1;
}

int puede_ubicar(int hora, int personas) {
    if (hora < 7 || hora+1 > 19) return 0;
    if (ocupacion[hora] + personas <= aforo && ocupacion[hora+1] + personas <= aforo) return 1;
    return 0;
}

void reservar_bloque(int hora, int personas, const char* familia, const char* agente_nombre) {
    if (reservas_count >= MAX_RESERVAS) {
        fprintf(stderr, "Limite de reservas alcanzado\n");
        return;
    }
    Reserva r;
    strncpy(r.familia, familia, MAX_FAMILY);
    r.horaInicio = hora;
    r.personas = personas;
    strncpy(r.agente, agente_nombre, MAX_NAME);
    reservas[reservas_count++] = r;
    ocupacion[hora] += personas;
    ocupacion[hora+1] += personas;
}

int buscar_bloque_libre_siguiente(int desde, int personas) {
    
    for (int h = desde; h <= horaFin - 1; h++) {
        if (h < 7 || h+1 > 19) continue;
        if (puede_ubicar(h, personas)) return h;
    }
    return -1;
}

void imprimir_peticion(const Solicitud* s) {
    printf("[Controlador] Peticion: agente=%s familia=%s hora=%d personas=%d\n",
           s->nombreAgente, s->familia, s->hora, s->personas);
}

// Procesa una solicitud y devuelve un string en 'out' (preparado para enviar)
void procesar_solicitud(const Solicitud* s, char* out, size_t out_sz) {
    pthread_mutex_lock(&mutex);
    total_solicitudes++;
    imprimir_peticion(s);

    // Validaciones
    if (s->personas <= 0) {
        snprintf(out, out_sz, "NEGADA: personas invalidas");
        total_negadas++;
        pthread_mutex_unlock(&mutex);
        return;
    }
    if (s->hora < 7 || s->hora > 19) {
        snprintf(out, out_sz, "NEGADA_VOLVER_OTRO_DIA: hora fuera de rango");
        total_negadas++;
        pthread_mutex_unlock(&mutex);
        return;
    }
    if (s->personas > aforo) {
        snprintf(out, out_sz, "NEGADA_VOLVER_OTRO_DIA: personas > aforo");
        total_negadas++;
        pthread_mutex_unlock(&mutex);
        return;
    }

    // Si solicita una hora mayor a horaFin -> negar
    if (s->hora > horaFin) {
        snprintf(out, out_sz, "NEGADA_VOLVER_OTRO_DIA: hora > horaFin");
        total_negadas++;
        pthread_mutex_unlock(&mutex);
        return;
    }

    // Si solicita una hora pasada (extemporanea)
    if (s->hora < horaActual) {
        // buscar bloque a partir de horaActual
        int h = buscar_bloque_libre_siguiente(horaActual, s->personas);
        if (h >= 0) {
            // reservar en h
            reservar_bloque(h, s->personas, s->familia, s->nombreAgente);
            total_reprogramadas++;
            snprintf(out, out_sz, "REPROGRAMADA:%d", h);
            pthread_mutex_unlock(&mutex);
            return;
        } else {
            snprintf(out, out_sz, "NEGADA_EXTEMPORANEA");
            total_negadas++;
            pthread_mutex_unlock(&mutex);
            return;
        }
    }

    // Si la hora solicitada y siguiente tienen espacio -> aceptar
    if (puede_ubicar(s->hora, s->personas)) {
        reservar_bloque(s->hora, s->personas, s->familia, s->nombreAgente);
        total_aceptadas_en_su_hora++;
        snprintf(out, out_sz, "ACEPTADA:%d", s->hora);
        pthread_mutex_unlock(&mutex);
        return;
    } else {
        // buscar otro bloque posterior dentro del periodo
        int h = buscar_bloque_libre_siguiente(s->hora+1, s->personas); // buscar después de la solicitada
        if (h >= 0) {
            reservar_bloque(h, s->personas, s->familia, s->nombreAgente);
            total_reprogramadas++;
            snprintf(out, out_sz, "REPROGRAMADA:%d", h);
            pthread_mutex_unlock(&mutex);
            return;
        } else {
            // intentar desde horaActual si hora solicitada > horaActual? Already tried forward
            h = buscar_bloque_libre_siguiente(horaActual, s->personas);
            if (h >= 0) {
                reservar_bloque(h, s->personas, s->familia, s->nombreAgente);
                total_reprogramadas++;
                snprintf(out, out_sz, "REPROGRAMADA:%d", h);
                pthread_mutex_unlock(&mutex);
                return;
            }
            // no hay bloque
            snprintf(out, out_sz, "NEGADA_VOLVER_OTRO_DIA");
            total_negadas++;
            pthread_mutex_unlock(&mutex);
            return;
        }
    }
}

// Hilo reloj: avanza cada segHoras segundos y procesa entradas/salidas
void* hiloReloj(void* arg) {
    (void)arg;
    while (1) {
        sleep(segHoras);
        pthread_mutex_lock(&mutex);
        if (horaActual > horaFin) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        printf("\n[Controlador] ----- Avanzando hora: ahora es %d -----\n", horaActual);
        
        int salen_personas = 0;
        printf("[Controlador] Salen: ");
        int first = 1;
        for (int i = 0; i < reservas_count; i++) {
            if (reservas[i].horaInicio + 2 == horaActual) {
                if (!first) printf(", ");
                printf("%s(%d)", reservas[i].familia, reservas[i].personas);
                salen_personas += reservas[i].personas;
                first = 0;
            }
        }
        if (first) printf("Nadie");
        printf("\n");

        // Quienes entran: reservas con horaInicio == horaActual
        int entran_personas = 0;
        printf("[Controlador] Entran: ");
        first = 1;
        for (int i = 0; i < reservas_count; i++) {
            if (reservas[i].horaInicio == horaActual) {
                if (!first) printf(", ");
                printf("%s(%d)", reservas[i].familia, reservas[i].personas);
                entran_personas += reservas[i].personas;
                first = 0;
            }
        }
        if (first) printf("Nadie");
        printf("\n");

        // imprimir ocupacion actual por horaActual
        if (horaActual >= 7 && horaActual <= 19) {
            printf("[Controlador] Ocupacion hora %d = %d personas\n", horaActual, ocupacion[horaActual]);
        }

        // Avanzar hora
        horaActual++;
        if (horaActual > horaFin) {
            printf("[Controlador] Hora final alcanzada (%d). Preparando reporte y cerrando.\n", horaFin);
            pthread_mutex_unlock(&mutex);
            break;
        }
        pthread_mutex_unlock(&mutex);
    }
    // cuando sale del loop, señal de finalización
    finalizar = 1;
    return NULL;
}

// Hilo receptor: lee pipeRecibe y procesa registros y solicitudes
void* hiloReceptor(void* arg) {
    (void)arg;
    // Abrir pipe para lectura
    int fd = open(pipeRecibe, O_RDONLY);
    if (fd < 0) {
        perror("[Controlador] open pipeRecibe");
        return NULL;
    }

    while (!finalizar) {
        
        char buffer[512];
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            
            usleep(100000);
            continue;
        }
        int tipo = ((int*)buffer)[0];

        if (tipo == MSG_REGISTRO) {
            RegistroAgente *reg = (RegistroAgente*)buffer;
            registrar_agente(reg);

            // enviar hora actual al agente (por su pipe)
            Respuesta r;
            r.tipo = MSG_RESPUESTA;
            snprintf(r.respuesta, sizeof(r.respuesta), "HORA_ACTUAL:%d", horaActual);
            safe_write_to_pipe(reg->pipeRespuesta, &r, sizeof(r));
        } else if (tipo == MSG_SOLICITUD) {
            Solicitud *s = (Solicitud*)buffer;
            char out[MAX_MSG];
            out[0] = 0;
            procesar_solicitud(s, out, sizeof(out));

            // enviar respuesta al agente
            Agente* ag = buscar_agente_por_nombre(s->nombreAgente);
            if (ag) {
                Respuesta r;
                r.tipo = MSG_RESPUESTA;
                strncpy(r.respuesta, out, sizeof(r.respuesta));
                safe_write_to_pipe(ag->pipeRespuesta, &r, sizeof(r));
            } else {
                fprintf(stderr, "[Controlador] No se encontro agente %s para responder\n", s->nombreAgente);
            }
        } else {
            // mensaje desconocido
            fprintf(stderr, "[Controlador] Mensaje desconocido tipo=%d\n", tipo);
        }
    }

    close(fd);
    return NULL;
}

void generar_reporte() {
    pthread_mutex_lock(&mutex);
    printf("\n\n[Controlador] ===== REPORTE FINAL =====\n");
    printf("Total solicitudes recibidas: %d\n", total_solicitudes);
    printf("Solicitudes aceptadas en su hora: %d\n", total_aceptadas_en_su_hora);
    printf("Solicitudes reprogramadas: %d\n", total_reprogramadas);
    printf("Solicitudes negadas: %d\n", total_negadas);

    
    int maxp = -1, minp = 1<<30;
    for (int h = 7; h <= 19; h++) {
        if (ocupacion[h] > maxp) maxp = ocupacion[h];
        if (ocupacion[h] < minp) minp = ocupacion[h];
    }
    printf("Horas pico (max personas = %d): ", maxp);
    int first = 1;
    for (int h = 7; h <= 19; h++) {
        if (ocupacion[h] == maxp) {
            if (!first) printf(", ");
            printf("%d", h);
            first = 0;
        }
    }
    printf("\n");

    printf("Horas valle (min personas = %d): ", minp);
    first = 1;
    for (int h = 7; h <= 19; h++) {
        if (ocupacion[h] == minp) {
            if (!first) printf(", ");
            printf("%d", h);
            first = 0;
        }
    }
    printf("\n");

    // opcional: listar todas las reservas
    printf("\nReservas registradas:\n");
    for (int i = 0; i < reservas_count; i++) {
        printf(" - Familia %s (agente %s): inicio=%d, personas=%d\n",
               reservas[i].familia, reservas[i].agente, reservas[i].horaInicio, reservas[i].personas);
    }

    pthread_mutex_unlock(&mutex);
}

// enviar FIN a todos los agentes antes de terminar
void notificar_fin_a_agentes() {
    pthread_mutex_lock(&mutex);
    Respuesta r;
    r.tipo = MSG_FIN;
    snprintf(r.respuesta, sizeof(r.respuesta), "FIN");
    for (int i = 0; i < totalAgentes; i++) {
        safe_write_to_pipe(agentes[i].pipeRespuesta, &r, sizeof(r));
    }
    pthread_mutex_unlock(&mutex);
}

void print_usage(const char* prog) {
    printf("Uso: %s -i horaIni -f horaFin -s segHoras -t total -p pipeRecibe\n", prog);
}

int main(int argc, char* argv[]) {
    if (argc != 11) {
        print_usage(argv[0]);
        return 1;
    }

    // Inicializar ocupacion
    for (int i = 0; i < 25; i++) ocupacion[i] = 0;

    // Parse args
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i")) horaActual = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-f")) horaFin = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s")) segHoras = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t")) aforo = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-p")) strncpy(pipeRecibe, argv[++i], MAX_PIPE);
    }

    // Validaciones básicas
    if (horaActual < 7 || horaActual > 19) {
        fprintf(stderr, "horaIni fuera del rango 7..19\n"); return 1;
    }
    if (horaFin < 7 || horaFin > 19) {
        fprintf(stderr, "horaFin fuera del rango 7..19\n"); return 1;
    }
    if (horaActual > horaFin) {
        fprintf(stderr, "horaIni > horaFin\n"); return 1;
    }
    if (segHoras <= 0) segHoras = 1;
    if (aforo <= 0) { fprintf(stderr, "aforo (-t) debe ser > 0\n"); return 1; }

    // Crear pipe nominal si no existe
    if (mkfifo(pipeRecibe, 0666) != 0) {
        if (errno != EEXIST) {
            perror("mkfifo pipeRecibe");
            return 1;
        }
    }

    printf("[Controlador] Iniciado. horaIni=%d horaFin=%d segHoras=%d aforo=%d pipe=%s\n",
           horaActual, horaFin, segHoras, aforo, pipeRecibe);

    pthread_t tr, trec;
    pthread_create(&tr, NULL, hiloReloj, NULL);
    pthread_create(&trec, NULL, hiloReceptor, NULL);

    // Esperar a que reloj termine
    pthread_join(tr, NULL);

    // cuando reloj terminó -> dar tiempo para procesar peticiones finales brevemente
    sleep(1);

    // indicar finalizar y esperar que receptor termine
    finalizar = 1;
    pthread_join(trec, NULL);

    // Generar reporte
    generar_reporte();

    // Notificar FIN a agentes
    notificar_fin_a_agentes();

    // Limpieza
    unlink(pipeRecibe);
    printf("[Controlador] Finalizado limpiamente.\n");
    return 0;
}


