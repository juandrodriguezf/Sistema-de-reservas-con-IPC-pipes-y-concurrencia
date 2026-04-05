

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "../include/protocol.h"

char nombreAgente[MAX_NAME];
char archivoCSV[256];
char pipeRecibe[MAX_PIPE];
char pipeRespuesta[MAX_PIPE];

int horaActual_recibida = -1;

void print_usage(const char* p) {
    printf("Uso: %s -s nombre -a fileSolicitud -p pipeRecibe\n", p);
}


int leer_respuesta(Respuesta *r) {
    int fd = open(pipeRespuesta, O_RDONLY);
    if (fd < 0) {
        perror("[Agente] open pipeRespuesta");
        return -1;
    }
    ssize_t n = read(fd, r, sizeof(*r));
    close(fd);
    if (n <= 0) {
        return -1;
    }
    return 0;
}

int enviar_registro_y_recibir_hora() {
    // crear pipe de respuesta si no existe
    if (mkfifo(pipeRespuesta, 0666) != 0) {
        if (errno != EEXIST) {
            perror("[Agente] mkfifo pipeRespuesta");
            return -1;
        }
    }

    RegistroAgente reg;
    reg.tipo = MSG_REGISTRO;
    strncpy(reg.nombreAgente, nombreAgente, MAX_NAME);
    strncpy(reg.pipeRespuesta, pipeRespuesta, MAX_PIPE);

    int fd = open(pipeRecibe, O_WRONLY);
    if (fd < 0) {
        perror("[Agente] open pipeRecibe (escritura)");
        return -1;
    }
    write(fd, &reg, sizeof(reg));
    close(fd);

    // esperar respuesta con hora actual
    Respuesta r;
    if (leer_respuesta(&r) != 0) return -1;

    if (strncmp(r.respuesta, "HORA_ACTUAL:", 12) == 0) {
        horaActual_recibida = atoi(r.respuesta + 12);
        printf("[Agente %s] Hora actual recibida: %d\n", nombreAgente, horaActual_recibida);
    } else {
        printf("[Agente %s] Respuesta inesperada: %s\n", nombreAgente, r.respuesta);
    }
    return 0;
}

int enviar_solicitud_y_esperar(const Solicitud *s, char *out, size_t out_sz) {
    int fd = open(pipeRecibe, O_WRONLY);
    if (fd < 0) {
        perror("[Agente] open pipeRecibe para solicitud");
        return -1;
    }
    write(fd, s, sizeof(*s));
    close(fd);

    Respuesta r;
    if (leer_respuesta(&r) != 0) return -1;

    strncpy(out, r.respuesta, out_sz);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 7) {
        print_usage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s")) strncpy(nombreAgente, argv[++i], MAX_NAME);
        else if (!strcmp(argv[i], "-a")) strncpy(archivoCSV, argv[++i], sizeof(archivoCSV));
        else if (!strcmp(argv[i], "-p")) strncpy(pipeRecibe, argv[++i], MAX_PIPE);
    }

    // definir nombre de pipeRespuesta (único por agente)
    snprintf(pipeRespuesta, sizeof(pipeRespuesta), "pipe_%s_resp", nombreAgente);

    if (enviar_registro_y_recibir_hora() != 0) {
        fprintf(stderr, "[Agente] No pudo registrarse con controlador\n");
        return 1;
    }

    // Abrir CSV
    FILE* f = fopen(archivoCSV, "r");
    if (!f) {
        perror("[Agente] fopen CSV");
        
        return 1;
    }

    char linea[256];
    while (fgets(linea, sizeof(linea), f)) {
        // quitar \n
        linea[strcspn(linea, "\r\n")] = 0;
        if (strlen(linea) == 0) continue;

        Solicitud s;
        s.tipo = MSG_SOLICITUD;
        strncpy(s.nombreAgente, nombreAgente, MAX_NAME);
        s.familia[0] = 0;
        s.hora = 0;
        s.personas = 0;

        // parsear familia,hora,personas
        int got = sscanf(linea, "%[^,],%d,%d", s.familia, &s.hora, &s.personas);
        if (got != 3) {
            printf("[Agente %s] Linea mal formada: %s\n", nombreAgente, linea);
            continue;
        }

        
        if (s.hora < horaActual_recibida) {
            printf("[Agente %s] Solicitud extemporanea (no enviada): %s\n", nombreAgente, linea);
            continue;
        }

        printf("[Agente %s] Enviando solicitud: %s %d %d\n", nombreAgente, s.familia, s.hora, s.personas);

        char respuesta[MAX_MSG];
        if (enviar_solicitud_y_esperar(&s, respuesta, sizeof(respuesta)) != 0) {
            fprintf(stderr, "[Agente %s] Error esperando respuesta\n", nombreAgente);
            // intentar continuar
        } else {
            printf("[Agente %s] Respuesta: %s\n", nombreAgente, respuesta);
            
        }

        sleep(2);
    }

    fclose(f);

    // Al terminar el archivo, según enunciado: imprimir Agente <nombre> termina.
    printf("Agente %s termina.\n", nombreAgente);

    // Después de terminar, esperar a que el controlador mande FIN para terminar definitivamente
    // (el enunciado exige que el controlador termine a los agentes)
    printf("[Agente %s] Esperando FIN del controlador...\n", nombreAgente);
    while (1) {
        Respuesta r;
        if (leer_respuesta(&r) != 0) {
            // leer fallo, esperar un momento e intentar de nuevo
            sleep(1);
            continue;
        }
        if (r.tipo == MSG_FIN || strcmp(r.respuesta, "FIN") == 0) {
            printf("[Agente %s] Recibido FIN. Terminando.\n", nombreAgente);
            break;
        } else {
            // podrían llegar respuestas residuales; se ignoran
            // imprimimos por transparencia
            printf("[Agente %s] Mensaje posterior: %s\n", nombreAgente, r.respuesta);
        }
    }

    // limpieza: eliminar pipeRespuesta (si existe)
    unlink(pipeRespuesta);

    return 0;
}

