/*
 * zs - work with, and move objects from one AS/400 to another.
 * file is used for testing zs
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */

#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include "../config.h"
#include "util.h"

struct file {
    int             fd;
    int             origfd;
    char           *buffer;
    size_t          length;
    size_t          size;
    int             done;
};

static
int
readpartfd(struct file *f)
{
    ssize_t         readlen;
    struct timespec sleeper;

    sleeper.tv_sec = 0;
    sleeper.tv_nsec = 100L * 1000L * 1000L;

    /* EOF reached */
    if (f->done)
	return 0;

    /* realloc */
    if (f->size == f->length) {
	f->size *= 2;
	f->buffer = realloc(f->buffer, f->size);
	if (f->buffer == NULL) {
	    err(1, "realloc");
	}
    }

    readlen = read(f->fd, f->buffer + f->length, f->size - f->length);

    switch (readlen) {
    case -1:
	if (errno == EAGAIN) {
	    nanosleep(&sleeper, NULL);
	    return -1;
	}
	err(1, "read");
	break;
    case 0:
	f->done = 1;
	return 0;
    default:
	dprintf(f->origfd, "%*s", (int) readlen, f->buffer + f->length);
	f->length += readlen;
    }
    return readlen;
}

static
void
readfiles(char **pstdout, char **pstderr, int outfd, int errfd)
{
    struct file     stdout = { outfd, STDOUT_FILENO, NULL, 0, 0, 0 };
    struct file     stderr = { errfd, STDERR_FILENO, NULL, 0, 0, 0 };

    /* allocate stdout */
    stdout.size = BUFSIZ;
    stdout.buffer = malloc(stdout.size);
    if (stdout.buffer == NULL) {
	err(1, "alloc");
    }

    /* allocate stderr */
    stderr.size = BUFSIZ;
    stderr.buffer = malloc(stderr.size);
    if (stderr.buffer == NULL) {
	err(1, "alloc");
    }

    while (readpartfd(&stdout) != 0 || readpartfd(&stderr) != 0)
	/* read more */;

    close(stdout.fd);
    close(stderr.fd);

    *pstdout = stdout.buffer;
    *pstderr = stderr.buffer;
}

int
runcmd(int *exit_status, char **stdout, char **stderr, char *const *cmd)
{
    int             _exit_status;
    int             stdoutfd[2];
    int             stderrfd[2];
    char * const *  p;

    *exit_status = 0;
    *stdout = NULL;
    *stderr = NULL;

    if (pipe(stdoutfd) != 0) {
	err(1, "pipe");
    }

    if (pipe(stderrfd) != 0) {
	err(1, "pipe");
    }

    /* print command line */
    printf("%s", *cmd);
    for (p = cmd + 1; *p; p++) {
	printf(" %s", *p);
    }
    printf("\n");

    switch (fork()) {
    case -1:
	err(1, "fork");
	break;
    case 0:
	if (close(stdoutfd[0]) == -1) {
	    err(1, "close");
	}
	if (close(stderrfd[0]) == -1) {
	    err(1, "close");
	}
	if (dup2(stdoutfd[1], STDOUT_FILENO) == -1) {
	    err(1, "dup2");
	}
	if (dup2(stderrfd[1], STDERR_FILENO) == -1) {
	    err(1, "dup2");
	}
	execv(*cmd, cmd);
	err(1, "execv");
	break;
    default:
	if (close(stdoutfd[1]) == -1) {
	    err(1, "close");
	}
	if (close(stderrfd[1]) == -1) {
	    err(1, "close");
	}
	if (fcntl(stdoutfd[0], F_SETFL, O_NONBLOCK) == -1) {
	    err(1, "fcntl");
	}
	if (fcntl(stderrfd[0], F_SETFL, O_NONBLOCK) == -1) {
	    err(1, "fcntl");
	}

	readfiles(stdout, stderr, stdoutfd[0], stderrfd[0]);

	wait(&_exit_status);
	if (!WIFEXITED(_exit_status)) {
	    err(1, "wait");
	}
	*exit_status = WEXITSTATUS(_exit_status);

	break;
    }

    return 0;
}
