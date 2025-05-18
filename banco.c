#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <string.h>
#include <pthread.h>
#include "estructuras.h"

#define NOMBRE_SEM "/cuentas_sem"
#define MAX_USUARIOS 1
#define SHM_KEY 1234

// Buffer y sincronización locales al banco
BufferEstructurado buffer = {.inicio = 0, .fin = 0};
pthread_mutex_t mutex_buffer = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_buffer = PTHREAD_COND_INITIALIZER;

// Insertar cuenta modificada en el buffer circular
void insertar_en_buffer(Cuenta cuenta_modificada) {
    pthread_mutex_lock(&mutex_buffer);
    buffer.operaciones[buffer.fin] = cuenta_modificada;
    buffer.fin = (buffer.fin + 1) % TAM_BUFFER;
    pthread_cond_signal(&cond_buffer);
    pthread_mutex_unlock(&mutex_buffer);
}

// Hilo de escritura diferida en disco desde el buffer
void *gestionar_entrada_salida(void *arg) {
    while (1) {
        pthread_mutex_lock(&mutex_buffer);
        while (buffer.inicio == buffer.fin) {
            pthread_cond_wait(&cond_buffer, &mutex_buffer);
        }

        Cuenta op = buffer.operaciones[buffer.inicio];
        buffer.inicio = (buffer.inicio + 1) % TAM_BUFFER;
        pthread_mutex_unlock(&mutex_buffer);

        FILE *archivo = fopen("cuentas.dat", "rb+");
        if (archivo) {
            int pos = 0;
            Cuenta aux;
            while (fread(&aux, sizeof(Cuenta), 1, archivo)) {
                if (aux.numero_cuenta == op.numero_cuenta) {
                    fseek(archivo, pos * sizeof(Cuenta), SEEK_SET);
                    fwrite(&op, sizeof(Cuenta), 1, archivo);
                    printf("📝 Banco: Cuenta %d sincronizada. Saldo: %.2f\n", op.numero_cuenta, op.saldo);
                    break;
                }
                pos++;
            }
            fclose(archivo);
        }
    }
    return NULL;
}

// Hilo que escucha operaciones por cola de mensajes y las aplica
void *procesar_operaciones(void *arg) {
    key_t key = ftok("banco.c", 66);
    int cola_id = msgget(key, 0666 | IPC_CREAT);
    if (cola_id == -1) {
        perror("Error creando cola de mensajes");
        pthread_exit(NULL);
    }

    int shm_id = shmget(SHM_KEY, sizeof(TablaCuentas), 0666);
    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);

    struct {
        long tipo;
        DatosOperacion operacion;
    } mensaje;

    while (1) {
        if (msgrcv(cola_id, &mensaje, sizeof(DatosOperacion), 0, 0) != -1) {
            DatosOperacion op = mensaje.operacion;

            sem_t *sem = sem_open(NOMBRE_SEM, 0);
            sem_wait(sem);

            for (int i = 0; i < tabla->num_cuentas; i++) {
                if (tabla->cuentas[i].numero_cuenta == op.numero_cuenta) {
                    Cuenta *cuenta = &tabla->cuentas[i];

                    if (op.tipo_operacion == 1) {
                        cuenta->saldo += op.monto;
                        insertar_en_buffer(*cuenta);
                    } else if (op.tipo_operacion == 2 && cuenta->saldo >= op.monto) {
                        cuenta->saldo -= op.monto;
                        insertar_en_buffer(*cuenta);
                    } else if (op.tipo_operacion == 3) {
                        for (int j = 0; j < tabla->num_cuentas; j++) {
                            if (tabla->cuentas[j].numero_cuenta == op.cuenta_destino) {
                                Cuenta *destino = &tabla->cuentas[j];
                                if (cuenta->saldo >= op.monto) {
                                    cuenta->saldo -= op.monto;
                                    destino->saldo += op.monto;
                                    insertar_en_buffer(*cuenta);
                                    insertar_en_buffer(*destino);
                                }
                                break;
                            }
                        }
                    }

                    break;
                }
            }

            sem_post(sem);

            // Reenviar a monitor para detectar anomalías
            key_t kmon = ftok("monitor.c", 65);
            int cola_mon = msgget(kmon, 0666);
            if (cola_mon != -1) {
                struct msgbuf mensaje_mon;
                mensaje_mon.mtype = 1;
                snprintf(mensaje_mon.mtext, sizeof(mensaje_mon.mtext), "%d,%s,%.2f,%d",
                         op.numero_cuenta,
                         op.tipo_operacion == 1 ? "deposito" :
                         op.tipo_operacion == 2 ? "retiro" : "transferencia",
                         op.monto,
                         op.cuenta_destino);
                msgsnd(cola_mon, &mensaje_mon, sizeof(mensaje_mon.mtext), 0);
            }
        }
    }
    return NULL;
}

// Función para crear la memoria compartida
int crear_memoria_compartida() {
    int shm_id = shmget(SHM_KEY, sizeof(TablaCuentas), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Error creando memoria compartida");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

// Vincular la memoria compartida
TablaCuentas* obtener_tabla_cuentas(int shm_id) {
    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);
    if (tabla == (void *) -1) {
        perror("Error mapeando memoria compartida");
        exit(EXIT_FAILURE);
    }
    return tabla;
}

int main() {
    int shm_id = crear_memoria_compartida();
    TablaCuentas *tabla = obtener_tabla_cuentas(shm_id);

    // Lanzar hilo de E/S diferida
    pthread_t hilo_es;
    pthread_create(&hilo_es, NULL, gestionar_entrada_salida, NULL);

    // Lanzar hilo que procesa operaciones
    pthread_t hilo_ops;
    pthread_create(&hilo_ops, NULL, procesar_operaciones, NULL);

    // Semáforo global
    sem_t *sem = sem_open(NOMBRE_SEM, O_CREAT, 0644, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        exit(1);
    }

    // Comunicación con monitor
    int pipe_monitor[2];
    if (pipe(pipe_monitor) == -1) {
        perror("pipe monitor");
        exit(1);
    }

    pid_t pid_monitor = fork();
    if (pid_monitor == 0) {
        close(pipe_monitor[0]);
        char fd_monitor_write[10];
        snprintf(fd_monitor_write, sizeof(fd_monitor_write), "%d", pipe_monitor[1]);
        execl("./monitor", "./monitor", fd_monitor_write, NULL);
        perror("Error al lanzar monitor");
        exit(1);
    }

    // Selección de cuenta
    int cuenta_seleccionada;
    printf("Selecciona la cuenta que deseas abrir: ");
    scanf("%d", &cuenta_seleccionada);

    if (cuenta_seleccionada < 1001 || cuenta_seleccionada > 1004) {
        printf("Cuenta no válida.\n");
        exit(1);
    }

    // Lanzar usuario
    for (int i = 0; i < MAX_USUARIOS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char id_str[10];
            snprintf(id_str, sizeof(id_str), "%d", cuenta_seleccionada);
            execlp("xterm", "xterm", "-e", "./usuario", id_str, NULL);
            perror("Error lanzando xterm con usuario");
            exit(1);
        }
    }

    // Leer alertas del monitor
    close(pipe_monitor[1]);
    char alerta[256];
    int leidos;

    while ((leidos = read(pipe_monitor[0], alerta, sizeof(alerta) - 1)) > 0) {
        alerta[leidos] = '\0';
        printf("Alerta de MONITOR: %s", alerta);
    }

    close(pipe_monitor[0]);

    for (int i = 0; i < MAX_USUARIOS + 1; i++) {
        wait(NULL);
    }

    sem_unlink(NOMBRE_SEM);
    return 0;
}
