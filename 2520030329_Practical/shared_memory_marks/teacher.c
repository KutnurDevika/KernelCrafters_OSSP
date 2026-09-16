#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define SUBJECTS 5

int main()
{
    int marks1[SUBJECTS], marks2[SUBJECTS];
    int fd1, fd2;
    int *shm1, *shm2;

    const char *name1 = "/student1_marks";
    const char *name2 = "/student2_marks";

    size_t size = SUBJECTS * sizeof(int);

    /* Remove old shared memory objects if they exist */
    shm_unlink(name1);
    shm_unlink(name2);

    /* Create shared memory for Student 1 */
    fd1 = shm_open(name1, O_CREAT | O_RDWR, 0666);

    /* Create shared memory for Student 2 */
    fd2 = shm_open(name2, O_CREAT | O_RDWR, 0666);

    if (fd1 == -1 || fd2 == -1)
    {
        perror("shm_open");
        exit(1);
    }

    /* Set size of shared memory */
    ftruncate(fd1, size);
    ftruncate(fd2, size);

    /* Map shared memory into teacher's address space */
    shm1 = mmap(NULL, size, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd1, 0);

    shm2 = mmap(NULL, size, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd2, 0);

    if (shm1 == MAP_FAILED || shm2 == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    /* Enter Student 1 marks */
    printf("Enter marks for Student 1 (5 subjects):\n");

    for (int i = 0; i < SUBJECTS; i++)
    {
        printf("Subject %d: ", i + 1);
        scanf("%d", &marks1[i]);
        shm1[i] = marks1[i];
    }

    /* Enter Student 2 marks */
    printf("\nEnter marks for Student 2 (5 subjects):\n");

    for (int i = 0; i < SUBJECTS; i++)
    {
        printf("Subject %d: ", i + 1);
        scanf("%d", &marks2[i]);
        shm2[i] = marks2[i];
    }

    printf("\nTeacher: Marks written to shared memory.\n");
    printf("Teacher: Allowing students to read their marks...\n\n");

    /* Start Student 1 */
    pid_t student1 = fork();

    if (student1 == 0)
    {
        execl("./student1", "./student1", NULL);
        perror("execl student1");
        exit(1);
    }

    /* Start Student 2 */
    pid_t student2 = fork();

    if (student2 == 0)
    {
        execl("./student2", "./student2", NULL);
        perror("execl student2");
        exit(1);
    }

    /* Wait for both students */
    waitpid(student1, NULL, 0);
    waitpid(student2, NULL, 0);

    printf("\nTeacher: Both students completed their calculations.\n");
    printf("Teacher: Cleaning up shared memory...\n");

    /* Cleanup */
    munmap(shm1, size);
    munmap(shm2, size);

    close(fd1);
    close(fd2);

    shm_unlink(name1);
    shm_unlink(name2);

    printf("Teacher: Shared memory removed successfully.\n");

    return 0;
}
