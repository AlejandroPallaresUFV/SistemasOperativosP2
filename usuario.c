#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <time.h>
#include "estructuras.h"

#define SEMAFORO_LOG "/log_sem"
#define SHM_KEY 1234
#define NOMBRE_SEM "/cuentas_sem"

sem_t *semaforo;
sem_t *sem_log;
int cuenta_actual;

// Registrar log local
void registrar_log(const char *tipo_op, float monto, int destino) {
    char dir_path[100], log_path[150];
    snprintf(dir_path, sizeof(dir_path), "./transacciones/%d", cuenta_actual);
    snprintf(log_path, sizeof(log_path), "%s/transacciones.log", dir_path);

    mkdir("./transacciones", 0777);
    mkdir(dir_path, 0777);

    sem_wait(sem_log);
    FILE *log = fopen(log_path, "a");
    if (log) {
        time_t t = time(NULL);
        struct tm *tm_info = localtime(&t);
        char timestamp[30];
        strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S]", tm_info);

        if (strcmp(tipo_op, "Transferencia") == 0)
            fprintf(log, "%s %s: %.2f -> %d\n", timestamp, tipo_op, monto, destino);
        else
            fprintf(log, "%s %s: %.2f\n", timestamp, tipo_op, monto);

        fclose(log);
    }
    sem_post(sem_log);
}

// Enviar operación al banco por cola de mensajes
void enviar_operacion(DatosOperacion *op) {
    key_t key = ftok("banco.c", 66);  // Mismo ftok que en banco.c
    int cola_id = msgget(key, 0666 | IPC_CREAT);
    if (cola_id == -1) {
        perror("Error accediendo a la cola de mensajes");
        return;
    }

    struct {
        long tipo;
        DatosOperacion operacion;
    } mensaje;

    mensaje.tipo = 1;
    mensaje.operacion = *op;

    if (msgsnd(cola_id, &mensaje, sizeof(DatosOperacion), 0) == -1) {
        perror("Error enviando mensaje al banco");
    } else {
        printf("📤 Operación enviada al banco correctamente.\n");
    }
}

void consultar_saldo() {
    sem_wait(semaforo);
    int shm_id = shmget(SHM_KEY, sizeof(TablaCuentas), 0666);
    if (shm_id == -1) {
        perror("Error accediendo a memoria compartida");
        sem_post(semaforo);
        return;
    }
    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);
    if (tabla == (void *)-1) {
        perror("Error mapeando memoria compartida");
        sem_post(semaforo);
        return;
    }

    for (int i = 0; i < tabla->num_cuentas; i++) {
        if (tabla->cuentas[i].numero_cuenta == cuenta_actual) {
            printf("Titular: %s | Saldo actual: %.2f\n", tabla->cuentas[i].titular, tabla->cuentas[i].saldo);
            break;
        }
    }

    shmdt(tabla);
    sem_post(semaforo);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s <numero_cuenta>\n", argv[0]);
        return 1;
    }

    cuenta_actual = atoi(argv[1]);
    printf("🧾 Accediendo a la cuenta: %d\n", cuenta_actual);

    semaforo = sem_open(NOMBRE_SEM, 0);
    if (semaforo == SEM_FAILED) {
        perror("Error al abrir semáforo");
        return 1;
    }

    sem_log = sem_open(SEMAFORO_LOG, O_CREAT, 0644, 1);
    if (sem_log == SEM_FAILED) {
        perror("Error al abrir semáforo de log");
        return 1;
    }

    int opcion;
    do {
        printf("\n--- Menú Usuario [%d] ---\n", cuenta_actual);
        printf("1. Depósito\n2. Retiro\n3. Transferencia\n4. Consultar saldo\n5. Salir\nOpción: ");
        scanf("%d", &opcion);

        if (opcion >= 1 && opcion <= 3) {
            DatosOperacion op;
            op.numero_cuenta = cuenta_actual;
            op.write_fd = STDOUT_FILENO;
            op.tipo_operacion = opcion;

            printf("Monto: ");
            scanf("%f", &op.monto);

            if (opcion == 3) {
                printf("Cuenta destino: ");
                scanf("%d", &op.cuenta_destino);
            } else {
                op.cuenta_destino = -1;
            }

            enviar_operacion(&op);

            // Log local para auditoría
            registrar_log(op.tipo_operacion == 1 ? "Depósito" :
                          op.tipo_operacion == 2 ? "Retiro" : "Transferencia",
                          op.monto,
                          op.cuenta_destino);

        } else if (opcion == 4) {
            consultar_saldo();
        }

    } while (opcion != 5);

    sem_close(semaforo);
    sem_close(sem_log);

    return 0;
}
