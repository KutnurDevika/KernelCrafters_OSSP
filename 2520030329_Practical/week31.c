#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main()
{
    pid_t pid;

    printf("Start of Parent\n");

    pid = fork();   // Create child process

    if (pid == 0)
    {
        // Child process
        printf("Start of Child\n");

        sleep(1);   // Child process waits for 1 second

        printf("End of Child\n");

        exit(0);
    }
    else
    {
        // Parent process
        wait(NULL); // Parent waits for child process

        printf("End of Parent\n");
    }

    return 0;
}
