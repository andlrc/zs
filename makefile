# zs - copy objects from one AS/400 to another
# Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
# See LICENSE

CFLAGS	= -O2 -Wall -Wextra -Wpedantic -Wshadow

ZS_O	= zs.o util.o ftp.o
ZSA_O	= analyze.o util.o ftp.o

all:	zs zs-analyze
.PHONY:	all
zs:	$(ZS_O)
	$(CC) $(CFLAGS) -o $@ $^

ftp.o:	ftp.h ftp.c
zs.o:	ftp.h zs.h util.h zs.c
util.o:	ftp.h zs.h util.h util.c

zs-analyze:	$(ZSA_O)
	$(CC) $(CFLAGS) -o $@ $^

analyze.o:	analyze.h analyze.c

clean:
	-rm $(ZS_O)
	-rm zs
	-rm $(ZSA_O)
	-rm zs-analyze
.PHONY:	clean
