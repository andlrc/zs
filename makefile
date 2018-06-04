CFLAGS	= -O2 -Wall -Wextra -Wpedantic -Wshadow

OFILES	= main.o util.o ftp.o

all:	zs
.PHONY:	all
zs:	$(OFILES)
	$(CC) $(CFLAGS) -o $@ $^

main.o:	zs.h util.h ftp.h main.c
util.o:	zs.h util.h util.c
ftp.o:	ftp.h ftp.c

clean:
	-rm $(OFILES)
	-rm zs
.PHONY:	clean
