# Sistema de reservas con IPC (pipes) y concurrencia

Proyecto académico de **Sistemas Operativos**: simulación de reservas para un escenario con **aforo por hora** (bloques de dos horas consecutivas), donde varios procesos **agente** envían solicitudes a un proceso **controlador** central mediante **tuberías con nombre (FIFOs)** y el controlador usa **hilos** y **mutex** para sincronizar el estado compartido.

## Contenido

- Comunicación entre procesos con **pipes nominales** (`mkfifo`, lectura/escritura).
- **Controlador**: hilo **reloj** (avanza la hora simulada), hilo **receptor** (lee mensajes del pipe común), registro de agentes, política de aceptación/reprogramación/negación y **reporte final** (estadísticas, picos/valles de ocupación, listado de reservas).
- **Agente**: registro en el controlador, recepción de la hora actual, lectura de solicitudes desde **CSV** y envío secuencial de peticiones con espera de respuesta en un pipe propio (`pipe_<nombre>_resp`).

## Requisitos

- **GCC** con soporte **POSIX** y **pthreads** (por ejemplo Linux, macOS o **WSL** en Windows).
- Las APIs usadas (`mkfifo`, `unlink`, FIFOs) no son nativas de Windows sin un entorno tipo Cygwin/MSYS2/WSL.

## Estructura recomendada del repositorio

Los fuentes incluyen `#include "../include/protocol.h"`, pensados para compilarse desde `src/`. El archivo `makefile.txt` espera esta disposición:

```text
.
├── include/
│   └── protocol.h
├── src/
│   ├── controlador.c
│   └── agente.c
├── bin/                 # se crea al compilar (mkdir bin)
├── makefile.txt         # opcional: renombrar a Makefile
├── solicitudes.csv
└── solicitudes-f.csv
```

Si `protocol.h` y los `.c` están en la raíz del proyecto, muévelos a `include/` y `src/` respectivamente (o ajusta los `#include` y el Makefile) para que coincidan con lo anterior.

## Compilación

Crear la carpeta de binarios y compilar (desde la raíz del proyecto):

```bash
mkdir -p bin
make -f makefile.txt
```

Si renombraste `makefile.txt` a `Makefile`, basta con `make`.

Flags relevantes: `-Wall -Wextra -pthread -Iinclude` (definidos en el Makefile).

## Ejecución

1. **Arrancar el controlador** (crea el FIFO común si no existe):

   ```bash
   ./bin/controlador -i <horaIni> -f <horaFin> -s <segundosPorHora> -t <aforo> -p <pipeRecibe>
   ```

   - `horaIni` / `horaFin`: rango **7–19** (simulación del día).
   - `segundosPorHora`: cada cuántos segundos reales avanza una hora simulada.
   - `aforo`: cupo máximo de personas por hora en el bloque considerado.
   - `pipeRecibe`: nombre del FIFO por el que los agentes escriben (ej. `mi_pipe`).

2. **En otras terminales**, lanzar uno o más **agentes** (después de que el controlador esté en marcha):

   ```bash
   ./bin/agente -s <nombreAgente> -a <archivo.csv> -p <mismo_pipeRecibe>
   ```

   Cada agente crea su FIFO de respuesta `pipe_<nombreAgente>_resp`.

### Ejemplo mínimo

Terminal 1:

```bash
./bin/controlador -i 7 -f 19 -s 2 -t 50 -p entrada
```

Terminal 2:

```bash
./bin/agente -s vendedor1 -a solicitudes.csv -p entrada
```

## Formato del CSV (agente)

Una solicitud por línea:

```text
Familia,hora,personas
```

Ejemplo:

```text
Rojas,8,9
Perez,15,6
```

Las solicitudes con hora **menor** que la hora actual recibida del controlador no se envían (el agente las marca como extemporáneas).

## Protocolo de mensajes (`protocol.h`)

| Tipo            | Uso |
|-----------------|-----|
| `MSG_REGISTRO`  | El agente se da de alta e indica el FIFO donde recibir respuestas. |
| `MSG_SOLICITUD` | Familia, hora deseada, personas y nombre del agente. |
| `MSG_RESPUESTA` | Texto con el resultado (p. ej. `ACEPTADA:…`, `REPROGRAMADA:…`, `NEGADA…`). |
| `MSG_FIN`       | El controlador avisa fin de simulación a los agentes registrados. |

## Política de reserva (resumen)

- Cada reserva ocupa **dos horas consecutivas** (`hora` y `hora+1`); el aforo se comprueba en ambas.
- Si hay cupo en la hora pedida: **aceptada** en esa hora.
- Si no hay cupo: se intenta **reprogramar** a un bloque libre posterior (o, si aplica, manejo de solicitud extemporánea respecto al reloj simulado).
- Casos inválidos o sin alternativa: **negada** (mensajes detallados en la respuesta).

## Limpieza

```bash
make -f makefile.txt clean
```

Elimina los binarios en `bin/`. Los FIFOs creados en tiempo de ejecución pueden quedar en el directorio de trabajo; bórralos manualmente si hace falta (`rm entrada pipe_*_resp` en Unix).

## Autores y licencia

Completar con nombres del equipo y la licencia o política de uso que corresponda al curso o al repositorio.
