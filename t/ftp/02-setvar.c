/*
 * zs - work with, and move objects from one AS/400 to another.
 * file is used for testing zs
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../config.h"
#include "../../ftp.h"

int main(void)
{
    struct ftp      ftp;

    ftp_init(&ftp);

    assert(ftp_set_variable(&ftp, FTP_VAR_HOST, "HOST") == 0);
    assert(strcmp(ftp.server.host, "HOST") == 0);

    assert(ftp_set_variable(&ftp, FTP_VAR_USER, "USER") == 0);
    assert(strcmp(ftp.server.user, "USER") == 0);

    assert(ftp_set_variable(&ftp, FTP_VAR_PASSWORD, "PASS") == 0);
    assert(strcmp(ftp.server.password, "PASS") == 0);

    assert(ftp_set_variable(&ftp, FTP_VAR_VERBOSE, "1") == 0);
    assert(ftp.verbosity == 1);

    assert(ftp_set_variable(&ftp, FTP_VAR_VERBOSE, "+1") == 0);
    assert(ftp.verbosity == 2);

    assert(ftp_set_variable(&ftp, FTP_VAR_VERBOSE, "-1") == 0);
    assert(ftp.verbosity == 1);

    assert(ftp_set_variable(&ftp, FTP_VAR_PORT, "8000") == 0);
    assert(ftp.server.port == 8000);

    assert(ftp_set_variable(&ftp, FTP_VAR_MAXTRIES, "500") == 0);
    assert(ftp.server.maxtries == 500);

    return 0;
}
