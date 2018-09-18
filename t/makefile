# zs - work with, and move objects from one AS/400 to another.
# file is used for testing zs
# Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
# See LICENSE

all:	ftp zs-copy
.PHONY:	all

ftp:	config.h
	$(MAKE) -C ftp
.PHONY:	ftp

zs-copy:	config.pl
	$(MAKE) -C zs-copy
.PHONY:	zs-copy

config.h:	build-config.bash
	./build-config.bash
