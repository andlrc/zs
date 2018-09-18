/*
 * zs - work with, and move objects from one AS/400 to another.
 * file is used for testing zs
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include "../config.h"
#include "../../ftp.h"

int main(void)
{
    struct ftp      ftp;
    int             rc;
    int             destfd;
    char            localname[PATH_MAX];
    char            remotename[PATH_MAX];

    ftp_init(&ftp);

    assert(ftp_set_variable(&ftp, FTP_VAR_HOST, AS400_HOST) == 0);
    assert(ftp_set_variable(&ftp, FTP_VAR_USER, AS400_USER) == 0);
    assert(ftp_set_variable(&ftp, FTP_VAR_PASSWORD, AS400_PASS) == 0);
    assert(ftp_set_variable(&ftp, FTP_VAR_VERBOSE, AS400_VERBOSITY) == 0);

    assert(ftp_connect(&ftp) == 0);

    strcpy(localname, "/tmp/zstest-XXXXXX");
    strcpy(remotename, "/tmp/zstest");

    assert((destfd = mkstemp(localname)) != -1);
    assert(close(destfd) == 0);
    assert(ftp_put(&ftp, localname, remotename) == 0);

    assert(ftp_get(&ftp, localname, remotename) == 0);

    rc = ftp_cmd(&ftp, "RCMD QSH CMD('rm %s')\r\n", remotename);
    assert(ftp_dfthandle(&ftp, rc, 250) == 0);
    assert(unlink(localname) == 0);

    ftp_close(&ftp);

    return 0;
}
