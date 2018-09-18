# zs - work with, and move objects from one AS/400 to another.
# file is used for testing zs
# Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
# See LICENSE

CFLAGS	= -O2 -Wall -Wextra -Wpedantic -Wshadow
TFILES	= 01-init.t		\
	  02-setvar.t		\
	  03-connect.t		\
	  04-crtsavf.t		\
	  05-crtsavf2.t		\
	  06-expectlogin.t	\
	  07-failwrite.t	\
	  08-threadsafe.t	\
	  09-write.t		\
	  10-put.t		\
	  11-get.t		\
	  12-EFTP.t

all:	$(TFILES)
.PHONY:	all

./%.t:	%.o ../../ftp.o ../config.h
	$(CC) $(CFLAGS) -o $@ $< ../../ftp.o
	./$@

../../ftp.o:	../../ftp.h ../../ftp.c
	$(MAKE) -C ../../ ftp.o

clean:
	-rm $(TFILES)
.PHONY:	clean
