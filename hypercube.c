#include "hypercube.h"
#include <sys/stat.h>

int nbProcesses = 0;
int nbPipes = 0;
pid_t *childs;
int **pipes;
int *connectedPipes;

volatile sig_atomic_t n_sigusr1 = 1;


/**
 * Convert an integer into a binary string
 * 
 * param num The integer to convert
 * param n The number of bits in the integer
 * return char* The binary string
 */
char *intToBinary(int num, int n) {
    char *binary = (char *)malloc((n + 1) * sizeof(char));
    binary[n] = '\0';

    for (int i = n - 1; i >= 0; i--) {
        binary[i] = (num & 1) ? '1' : '0';
        num >>= 1;
    }

    return binary;
}


/**
 * Creates a specified number of pipes for inter-process communication.
 * 
 * This function calculates the total number of pipes needed based on the
 * dimension of the hypercube (n) and allocates memory for storing pipe
 * file descriptors. Each pipe is created using the pipe() system call.
 * If pipe creation fails, the function prints an error message and exits.
 * 
 * n The dimension of the hypercube, which determines the number of
 *          processes and pipes. The total number of pipes created is n * 2^n.
 */
void createPipes(int n)
{
    nbPipes = (1<<n) * n; // Calculate the total number of pipes needed
    pipes = (int **)malloc(nbPipes * sizeof(int *)); // Allocate memory for pipe file descriptors

    for (int i = 0; i < nbPipes; i++)
    {
        pipes[i] = (int *)malloc(2 * sizeof(int)); // Allocate memory for each pipe's read and write file descriptors

        if(pipe(pipes[i]) == -1) // Attempt to create a pipe and check for failure
        {
            perror("pipe"); // Print an error message if pipe creation fails
            exit(EXIT_FAILURE); // Exit the program on failure
        }
     
    }
}



/**
 * Creates a specified number of processes for a hypercube topology and establishes pipe connections between them.
 * 
 * This function first calculates the number of processes to be created based on the dimension of the hypercube (n).
 * It then allocates memory for storing the process IDs (PIDs) of the child processes. For each process, it forks
 * the current process. The child processes calculate their neighbors in the hypercube topology and establish
 * pipe connections with them. Each child process closes the ends of the pipes that it does not use and then
 * proceeds to execute a token passing algorithm. The parent process closes all ends of the pipes after forking
 * all child processes.
 * 
 * n The dimension of the hypercube. The total number of processes created is 2^n.
 */
