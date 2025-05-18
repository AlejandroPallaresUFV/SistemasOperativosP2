#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <time.h>
#include "estructuras.h"

Config leer_configuracion(const char *ruta) {
    Config config;
    FILE *f = fopen(ruta, "r");
    if (!f) {
        perror("Error abriendo config.txt");
        exit(1);
    }

    char linea[256];
    while (fgets(linea, sizeof(linea), f)) {
        if (linea[0] == '#' || linea[0] == '\n') continue;

        if (strncmp(linea, "LIMITE_RETIRO=", 14) == 0) {
            sscanf(linea + 14, "%d", &config.limite_retiro);
        } else if (strncmp(linea, "LIMITE_TRANSFERENCIA=", 22) == 0) {
            sscanf(linea + 22, "%d", &config.limite_transferencia);
        } else if (strncmp(linea, "UMBRAL_RETIROS=", 16) == 0) {
            sscanf(linea + 16, "%d", &config.umbral_retiros);
        } else if (strncmp(linea, "UMBRAL_TRANSFERENCIAS=", 24) == 0) {
            sscanf(linea + 24, "%d", &config.umbral_transferencias);
        } else if (strncmp(linea, "NUM_HILOS=", 10) == 0) {
            sscanf(linea + 10, "%d", &config.num_hilos);
        } else if (strncmp(linea, "ARCHIVO_CUENTAS=", 16) == 0) {
            sscanf(linea + 16, "%99s", config.archivo_cuentas);
        } else if (strncmp(linea, "ARCHIVO_LOG=", 12) == 0) {
            sscanf(linea + 12, "%99s", config.archivo_log);
        }
    }

    fclose(f);
    return config;
}

typedef struct {
    int cuenta;
    int retiros_consecutivos;
    float ultimo_monto;
    int uso_concurrente;
    char ultima_operacion[20];
    char ultimo_destino[20];
} EstadoCuenta;

EstadoCuenta cuentas[100];
int num_cuentas = 0;

EstadoCuenta *get_estado(int cuenta) {
    for (int i = 0; i < num_cuentas; i++) {
        if (cuentas[i].cuenta == cuenta) return &cuentas[i];
    }
    cuentas[num_cuentas].cuenta = cuenta;
    cuentas[num_cuentas].retiros_consecutivos = 0;
    cuentas[num_cuentas].uso_concurrente = 0;
    num_cuentas++;
    return &cuentas[num_cuentas - 1];
}

int main(int argc, char *argv[]) {

    char archivo_transacciones[] = "transacciones.log";

    Config cfg = leer_configuracion("config.txt");
    printf("Configuración cargada. LIMITE_RETIRO = %d\n", cfg.limite_retiro);

    if (argc < 2) {
        printf("Uso: %s <fd_alertas>\n", argv[0]);
        return 1;
    }

    int pipe_fd = atoi(argv[1]);

    key_t key = ftok("monitor.c", 65);
    int cola_id = msgget(key, 0666 | IPC_CREAT);
    if (cola_id == -1) {
        perror("msgget");
        return 1;
    }

    while (1) {
        struct msgbuf mensaje;
        if (msgrcv(cola_id, &mensaje, sizeof(mensaje.mtext), 0, 0) == -1) {
            perror("msgrcv");
            continue;
        }

        if (strcmp(mensaje.mtext, "FIN") == 0) {
            printf("Finalizando\n");
            break;
        }

        int cuenta;
        char tipo[20], destino[20] = "";
        float monto;

        int campos = sscanf(mensaje.mtext, "%d,%[^,],%f,%s", &cuenta, tipo, &monto, destino);
        EstadoCuenta *estado = get_estado(cuenta);

        if (strcmp(tipo, "retiro") == 0 && monto >= cfg.limite_retiro) {
            if (strcmp(estado->ultima_operacion, "retiro") == 0)
                estado->retiros_consecutivos++;
            else
                estado->retiros_consecutivos = 1;

            if (estado->retiros_consecutivos >= 2) {
                char alerta[100];
                snprintf(alerta, sizeof(alerta),
                         "ALERTA: Retiros sospechosos consecutivos en cuenta %d\n", cuenta);
                write(pipe_fd, alerta, strlen(alerta));

                FILE *t = fopen(archivo_transacciones, "a");
                fprintf(t,"ALERTA: Retiros sospechosos consecutivos en cuenta %d\n", cuenta);
                fclose(t);
            }
        } else {
            estado->retiros_consecutivos = 0;
        }

        if (strcmp(tipo, "transferencia") == 0 && campos == 4) {
            if (strcmp(estado->ultima_operacion, "transferencia") == 0 &&
                strcmp(estado->ultimo_destino, destino) == 0) {

                char alerta[100];
                snprintf(alerta, sizeof(alerta),
                         "ALERTA: Transferencias repetidas desde %d a %s\n", cuenta, destino);
                write(pipe_fd, alerta, strlen(alerta));

                FILE *t = fopen(archivo_transacciones, "a");
                fprintf(t,"ALERTA: Transferencias repetidas desde %d a %s\n", cuenta, destino);
                fclose(t);
            }
            strcpy(estado->ultimo_destino, destino);
        }

        estado->uso_concurrente++;
        if (estado->uso_concurrente > 1) {
            char alerta[100];
            snprintf(alerta, sizeof(alerta),
                     "ALERTA: Uso simultáneo detectado en cuenta %d\n", cuenta);
            write(pipe_fd, alerta, strlen(alerta));

            FILE *t = fopen(archivo_transacciones, "a");
                fprintf(t,"ALERTA: Uso simultáneo detectado en cuenta %d\n", cuenta);
                fclose(t);
        }

        strcpy(estado->ultima_operacion, tipo);
        estado->uso_concurrente--;

        printf("[MONITOR] %s\n", mensaje.mtext);
    }

    return 0;
}
