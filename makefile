CFLAGS	= -Wall -Werror
OFILES	= main.o zs.o

all:	zs

zs:	$(OFILES)
	$(CC) -o $@ $(OFILES)

zs.o:	zs.c zs.h
main.o:	zs.h main.h main.c

clean:
	-rm zs $(OFILES)

debug: clean
	$(MAKE) CFLAGS="-g"

install:
	cp zs /usr/bin/zs
	cp zs.1 /usr/share/man/man1/zs.1

uninstall:
	-rm /usr/bin/zs
	-rm /usr/share/man/man1/zs.1
