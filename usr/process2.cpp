extern "C" {
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
}

class Test {
    int test;
};

int main(void) 
{


    while (1)
    {
        sleep(300);

        int fd = open("test.txt");
        printf("fd: %d\n", (int64_t)fd);

        if (fd == -1) {
            printf("Cannot open file\n");
            continue;
        }
        char buf[100];

        int byte = read(fd, buf, 100);
        printf("data file is: %s byte: %d\n", buf, byte);

        close(fd);

        printf("closed\n");
    }

    Test t;
    return 0;
}