#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "estructuras.h"
#include <errno.h>
#include <sys/stat.h> 

#define SHM_KEY 1234

int main() {
    // Crear el segmento de memoria compartida
    int shm_id = shmget(SHM_KEY, sizeof(TablaCuentas), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Error creando memoria compartida");
        exit(EXIT_FAILURE);
    }

    // Asociar el segmento al espacio de direcciones del proceso
    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);
    if (tabla == (void *) -1) {
        perror("Error mapeando memoria compartida");
        exit(EXIT_FAILURE);
    }

    // Definir las cuentas iniciales
    Cuenta cuentas[] = {
        {1001, "Blas fuma Crack", 5000.00, 0},
        {1002, "Kike oso maduro", 3000.00, 0},
        {1003, "Alice Johnson", 4500.50, 0},
        {1004, "Bob Brown", 6000.00, 0}
    };

    size_t num_cuentas = sizeof(cuentas) / sizeof(Cuenta);
    tabla->num_cuentas = num_cuentas;

    for (size_t i = 0; i < num_cuentas; i++) {
        tabla->cuentas[i] = cuentas[i];
    }

    // Guardar las cuentas iniciales en el archivo cuentas.dat
    FILE *archivo = fopen("cuentas.dat", "wb");
    if (!archivo) {
        perror("Error creando cuentas.dat");
        exit(EXIT_FAILURE);
    }
    fwrite(cuentas, sizeof(Cuenta), num_cuentas, archivo);
    fclose(archivo);
    printf("Archivo cuentas.dat creado con cuentas iniciales.\n");

    printf("Memoria compartida inicializada con %zu cuentas.\n", num_cuentas);

    // Desvincular memoria compartida
    if (shmdt((void *)tabla) == -1) {
        perror("Error al desvincular la memoria compartida");
        exit(EXIT_FAILURE);
    }

    return 0;
}
