#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

#include "ftp.h"
#include "zs.h"
#include "util.h"

static const char *util_error_messages[] = {
	"success",
	"missing value for option",
	"unknown option",
	"maximum libraries reached",
	"maximum types reached"
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
			returncode = EUTIL_MISVAL;
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
		} else {
			returncode = EUTIL_UNKNOWNKEY;
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
			return EUTIL_MAXLIB;
		}

		strncpy(sourceopt->libl[i], p, Z_LIBSIZ);
		sourceopt->libl[i][Z_LIBSIZ - 1] = '\0';
		i++;
	} while ((p = strtok_r(NULL, ",", &saveptr)) != NULL);

	return 0;
}

int util_parsetypes(struct sourceopt *sourceopt, char *opttypes)
{
	/*
	 * $type  = "*" TYPE | TYPE
	 * $types = $type | $types "," $type
	 * "type1,type2,...typeN"
	 */
	char *saveptr, *p;
	int i = 0;

	p = strtok_r(opttypes, ",", &saveptr);
	do {
		if (i == Z_TYPMAX) {
			return EUTIL_MAXTYP;
		}

		strncpy(sourceopt->types[i], p, Z_TYPSIZ);
		sourceopt->types[i][Z_TYPSIZ - 1] = '\0';
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
	p = strtok_r(NULL, ".", &saveptr);
	if (p != NULL) {
		strncpy(obj->type, p, Z_TYPSIZ);
		obj->type[Z_TYPSIZ - 1] = '\0';
	}

	return 0;
}

const char *util_strerror(int errnum)
{
	switch (errnum) {
	case EUTIL_SYSTEM:
		return strerror(errno);
	default:
		return util_error_messages[errnum];
	}
}
