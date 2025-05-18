#include <stdio.h>
#include <stdlib.h>
#include "estructuras.h"

int main() {
    FILE *archivo = fopen("cuentas.dat", "rb");
    if (!archivo) {
        perror("No se pudo abrir cuentas.dat");
        return 1;
    }

    Cuenta cuenta;
    printf("Estado actual de cuentas en disco:\n");
    printf("--------------------------------------\n");

    while (fread(&cuenta, sizeof(Cuenta), 1, archivo) == 1) {
        printf("Cuenta: %d\nTitular: %s\nSaldo: %.2f\nBloqueado: %s\n\n",
               cuenta.numero_cuenta,
               cuenta.titular,
               cuenta.saldo,
               cuenta.bloqueado ? "Sí" : "No");
    }

    fclose(archivo);
    return 0;
}
