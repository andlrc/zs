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
