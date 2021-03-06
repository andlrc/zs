/*
 * zs - work with, and move objects from one AS/400 to another.
 * file is used for testing zs
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#include <stdio.h>
#include <assert.h>
#include "../config.h"
#include "../../ftp.h"

int
main(void)
{
    struct ftp      ftp;
    int             rc;

    ftp_init(&ftp);

    assert(ftp_connect(&ftp) == -1);
    assert(ftp.errnum == EFTP_NOHOST);

    assert(ftp_set_variable(&ftp, FTP_VAR_HOST, AS400_HOST) == 0);
    assert(ftp_set_variable(&ftp, FTP_VAR_VERBOSE, AS400_VERBOSITY) == 0);

    assert(ftp_connect(&ftp) == 0);

    rc = ftp_cmd(&ftp, "RCMD CRTSAVF QTEMP/ZSTEST\r\n");
    assert(ftp_dfthandle(&ftp, rc, 250) == -1);
    assert(ftp.errnum == EFTP_NOLOGIN);

    assert(ftp_set_variable(&ftp, -1, "XXX") == -1);
    assert(ftp.errnum == EFTP_BADVAR);

    return 0;
}
