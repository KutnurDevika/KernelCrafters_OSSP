#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main()
{
    int fd;

    // Path and name of the FIFO (named pipe)
    char *fifo_path = "/tmp/my_named_pipe";

    // Create the FIFO with read/write permissions
    // 0666 gives read and write permission
    mkfifo(fifo_path, 0666);

    // Message to be sent through the FIFO
    char message[] = "Hello from the independent writer process!";

    // Display message while waiting for the reader
    printf("Writer: Waiting for a reader to open the pipe...\n");

    // Open the FIFO for write-only
    // This waits until a reader opens the FIFO
    fd = open(fifo_path, O_WRONLY);

    // Reader has connected
    printf("Writer: Reader connected! Sending message...\n");

    // Write the message into the FIFO
    write(fd, message, strlen(message) + 1);

    // Close the FIFO
    close(fd);

    printf("Writer: Message sent and pipe closed.\n");

    return 0;
}
