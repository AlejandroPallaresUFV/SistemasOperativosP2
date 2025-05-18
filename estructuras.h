#ifndef ESTRUCTURAS_H
#define ESTRUCTURAS_H

// Definimos constantes de uso general en el sistema
#define ARCHIVO_LOG "transacciones.log"
#define CONFIG_PATH "config.txt"
#define PIPE_ALERTA "/tmp/pipe_alerta"

// Estructura para los mensajes enviados por cola de mensajes
struct msgbuf {
    long mtype;
    char mtext[256];
};

// Estructura para encapsular datos de la operación bancaria
typedef struct {
    int numero_cuenta;
    int cuenta_destino;
    float monto;
    int tipo_operacion;
    int write_fd;
} DatosOperacion;

// Estructura principal que representa una cuenta bancaria
typedef struct {
    int numero_cuenta;
    char titular[50];
    float saldo;
    int num_transacciones;
    int bloqueado;  // 1 si bloqueada, 0 si activa
} Cuenta;

// Estructura de configuración para el sistema
typedef struct {
    int limite_retiro;
    int limite_transferencia;
    int umbral_retiros;
    int umbral_transferencias;
    int num_hilos;
    char archivo_cuentas[100];
    char archivo_log[100];
} Config;

// Tabla global de cuentas para memoria compartida
typedef struct {
    Cuenta cuentas[100];
    int num_cuentas;
} TablaCuentas;

// Buffer circular de E/S (local por proceso, no compartido)
#define TAM_BUFFER 10

typedef struct {
    Cuenta operaciones[TAM_BUFFER];
    int inicio;
    int fin;
} BufferEstructurado;

#endif