void createProcesses(int n)
{
    nbProcesses = 1<<n; // Calculate the number of processes based on the dimension of the hypercube
    printf("nb of processes : %d\n", nbProcesses);
    childs = (pid_t *)malloc(nbProcesses*sizeof(pid_t)); // Allocate memory for storing child PIDs

    for (int i = 0; i < nbProcesses; i++)
    {
        pid_t pid = fork(); // Fork the current process

        if (pid == -1) // Check for fork error
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0) // Child process
        {
            connectedPipes = (int *)malloc(n * 2 * sizeof(int)); // Allocate memory for storing connected pipe file descriptors

            // Establish pipe connections with neighbors in the hypercube topology
            for (int j = 0; j < n ; j++)
            {
                int neighbour = i ^ (1 << j); // Calculate neighbor's ID

                // Store file descriptors for pipes connected to the neighbor
                connectedPipes[2*j] = pipes[i * n + j][0];
                connectedPipes[2*j + 1] = pipes[neighbour * n + j][1];

                // Close the ends of the pipes that are not used by this process
                close(pipes[i*n + j][1]);
                close(pipes[neighbour * n +j][0]);
            }

            // Close all other pipes that are not connected to this process
            for (int j = 0; j < nbPipes; j++)
            {
                int needClose = 1;

                for(int k = 0; k < n * 2; k++)
                {
                    if (pipes[j][0] == connectedPipes[k] || pipes[j][1] == connectedPipes[k])
                    {
                        needClose = 0;
                        break;
                    }
                }

                if(needClose)
                {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
            }
            
            passToken(i, connectedPipes, n); // Execute the token passing algorithm

            // Close all connected pipes before exiting
            for(int j = 0; j < n * 2; j++)
            {
                close(connectedPipes[j]);
            }

            //free conectedPipes memory
            if (connectedPipes != NULL) {
                free(connectedPipes);
                connectedPipes = NULL;
            }

            exit (0); // Exit the child process
        }
        else // Parent process
        {
            childs[i] = pid; // Store the PID of the child process
        }
    }

    signal(SIGUSR1, handler);

    // Close all ends of the pipes in the parent process
    for (int i = 0; i < nbPipes; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Wait for all child processes to terminate
    waitChild();

    // Now that all child processes have finished, it's safe to free allocated memory
    freeMemory();
}


/**
 * Passes a token around the processes in a hypercube topology, simulating a token ring network.
 * This function simulates the passing of a token from one process to another in a hypercube topology.
 * It starts with process 0, increments the token, and passes it to a randomly selected neighbor.
 * Each process writes the token value and the time between receptions to a file named after its binary ID.
 * The process continues until the select call fails, indicating no more tokens are being passed.
 * 
 *  id The ID of the current process.
 *  connectedPipes An array of file descriptors for the pipes connected to this process.
 *  n The dimension of the hypercube, determining the number of neighbors each process has.
 */
void passToken(int id, int *connectedPipes, int n) {
    fd_set readfds; // Set of file descriptors to monitor for readability
    int pipe_index; // Index of the pipe to use for sending the token
    struct timeval stop, start = {0}; // Variables for tracking the time between token receptions

    int token = 0; // The token to be passed around

    // Convert n to a string for the directory name
    char dirName[128]; // Assuming n will not exceed the length that can be represented in 128 characters
    sprintf(dirName, "%d", n);

    // Create the directory with read, write and execute permissions for the owner
    // and with read and execute permissions for the group and others.
    mkdir(dirName, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

    // Use the directory name in the filename
    char *binaryString = intToBinary(id, n);
    char *filename = malloc(snprintf(NULL, 0, "%s/%s", dirName, binaryString) + 1);
    sprintf(filename, "%s/%s.txt", dirName, binaryString);

    FILE *file = fopen(filename, "w");
    if(file == NULL)
    {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    srand(time(NULL)); // Seed the random number generator
    
    if (id == 0) { // If this is the initial process
        gettimeofday(&start, NULL); // Record the current time
        token++; // Increment the token
        pipe_index = rand() % n; // Select a random neighbor
        fprintf(file, "token: %d\n", token); // Write the starting token to the file
        fflush(file);
        printf("starting token : %d", token);

        if (write(connectedPipes[2*pipe_index+1], &token, sizeof(token)) == -1) { // Send the token to the selected neighbor
            perror("Write to pipe failed");
            exit(EXIT_FAILURE);
        }
    }

    long microSec = 0; // Variable for calculating milliseconds
      
    int nfds = setReadfds(n, &readfds); // Set the file descriptors to monitor
      
    while(select(nfds+1, &readfds, NULL, NULL, NULL) > 0) { // Wait for a token to be received

      for(int i = 0; i < n; i++) // Check all connected pipes
      {
        if(FD_ISSET(connectedPipes[2*i], &readfds)) // If a token is received
        {
          if(read(connectedPipes[2*i], &token, sizeof(token)) != sizeof(token)) // Read the token
          {
            perror("pipe read fail");
            exit(EXIT_FAILURE);
          }
        }
      }
      token++; // Increment the token
      
      if(start.tv_sec == 0) // If this is the first token reception
      {
        gettimeofday(&start, NULL); // Record the current time
        fprintf(file, "first received token: %d\n", token); // Write the token to the file
        fflush(file);
        printf("first received token : %d", token);
      }
      else { // For subsequent receptions
        gettimeofday(&stop, NULL); // Record the current time
        microSec = (stop.tv_sec - start.tv_sec)*1000000L + (stop.tv_usec - start.tv_usec); // Calculate the time difference
        fprintf(file, "Token: %d, Time : %ld\n", token, microSec); // Write the token and time difference to the file
        fflush(file);
        printf("Token: %d, Time : %ld\n", token, microSec);
        start = stop; // Update timeBefore for the next iteration
      }

      pipe_index = rand() % n; // Select a random neighbor
      if (write(connectedPipes[2*pipe_index+1], &token, sizeof(int)) == -1) { // Send the token to the selected neighbor
        perror("write failed");
        exit(EXIT_FAILURE);
      }
      microSec = 0; // Reset the millisecond counter
      
      nfds = setReadfds(n, &readfds); // Reset the file descriptors to monitor
        
    }

    fclose(file); // Close the file when done
}


/**
 * Prepares a set of file descriptors for reading and determines the highest file descriptor value.
 * 
 * This function initializes the file descriptor set `readfds` for use with the `select` function.
 * It adds the read ends of the pipes connected to the current process to the set. The function
 * also calculates the highest file descriptor number among the added descriptors, which is required
 * as an argument to the `select` function.
 * 
 * n The number of pipes connected to the current process. This is typically equal to the
 *          dimension of the hypercube, as each process is connected to `n` other processes.
 * readfds A pointer to an fd_set structure that will be filled with the file descriptors
 *                of the pipes that are to be monitored for readability.
 * 
 * return The highest file descriptor number among the added descriptors. This value is used as
 *         an argument to the `select` function to specify the range of file descriptors to be monitored.
 */
int setReadfds(int n, fd_set *readfds) {
  int nfds = 0;
  FD_ZERO(readfds);

  for(int i = 0; i < n; i++)
  {
    FD_SET(connectedPipes[2*i], readfds);
    if (connectedPipes[2*i] > nfds)
    {
        nfds = connectedPipes[2*i];
    }
  }
  return nfds;
}


/**
 * Waits for all child processes to terminate.
 * 
 * This function iterates through the list of child process IDs stored in `childs` and waits for each
 * child process to terminate using the `waitpid` system call. It ensures that the parent process waits
 * for all its child processes to complete their execution before proceeding. This is particularly useful
 * in a concurrent programming context where the parent process needs to perform cleanup or further processing
 * only after all child processes have finished.
 * 
 * param n The number of child processes to wait for. This parameter is currently unused in the function.
 */
void waitChild() {
  for (int i = 0; i < nbProcesses; i++) {
    int state;
    waitpid(childs[i], &state, 0);
  }
}

void handler(int signum) 
{
    printf("Caught signal %d\n", signum);

    if (signum == SIGUSR1)
    {
        if (n_sigusr1)
        {
            for (int i = 0; i < nbProcesses; i++)
            {
                kill(childs[i], SIGSTOP);
            }
        }
        else
        {
            for (int i = 0; i < nbProcesses; i++)
            {
                kill(childs[i], SIGCONT);
            }
        }
        n_sigusr1 = !n_sigusr1;

    }
    else if (signum == SIGINT)
    {
        for (int i = 0; i < nbProcesses; i++)
        {
            kill(childs[i], SIGINT);
        }
    }
}

void freeMemory()
{

    // Free the memory allocated for each pipe in the pipes array
    if (pipes != NULL) {
        for (int i = 0; i < nbPipes; i++) {
            if (pipes[i] != NULL) {
                free(pipes[i]);
            }
        }
        // Free the array of pointers to pipes
        free(pipes);
        pipes = NULL;
    }

    // Free the memory allocated for the childs array
    if (childs != NULL) {
        free(childs);
        childs = NULL;
    }
}