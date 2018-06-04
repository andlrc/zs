#ifndef UTIL_H
#define UTIL_H 1

enum util_errors {
	EUTIL_SUCCESS = 0,
	EUTIL_MISVAL,
	EUTIL_UNKNOWNKEY,
	EUTIL_MAXLIB,
	EUTIL_SYSTEM = 99
};

int util_parsecfg(struct serveropt *server, char *filename);
int util_parselibl(struct sourceopt *sourceopt, char *optlibl);
int util_parseobj(struct object *obj, char *optobj);
const char *util_strerror(int errnum);
#endif
