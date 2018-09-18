# zs - work with, and move objects from one AS/400 to another.
# file is used for testing zs
# Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
# See LICENSE

CFLAGS	= -O2 -Wall -Wextra -Wpedantic -Wshadow
TFILES	= 01-args.t

all:	$(TFILES)
.PHONY:	all

./%.t:	%.o util.o ../../zs ../config.h
	$(CC) $(CFLAGS) -o $@ $< util.o
	./$@

util.o:	../config.h util.h util.c

../../zs:
	$(MAKE) -C ../../
.PHONY:	zs

clean:
	-rm $(TFILES)
	-rm util.o
.PHONY:	clean
