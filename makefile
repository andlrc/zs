# zs - work with, and move objects from one AS/400 to another.
# Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
# See LICENSE

CFLAGS	= -O2 -Wall -Wextra -Wpedantic -Wshadow

OFILES	= main.o analyze.o copy.o ftp.o util.o

all:	zs
.PHONY:	all
zs:	$(OFILES)
	$(CC) $(CFLAGS) -o $@ $^

ftp.o:		ftp.h ftp.c
copy.o:		ftp.h zs.h util.h copy.c
util.o:		ftp.h zs.h util.h util.c
analyze.o:	ftp.h zs.h util.h analyze.h analyze.c

clean:
	-rm $(OFILES)
	-rm zs
.PHONY:	clean
