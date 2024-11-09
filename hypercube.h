#ifndef HYPERCUBE_H
#define HYPERCUBE_H

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>

char *intToBinary(int num, int n);

void createPipes(int n);

void createProcesses(int dimension);

int chooseRandomNeighbour( int childId, int n);

void childProcessLogic(int myId, int n);

int setReadfds(int n, fd_set *readfds);

void passToken(int id, int *connectedPipes, int n);

void waitChild();

void handler(int signum);

void freeMemory();

#endif //HYPERCUBE_H