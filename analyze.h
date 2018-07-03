/*
 * zs - work with, and move objects from one AS/400 to another.
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#ifndef ANALYZE_H
#define ANALYZE_H 1

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

struct dspfdtab {
	char aprcen[1];
	char aprdat[6];
	char aprtim[6];
	char apfile[10];
	char aplib[10];
	char apftyp[1];
	char apfila[4];
	char apmxd[3];
	char apfatr[6];
	char apsysn[8];
	char apasp[5];
	char apres[4];
	char apmant[1];
	char apuniq[1];
	char apkeyo[1];
	char apselo[1];
	char apaccp[1];
	char apnsco[5];
	char apbof[10];
	char apbol[10];
	char apbolf[10];
	char apnkyf[5];
	char apkeyf[10];
	char apkseq[1];
	char apksin[1];
	char apkzd[1];
	char apkasq[1];
	char apkeyn[5];
	char apjoin[1];
	char apacpj[1];
	char apriky[1];
	char apuuiv[17];
	char NL[1];
} __attribute__((packed));

#endif
