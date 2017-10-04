CFLAGS	= -Wall -Werror -O2
OFILES	= main.o zs.o

.PHONY:	all clean debug install uninstall
all:	zs README

zs:	$(OFILES)
	$(CC) -o $@ $(OFILES)

zs.o:	zs.c zs.h
main.o:	zs.h main.h main.c

clean:
	-rm zs $(OFILES) *~

debug: clean
	$(MAKE) CFLAGS="-g"

README:	zs.1 readme.sed
	sed -f readme.sed $< | MANWIDTH=80 man -l - > $@

install:
	cp zs /usr/bin/zs
	cp zs.1 /usr/share/man/man1/zs.1
	mkdir /etc/zs
	chmod 755 /etc/zs
	cp default.conf /etc/zs/default.conf
	chmod 600 /etc/zs/default.conf

uninstall:
	-rm /usr/bin/zs
	-rm /usr/share/man/man1/zs.1
	-rm -r /etc/zs
