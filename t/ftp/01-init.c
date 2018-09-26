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

    ftp_init(&ftp);

    assert(ftp.recvline.buffer == NULL);
    assert(ftp.server.port == FTP_PORT);
    assert(ftp.server.maxtries == 100);

    return 0;
}
