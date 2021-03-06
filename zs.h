/*
 * zs - work with, and move objects from one AS/400 to another.
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#ifndef ZS_H
#define ZS_H 1

#define Z_LIBLMAX	16
#define Z_OBJMAX	128
#define Z_TYPEMAX	16

#define Z_LIBSIZ	11
#define Z_OBJSIZ	11
#define Z_TYPESIZ	11
#define Z_RLSSIZ	11

struct object {
    char            lib[Z_LIBSIZ];
    char            obj[Z_OBJSIZ];
    char            type[Z_TYPESIZ];
};

struct sourceopt {
    int             pipe;
    char            release[Z_RLSSIZ];
    char            libl[Z_LIBLMAX][Z_LIBSIZ];
    struct object   objects[Z_OBJMAX];
    char            types[Z_TYPEMAX][Z_TYPESIZ];
};

struct targetopt {
    int             pipe;
    char            lib[Z_LIBSIZ];
};

void            print_error(char *format, ...);

/*
 * set by main.c
 */
char           *program_name;

#endif
