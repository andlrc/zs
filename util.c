/*
 * zs - copy objects from one AS/400 to another
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

#include "ftp.h"
#include "zs.h"
#include "util.h"

/* error message follow the index of "enum util_errors" */
static const char *util_error_messages[] = {
	"Success",
	"Missing value for option",
	"Unknown option",
	"Maximum libraries reached",
	"Maximum types reached"
};

int util_parsecfg(struct ftp *ftp, char *filename)
{
	/* ($option <sp> $value <nl>)* */
	int returncode;
	FILE *fp;
	char *line, *pline;
	char *key;
	size_t linesiz;
	char filenamebuf[BUFSIZ];

	line = NULL;
	linesiz = 0;

	/* use /etc/zs/$FILE.conf instead */
	if (!strchr(filename, '/') && !strchr(filename, '.')) {
		snprintf(filenamebuf, sizeof(filenamebuf),
			 "/etc/zs/%s.conf", filename);
		filename = filenamebuf;
	}

	fp = fopen(filename, "r");
	if (fp == NULL) {
		return EUTIL_SYSTEM;
	}

	returncode = 0;
	while (getline(&line, &linesiz, fp) > 0) {
		pline = line;

		while (isspace(*pline))
			pline++;

		if (*pline == '#' || *pline == ';' || *pline == '\0')
			continue;

		key = strsep(&pline, " \t");
		if (*pline == '\0') {
			returncode = EUTIL_NOVAL;
			goto exit;
		}

		while (isspace(*pline))
			pline++;

		*(strchr(pline, '\n')) = '\0';

		if (strcmp(key, "server") == 0 || strcmp(key, "host") == 0) {
			ftp_set_variable(ftp, FTP_VAR_HOST, pline);
		} else if (strcmp(key, "user") == 0
			   || strcmp(key, "username") == 0) {
			ftp_set_variable(ftp, FTP_VAR_USER, pline);
		} else if (strcmp(key, "password") == 0) {
			ftp_set_variable(ftp, FTP_VAR_PASSWORD, pline);
		} else if (strcmp(key, "port") == 0) {
			ftp_set_variable(ftp, FTP_VAR_PORT, pline);
		} else if (strcmp(key, "tries") == 0
			   || strcmp(key, "maxtries") == 0) {
			ftp_set_variable(ftp, FTP_VAR_MAXTRIES, pline);
		} else {
			returncode = EUTIL_BADKEY;
			goto exit;
		}
	}

exit:	free(line);
	fclose(fp);
	return returncode;
}

int util_parselibl(struct sourceopt *sourceopt, char *optlibl)
{
	/* "lib1,lib2,...,libN" */
	char *saveptr, *p;
	int i = 0;

	p = strtok_r(optlibl, ",", &saveptr);
	do {
		if (i == Z_LIBLMAX) {
			return EUTIL_LIBOVERFLOW;
		}

		strncpy(sourceopt->libl[i], p, Z_LIBSIZ);
		sourceopt->libl[i][Z_LIBSIZ - 1] = '\0';
		i++;
	} while ((p = strtok_r(NULL, ",", &saveptr)) != NULL);

	return 0;
}

/*
 * parse searched types
 * split the input types on comma and store in "sourceopt->types"
 * $types = type1,type2,type3
 * $type  = *type | type
 */
int util_parsetypes(struct sourceopt *sourceopt, char *opttypes)
{
	char *saveptr, *p, *dest;
	int i = 0;

	p = strtok_r(opttypes, ",", &saveptr);
	do {
		if (i == Z_TYPEMAX) {
			return EUTIL_TYPEOVERFLOW;
		}

		dest = sourceopt->types[i];

		/* add leading asterisk if omitted */
		if (*p != '*') {
			*dest++ = '*';
		}

		strncpy(dest, p, Z_TYPESIZ - (dest - sourceopt->types[i]));
		sourceopt->types[i][Z_TYPESIZ - 1] = '\0';
		i++;
	} while ((p = strtok_r(NULL, ",", &saveptr)) != NULL);

	return 0;
}

int util_parseobj(struct object *obj, char *optobj)
{
	/*
	 * obj   = ( $libl "/" )? $obj ( "*" $type )?
	 * $libl = \w{1,10}
	 * $obj  = \w{1,10}
	 * $type = \w{1,10}
	 * If $type is missing then "ALL" is used
	 */
	char *saveptr, *p;

	/* $libl */
	if (strchr(optobj, '/')) {
		p = strtok_r(optobj, "/", &saveptr);
		strncpy(obj->lib, p, Z_LIBSIZ);
		obj->lib[Z_LIBSIZ - 1] = '\0';

		p = strtok_r(NULL, "*", &saveptr);
	} else {
		p = strtok_r(optobj, "*", &saveptr);
	}

	/* $obj */
	strncpy(obj->obj, p, Z_OBJSIZ);
	obj->obj[Z_OBJSIZ - 1] = '\0';

	/* $type */
	p = strtok_r(NULL, "?", &saveptr);
	if (p != NULL) {
		*obj->type = '*';
		strncpy(obj->type + 1, p, Z_TYPESIZ - 1);
		obj->type[Z_TYPESIZ - 2] = '\0';
	}

	return 0;
}

/*
 * print errors.
 * should always be called immediately after an error occurred as the value of
 * "errno" cannot change.
 */
const char *util_strerror(int errnum)
{
	switch (errnum) {
	case EUTIL_SYSTEM:
		return strerror(errno);
	default:
		return util_error_messages[errnum];
	}
}
