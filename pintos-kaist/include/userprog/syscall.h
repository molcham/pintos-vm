#include <stdbool.h>

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
int open(const char *path);
int write(int fd, const void *buffer, unsigned size);
bool validate_addr(void *addr);

#endif /* userprog/syscall.h */
