#ifndef UTIL_H
#define UTIL_H 1

enum util_errors {
	EUTIL_SUCCESS = 0,
	EUTIL_NOVAL,
	EUTIL_BADKEY,
	EUTIL_LIBOVERFLOW,
	EUTIL_TYPEOVERFLOW,
	EUTIL_SYSTEM = 99
};

int util_parsecfg(struct ftp *, char *);
int util_parselibl(struct sourceopt *, char *);
int util_parsetypes(struct sourceopt *, char *);
int util_parseobj(struct object *, char *);
const char *util_strerror(int errnum);
#endif
