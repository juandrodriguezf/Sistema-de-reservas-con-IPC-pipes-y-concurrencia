#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_NAME 64
#define MAX_FAMILY 64
#define MAX_PIPE 128
#define MAX_MSG 256

// Tipos de mensajes
#define MSG_REGISTRO 1
#define MSG_SOLICITUD 2
#define MSG_RESPUESTA 3
#define MSG_FIN 4

// Registro inicial del agente
typedef struct {
    int tipo; // MSG_REGISTRO
    char nombreAgente[MAX_NAME];
    char pipeRespuesta[MAX_PIPE];
} RegistroAgente;

// Solicitud de reserva
typedef struct {
    int tipo; // MSG_SOLICITUD
    char familia[MAX_FAMILY];
    int hora;
    int personas;
    char nombreAgente[MAX_NAME];
} Solicitud;

// Respuesta del controlador
typedef struct {
    int tipo; // MSG_RESPUESTA o MSG_FIN
    char respuesta[MAX_MSG];
} Respuesta;

#endif


