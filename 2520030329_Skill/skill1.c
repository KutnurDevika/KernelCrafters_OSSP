#include <stdio.h>      // Provides printf(), fgets(), fflush()
#include <string.h>     // Provides strcmp() and strcspn()

int main() {
    char command[100];  // Array to store the command entered by the user

    // Infinite loop to continuously accept commands
    while (1) {

        // Display the shell prompt
        printf("myShell> ");

        // Make sure the prompt is displayed immediately
        fflush(stdout);

        // Read the user's input and store it in command
        fgets(command, sizeof(command), stdin);

        // Remove the newline character '\n' added by fgets()
        command[strcspn(command, "\n")] = '\0';

        // Check if the user entered "exit"
        if (strcmp(command, "exit") == 0) {

            // Display exit message
            printf("Exiting shell...\n");

            // Stop the while loop
            break;
        }

        // Display the command entered by the user
        printf("You entered: %s\n", command);
    }

    // End the program successfully
    return 0;
}
