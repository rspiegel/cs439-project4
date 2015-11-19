#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "lib/string.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "pagedir.h"

typedef struct {
	struct file *file;
	char *name;
	int open_flag;
	struct semaphore resource;
	tid_t owner;
}file_pointer;

void syscall_init (void);
void halt(void);
void exit(int status);
pid_t exec(const char* cmd_line);
int wait(pid_t pid);
bool create(const char* file, unsigned initial_size);
bool remove(const char* file);
int open(const char* file);
int filesize(int fd);
int read(int fd, void* buffer,unsigned size);
int write(int fd, const void* buffer,unsigned size);
void seek(int fd,unsigned position);
unsigned tell(int fd);
void close(int fd);
bool is_good_ptr(void*);

#endif /* userprog/syscall.h */
