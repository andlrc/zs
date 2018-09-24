# zs - work with, and move objects from one AS/400 to another.
# file is used for testing zs
# Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
# See LICENSE

CFLAGS	= -O2 -Wall -Wextra -Wpedantic -Wshadow

FTP_TFILES	= ftp/01-init.t		\
		  ftp/02-setvar.t	\
		  ftp/03-connect.t	\
		  ftp/04-crtsavf.t	\
		  ftp/05-crtsavf2.t	\
		  ftp/06-expectlogin.t	\
		  ftp/07-failwrite.t	\
		  ftp/08-threadsafe.t	\
		  ftp/09-write.t	\
		  ftp/10-put.t		\
		  ftp/11-get.t		\
		  ftp/12-EFTP.t

COPY_TFILES	= zs-copy/01-args.t

all:	$(FTP_TFILES) $(COPY_TFILES)
.PHONY:	all

# shared
config.h:	build-config.bash
	./build-config.bash

# ftp files
ftp/%.t:	ftp/%.o ../ftp.o config.h
	$(CC) $(CFLAGS) -o $@ $< ../ftp.o
	./$@

../ftp.o:	../ftp.h ../ftp.c
	$(MAKE) -C ../ ftp.o

# zs-copy files
zs-copy/%.t:	zs-copy/%.o zs-copy/util.o ../zs config.h
	$(CC) $(CFLAGS) -o $@ $< zs-copy/util.o
	./$@

zs-copy/util.o:	config.h zs-copy/util.h zs-copy/util.c

../zs:
	$(MAKE) -C ../
.PHONY:	zs

clean:
	-rm $(FTP_TFILES) $(COPY_TFILES)
.PHONY:	clean
