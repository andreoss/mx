#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "proc.h"

#define MAX_CHAIN 8

static int read_stat(pid_t pid, pid_t *ppid, pid_t *pgrp, pid_t *sid)
{
    char path[64];
    char buf[1024];
    long pp, pg, se;
    FILE *f;
    size_t n;
    char *rp;

    snprintf(path, sizeof path, "/proc/%d/stat", (int) pid);
    f = fopen(path, "r");
    if (!f)
	return -1;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    if (n == 0)
	return -1;
    buf[n] = 0;
    rp = strrchr(buf, ')');
    if (!rp)
	return -1;
    rp++;
    if (sscanf(rp, " %*c %ld %ld %ld", &pp, &pg, &se) != 3)
	return -1;
    *ppid = (pid_t) pp;
    *pgrp = (pid_t) pg;
    *sid = (pid_t) se;
    return 0;
}

static int read_cmd(pid_t pid, char *buf, size_t cap)
{
    char path[64];
    size_t n;
    size_t i;
    FILE *f;

    snprintf(path, sizeof path, "/proc/%d/cmdline", (int) pid);
    f = fopen(path, "r");
    if (!f)
	return -1;
    n = fread(buf, 1, cap - 1, f);
    fclose(f);
    if (n == 0)
	return -1;
    for (i = 0; i < n; i++)
	if (buf[i] == 0)
	    buf[i] = ' ';
    while (n > 0 && buf[n - 1] == ' ')
	buf[--n] = 0;
    return 0;
}

static int read_comm(pid_t pid, char *buf, size_t cap)
{
    char path[64];
    FILE *f;

    snprintf(path, sizeof path, "/proc/%d/comm", (int) pid);
    f = fopen(path, "r");
    if (!f)
	return -1;
    if (!fgets(buf, (int) cap, f)) {
	fclose(f);
	return -1;
    }
    fclose(f);
    buf[strcspn(buf, "\n")] = 0;
    return 0;
}

static void cmd_name(const char *cmd, char *out, size_t cap)
{
    const char *end = cmd + strcspn(cmd, " ");
    const char *base = cmd;
    const char *p;
    size_t l;

    for (p = cmd; p < end; p++)
	if (*p == '/')
	    base = p + 1;
    l = (size_t) (end - base);
    if (l >= cap)
	l = cap - 1;
    memcpy(out, base, l);
    out[l] = 0;
}

static const char *shell_base(void)
{
    const char *sh = getenv("SHELL");
    const char *b;

    if (!sh || !*sh)
	return NULL;
    b = strrchr(sh, '/');
    return b ? b + 1 : sh;
}

static int
read_proc(pid_t pid, char *name, size_t ncap, char *cmd,
	  size_t ccap, pid_t *ppid, pid_t *pgid)
{
    pid_t sid;

    if (read_stat(pid, ppid, pgid, &sid) < 0)
	return -1;
    if (read_cmd(pid, cmd, ccap) < 0)
	read_comm(pid, cmd, ccap);
    cmd_name(cmd, name, ncap);
    return 0;
}

static int shell_only(char *out, size_t cap, const char *name, int skip)
{
    if (!skip)
	snprintf(out, cap, "%s", name);
    return 1;
}

static void append(char *out, size_t cap, size_t *off, const char *s)
{
    size_t l = strlen(s);
    size_t room = cap - 1 - *off;

    if (room > l)
	room = l;
    if (room > 0) {
	memcpy(out + *off, s, room);
	*off += room;
    }
}

int proc_chain(Pty *p, char *out, size_t cap)
{
    pid_t shell = pty_pid(p);
    pid_t fg = -1;
    pid_t sppid, spgid;
    char shell_name[128];
    char shell_cmd[512];
    const char *sb;
    int skip;

    if (shell <= 0 || cap == 0)
	return 0;
    out[0] = 0;

    if (read_proc(shell, shell_name, sizeof shell_name,
		  shell_cmd, sizeof shell_cmd, &sppid, &spgid) < 0)
	return 0;

    sb = shell_base();
    skip = sb && strcmp(shell_name, sb) == 0;

    {
	int fd = pty_fd(p);
	if (fd >= 0)
	    fg = tcgetpgrp(fd);
    }

    if (fg <= 0 || fg == spgid)
	return shell_only(out, cap, shell_name, skip);

    {
	char names[MAX_CHAIN][128];
	char leaf_cmd[512];
	pid_t c = fg;
	int clen = 0;
	int k;
	size_t off = 0;
	int first = 1;

	while (c > 0 && c != shell && clen < MAX_CHAIN) {
	    char cmd[512];
	    pid_t pp, pg;

	    if (read_proc(c, names[clen], sizeof names[clen],
			  cmd, sizeof cmd, &pp, &pg) < 0)
		break;
	    if (clen == 0)
		snprintf(leaf_cmd, sizeof leaf_cmd, "%s", cmd);
	    clen++;
	    c = pp;
	}
	if (clen == 0 || c != shell)
	    return shell_only(out, cap, shell_name, skip);

	if (!skip) {
	    append(out, cap, &off, shell_name);
	    first = 0;
	}
	for (k = clen - 1; k >= 1; k--) {
	    if (off >= cap - 2)
		break;
	    if (!first)
		append(out, cap, &off, " -> ");
	    append(out, cap, &off, names[k]);
	    first = 0;
	}
	if (off < cap - 2) {
	    if (!first)
		append(out, cap, &off, " -> ");
	    append(out, cap, &off, leaf_cmd[0] ? leaf_cmd : names[0]);
	}
	out[off] = 0;
	return 1;
    }
}
