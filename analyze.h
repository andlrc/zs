/*
 * zs - work with, and move objects from one AS/400 to another.
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#ifndef ANALYZE_H
#define ANALYZE_H 1

#define INIT_TAB_SIZE 64

struct ctx {
    struct object  *tab;
    unsigned int    tablen;
    unsigned int    tabsiz;
    struct ftp      ftp;
    char            libl[Z_LIBLMAX][Z_LIBSIZ];
};

struct dsppgmref {
    char            whlib[10];
    char            whpnam[10];
    char            whtext[50];
    char            whfnum[7];	/* numeric 5 */
    char            whdttm[13];
    char            whfnam[11];
    char            whlnam[11];
    char            whsnam[11];
    char            whrfno[5];	/* numeric 3 */
    char            whfusg[4];	/* numeric 2 */
    char            whrfnm[10];
    char            whrfsn[13];
    char            whrffn[7];	/* numeric 7 */
    char            whobjt[1];
    char            whotyp[10];
    char            whsysn[8];
    char            whspkg[1];
    char            whrfnb[7];	/* numeric 5 */
    char            __nl[1];    /* trailing newline */
} __attribute__ ((packed));

#endif
