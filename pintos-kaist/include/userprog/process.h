#include "filesys/off_t.h"

#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

typedef int tid_t;

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

/* aux 구조체 선언 */
struct aux {
    struct file *file;
    off_t ofs;
    size_t page_read_bytes;
    size_t page_zero_bytes;
    bool writable;    
};

#endif /* userprog/process.h */
