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

    assert(ftp_set_variable(&ftp, FTP_VAR_HOST, AS400_HOST) == 0);
    assert(ftp_set_variable(&ftp, FTP_VAR_VERBOSE, AS400_VERBOSITY) == 0);

    assert(ftp_connect(&ftp) == 0);

    rc = ftp_cmd(&ftp, "RCMD CRTSAVF QTEMP/ZSTEST\r\n");
    assert(ftp_dfthandle(&ftp, rc, 530) == 0);

    rc = ftp_cmd(&ftp, "USER %s\r\n", AS400_USER);
    assert(ftp_dfthandle(&ftp, rc, 331) == 0);

    rc = ftp_cmd(&ftp, "PASS %s\r\n", AS400_PASS);
    assert(ftp_dfthandle(&ftp, rc, 230) == 0);

    rc = ftp_cmd(&ftp, "RCMD CRTSAVF QTEMP/ZSTEST\r\n");
    assert(ftp_dfthandle(&ftp, rc, 250) == 0);

    ftp_close(&ftp);

    return 0;
}
