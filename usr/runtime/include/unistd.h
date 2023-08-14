#pragma once
#include <stdint.h>
#include <stddef.h>

int write(int fd, const char *buf, size_t count);
int read(int fd, char *buf, size_t count);
unsigned int sleep(unsigned int seconds);
void exit(void);
int wait(void);
int mem(void);