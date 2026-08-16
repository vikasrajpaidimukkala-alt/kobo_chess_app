#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    const char *path =
        "/mnt/onboard/.adds/kobochess/diagnostics.txt";

    FILE *f = fopen(path, "w");

    if (f == NULL) {
        return 1;
    }

    fprintf(f, "Kobo Chess native test\n");
    fprintf(f, "PID: %d\n", getpid());
    fprintf(f, "Hello from ARM C!\n");

    fclose(f);

    return 0;
}
