# zs - copy objects from one AS/400 to another
# Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
# See LICENSE

CFLAGS	= -O2 -Wall -Wextra -Wpedantic -Wshadow

OFILES	= main.o util.o ftp.o

all:	zs
.PHONY:	all
zs:	$(OFILES)
	$(CC) $(CFLAGS) -o $@ $^

ftp.o:	ftp.h ftp.c
main.o:	ftp.h zs.h util.h main.c
util.o:	ftp.h zs.h util.h util.c

clean:
	-rm $(OFILES)
	-rm zs
.PHONY:	clean
