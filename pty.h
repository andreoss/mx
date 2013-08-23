#ifndef PTY_H
#define PTY_H
#include <stddef.h>
#include <sys/types.h>


typedef struct Pty Pty;


Pty *pty_new(const char *shell, char *const argv[]);


void pty_set_term(const char *t);


void pty_free(Pty * p);


void pty_close(Pty * p);


int pty_fd(const Pty * p);


pid_t pty_pid(const Pty * p);


int pty_read(Pty * p, char *buf, size_t cap);


void pty_write(Pty * p, const char *buf, size_t len);


void pty_resize(Pty * p, int cols, int rows);


void pty_signal(Pty * p, int sig);
#endif
