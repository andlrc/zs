#ifndef ZS_H
#define ZS_H 1

#define Z_LIBLMAX	16
#define Z_OBJMAX	128

#define Z_LIBSIZ	11
#define Z_OBJSIZ	11
#define Z_TYPSIZ	11

struct object {
	char lib[Z_LIBSIZ];
	char obj[Z_OBJSIZ];
	char type[Z_TYPSIZ];
};

struct sourceopt {
	struct ftpserver server;
	int pipe;
	char libl[Z_LIBLMAX][Z_LIBSIZ];
	struct object objects[Z_OBJMAX];
};

struct targetopt {
	struct ftpserver server;
	int pipe;
	char lib[Z_LIBSIZ];
};

#endif
