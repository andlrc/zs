/*
 * zs - work with, and move objects from one AS/400 to another.
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#ifndef UTIL_H
#define UTIL_H 1

enum util_errors {
    EUTIL_SUCCESS = 0,
    EUTIL_NOVAL,
    EUTIL_BADKEY,
    EUTIL_LIBOVERFLOW,
    EUTIL_TYPEOVERFLOW,
    EUTIL_SYSTEM = 99
};

int             util_parsecfg(struct ftp *, char *);
int             util_parselibl(struct sourceopt *, char *);
int             util_parsetypes(struct sourceopt *, char *);
int             util_parseobj(struct object *, char *);
const char     *util_strerror(int errnum);
void            util_guessrelease(char *release, struct ftp *sourceftp,
				  struct ftp *targetftp);
#endif
