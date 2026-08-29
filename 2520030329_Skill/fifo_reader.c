#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
    int fd;
    char buffer[100];

    // Path and name of the FIFO (named pipe)
    char *fifo_path = "/tmp/my_named_pipe";

    // Open the FIFO for read-only
    // This waits until the writer opens the FIFO
    fd = open(fifo_path, O_RDONLY);

    // Read the message from the FIFO
    read(fd, buffer, sizeof(buffer));

    // Display the received message
    printf("Reader: Received message: %s\n", buffer);

    // Close the FIFO
    close(fd);

    return 0;
}
