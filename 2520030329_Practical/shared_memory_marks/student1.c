#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define SUBJECTS 5

int main()
{
    const char *name = "/student1_marks";
    size_t size = SUBJECTS * sizeof(int);

    int fd = shm_open(name, O_RDONLY, 0666);

    if (fd == -1)
    {
        perror("shm_open");
        exit(1);
    }

    int *marks = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

    if (marks == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    int total = 0;

    printf("Student 1:\n");
    printf("Marks: ");

    for (int i = 0; i < SUBJECTS; i++)
    {
        printf("%d ", marks[i]);
        total += marks[i];
    }

    double average = (double)total / SUBJECTS;

    printf("\nTotal = %d\n", total);
    printf("Average = %.2f\n", average);

    munmap(marks, size);
    close(fd);

    return 0;
}
