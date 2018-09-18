/*
 * zs - work with, and move objects from one AS/400 to another.
 * file is used for testing zs
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "../config.h"
#include "util.h"

int main(void)
{
    int             exit_status;
    char           *stdout;
    char           *stderr;

    assert(runcmd(&exit_status, &stdout, &stderr,
		  (char *const[]){ ZS_PATH, "copy", NULL }) == 0);
    assert(exit_status == 2);
    assert(strcmp(stderr, "zs: missing object\n") == 0);
    free(stdout);
    free(stderr);

    assert(runcmd(&exit_status, &stdout, &stderr,
		  (char *const[]){ ZS_PATH, "copy", "obj", NULL }) == 0);
    assert(exit_status == 1);
    assert(strcmp(stderr, "zs: failed to connect to source: Missing host\n") == 0);
    free(stdout);
    free(stderr);

    assert(runcmd(&exit_status, &stdout, &stderr,
		  (char *const[]){ ZS_PATH, "copy", "-s", AS400_HOST,
				   "obj", NULL }) == 0);
    assert(exit_status == 1);
    assert(strcmp(stderr, "zs: failed to connect to target: Missing host\n") == 0);
    free(stdout);
    free(stderr);

    assert(runcmd(&exit_status, &stdout, &stderr,
		  (char *const[]){ ZS_PATH, "copy", "-s", AS400_HOST,
				   "-S", AS400_HOST, "obj", NULL }) == 0);
    assert(exit_status == 1);
    assert(strcmp(stderr, "zs: failed to create save file: Not Logged In\n") == 0);
    free(stdout);
    free(stderr);

    return 0;
}
