#include "hypercube.h"

int main(int argc, char *argv[]) 
{
    if (argc != 2) {
        printf("Usage: %s <n>\n", argv[0]);
        return 1;
    }

    printf("process PID : %d\n", getpid());

    int n = atoi(argv[1]);

    createPipes(n);

    createProcesses(n);

    exit(0);

}