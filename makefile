CFLAGS	= -O2 -Wall -Wextra -Wpedantic -Wshadow

OFILES	= main.o ftp.o

all:	zs
.PHONY:	all
zs:	$(OFILES)
	$(CC) $(CFLAGS) -o $@ $^

main.o:	zs.h ftp.h main.c
ftp.o:	ftp.h ftp.c

clean:
	-rm $(OFILES)
	-rm zs
.PHONY:	clean
