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
