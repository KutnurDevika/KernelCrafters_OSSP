#include <stdio.h>

int main()
{
    int n;
    int a[10];

    printf("Enter the size of array\n");
    scanf("%d", &n);

    for (int i = 0; i < n; i++)
    {
        printf("Enter %d element\n", i + 1);
        scanf("%d", &a[i]);
    }

    for (int i = 0; i < n; i++)
    {
        printf("%d element is %d\n", i + 1, a[i]);
    }

    return 0;
}
