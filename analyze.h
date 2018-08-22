/*
 * zs - work with, and move objects from one AS/400 to another.
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#ifndef ANALYZE_H
#define ANALYZE_H 1

#define INIT_TAB_SIZE 64

struct ctx {
	char **tab;
	unsigned int tablen;
	unsigned int tabsiz;
	struct ftp ftp;
};

struct dspdbrtab {
	char whrtyp[1];
	char whrfi[10];
	char whrli[10];
	char whrmb[10];
	char whrrd[10];
	char whno[7];
	char whdtm[13];
	char whrefi[10];
	char whreli[10];
	char whremb[10];
	char whtype[1];
	char whjdil[4];
	char whjref[4];
	char whsysn[8];
	char whctln[10];
	char whcstn[258];
	char NL[1];
} __attribute__((packed));

#endif
