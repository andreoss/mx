#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include "config.h"
#include "pty.h"
#include "types.h"

struct Pty {
    int master;
    int slave;
    pid_t child;
    char *wbuf;
    size_t wlen;
    size_t wcap;
};

static const char *term_type = TERM_TYPE;

void pty_set_term(const char *t)
{
    term_type = t;
}


static const char *default_shell(void)
{
    const char *s = getenv("SHELL");
    return s ? s : "/bin/sh";
}

Pty *pty_new(const char *shell, char *const argv[])
{
    Pty *p;
    int mfd = -1, sfd = -1;

    p = calloc(1, sizeof(*p));
    if (!p)
	return NULL;


    mfd = posix_openpt(O_RDWR | O_NOCTTY);
    if (mfd < 0)
	goto fail;

    if (grantpt(mfd) < 0)
	goto fail;
    if (unlockpt(mfd) < 0)
	goto fail;

    sfd = open(ptsname(mfd), O_RDWR | O_NOCTTY);
    if (sfd < 0)
	goto fail;

    p->master = mfd;
    p->slave = sfd;


    p->child = fork();
    if (p->child < 0)
	goto fail;

    if (p->child == 0) {

	close(p->master);

	if (setsid() < 0)
	    _exit(127);
	ioctl(p->slave, TIOCSCTTY, NULL);

	dup2(p->slave, 0);
	dup2(p->slave, 1);
	dup2(p->slave, 2);

	if (p->slave > 2)
	    close(p->slave);


	struct termios tio;
	if (tcgetattr(0, &tio) == 0) {
	    tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
			     | INLCR | IGNCR | ICRNL | IXON);
	    tio.c_oflag |= OPOST | ONLCR;
	    tio.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
	    tio.c_cflag &= ~(CSIZE | PARENB);
	    tio.c_cflag |= CS8;
	    tio.c_cc[VMIN] = 1;
	    tio.c_cc[VTIME] = 0;
	    tcsetattr(0, TCSANOW, &tio);
	}


	setenv("TERM", term_type, 1);
	unsetenv("LINES");
	unsetenv("COLUMNS");
	setenv("WINDOWID", "0", 1);


	if (!shell)
	    shell = default_shell();
	if (argv) {
	    execvp(shell, argv);
	} else {
	    const char *args[] = { shell, NULL };
	    execvp(shell, (char *const *) args);
	}

	_exit(127);
    }


    close(p->slave);
    p->slave = -1;


    int flags = fcntl(p->master, F_GETFL);
    fcntl(p->master, F_SETFL, flags | O_NONBLOCK);

    return p;

  fail:
    if (mfd >= 0)
	close(mfd);
    if (sfd >= 0)
	close(sfd);
    free(p);
    return NULL;
}

void pty_free(Pty *p)
{
    if (!p)
	return;
    close(p->master);
    if (p->child > 0) {
	kill(p->child, SIGHUP);

	int status;
	for (int i = 0; i < 50; i++) {
	    if (waitpid(p->child, &status, WNOHANG) > 0)
		break;
	    usleep(20000);
	}
    }
    free(p->wbuf);
    free(p);
}

void pty_close(Pty *p)
{
    if (!p)
	return;
    if (p->master >= 0) {
	close(p->master);
	p->master = -1;
    }
}

int pty_fd(const Pty *p)
{
    return p->master;
}

pid_t pty_pid(const Pty *p)
{
    return p->child;
}

int pty_read(Pty *p, char *buf, size_t cap)
{
    ssize_t n = read(p->master, buf, cap);
    if (n < 0) {
	if (errno == EAGAIN)
	    return -2;
	if (errno == EINTR)
	    return -1;
	return -1;
    }
    return (int) n;
}

static int pty_queue(Pty *p, const char *buf, size_t len)
{
    if (p->wlen + len > p->wcap) {
	size_t cap = p->wcap ? p->wcap : 1024;
	while (cap < p->wlen + len)
	    cap *= 2;
	char *nb = realloc(p->wbuf, cap);
	if (!nb)
	    return -1;
	p->wbuf = nb;
	p->wcap = cap;
    }
    memcpy(p->wbuf + p->wlen, buf, len);
    p->wlen += len;
    return 0;
}

int pty_pending(const Pty *p)
{
    return p->wlen > 0;
}

void pty_flush(Pty *p)
{
    if (p->master < 0) {
	p->wlen = 0;
	return;
    }
    size_t off = 0;
    while (off < p->wlen) {
	ssize_t n = write(p->master, p->wbuf + off,
			  MIN(p->wlen - off, 255));
	if (n < 0) {
	    if (errno == EINTR)
		continue;
	    break;
	}
	off += n;
    }
    if (off > 0) {
	memmove(p->wbuf, p->wbuf + off, p->wlen - off);
	p->wlen -= off;
    }
}

void pty_write(Pty *p, const char *buf, size_t len)
{
    if (p->master < 0)
	return;

    if (p->wlen > 0) {
	pty_queue(p, buf, len);
	pty_flush(p);
	return;
    }
    while (len > 0) {
	ssize_t n = write(p->master, buf, MIN(len, 255));
	if (n < 0) {
	    if (errno == EAGAIN) {
		pty_queue(p, buf, len);
		return;
	    }
	    if (errno == EINTR)
		continue;
	    break;
	}
	buf += n;
	len -= n;
    }
}

void pty_resize(Pty *p, int cols, int rows)
{
    struct winsize ws_old;
    struct winsize ws = {
	.ws_col = (unsigned short) cols,
	.ws_row = (unsigned short) rows,
    };
    if (p->master < 0) {
	fprintf(stderr, "pty_resize: master fd closed\n");
	return;
    }
    if (ioctl(p->master, TIOCGWINSZ, &ws_old) == 0)
	fprintf(stderr, "pty_resize: %dx%d -> %dx%d\n",
		ws_old.ws_col, ws_old.ws_row, cols, rows);
    if (ioctl(p->master, TIOCSWINSZ, &ws) < 0)
	fprintf(stderr, "pty_resize: TIOCSWINSZ failed: %s\n",
		strerror(errno));
}

void pty_signal(Pty *p, int sig)
{
    if (p->master < 0)
	return;
    pid_t fg = tcgetpgrp(p->master);
    if (fg > 0) {
	fprintf(stderr, "pty_signal: sending SIGWINCH to fg pgid %d\n",
		fg);
	if (kill(-fg, sig) < 0)
	    fprintf(stderr, "pty_signal: kill(-%d) failed: %s\n",
		    fg, strerror(errno));
    } else {
	fprintf(stderr, "pty_signal: tcgetpgrp returned %d, "
		"falling back to child pgid %d\n", fg, p->child);
	if (p->child > 0)
	    kill(-p->child, sig);
    }
}
