#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main()
{
    int num;
    pid_t pid;
    long long factorial = 1;

    printf("Enter a number: ");
    scanf("%d", &num);

    pid = fork();

    if (pid < 0)
    {
        printf("Fork failed!\n");
        return 1;
    }
    else if (pid == 0)
    {
        // Child Process - Calculate Factorial
        printf("\n--- Child Process ---\n");
        printf("Child PID: %d\n", getpid());
        printf("Parent PID: %d\n", getppid());

        for (int i = 1; i <= num; i++)
            factorial *= i;

        printf("Factorial of %d = %lld\n", num, factorial);
    }
    else
    {
        // Parent Process - Calculate Square
        wait(NULL);

        printf("\n--- Parent Process ---\n");
        printf("Parent PID: %d\n", getpid());

        printf("Square of %d = %d\n", num, num * num);
    }

    return 0;
}
