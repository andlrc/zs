/*
 * zs-analyze - Print depends and dependencies for objects
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include "ftp.h"
#include "zs.h"
#include "util.h"
#include "analyze.h"

/* FIXME */
#define YYY(x, y)	0
#define ZZZ(x, y)	

/* set by main with argv[0] */
char *program_name;
#define PROGRAM_VERSION	"0.1"

static void print_version(void)
{
	printf("%s %s\n", program_name, PROGRAM_VERSION);
}

static void print_help(void)
{
	printf("Usage %s [OPTION]... OBJECT...\n"
	       "Copy objects from one AS/400 to another\n"
	       "\n"
	       "  -s host       set source host\n"
	       "  -u user       set source user\n"
	       "  -p port       set source port\n"
	       "  -m tries      set maximum tries for source to respond\n"
	       "  -c file       source config file\n"
	       "\n"
	       "  -v            level of verbosity, can be set multiple times\n"
	       "  -V            output version information and exit\n"
	       "  -h            show this help message and exit\n"
	       "\n"
	       "See zs-analyze(1) for more information\n",
	       program_name);
}

/* print error prefixed with "program_name" */
static void print_error(char *format, ...)
{
	va_list ap;
	char buf[BUFSIZ];
	int len;

	len = snprintf(buf, sizeof(buf), "%s: ", program_name);

	va_start(ap, format);
	vsnprintf(buf + len, sizeof(buf) - len, format, ap);
	va_end(ap);

	fputs(buf, stderr);
}


/* get fd with output of cmd, returns -1 on error */
static int freadcmd(struct ftp *ftp, char *cmd, char *fromfile)
{
	int rc, fd;
	char localname[PATH_MAX];
	char remotename[PATH_MAX];

	rc = ftp_cmd(ftp, cmd);
	if (ftp_dfthandle(ftp, rc, 250) != 0) {
		print_error("failed to run command: %s\n", ftp_strerror(ftp));
		return -1;
	}

	/* TODO: unique file */
	snprintf(remotename, sizeof(remotename), "/tmp/zs-readcmd");
	rc = ftp_cmd(ftp, "RCMD CPYTOIMPF FROMFILE(%s) TOSTMF('%s') MBROPT(*REPLACE) STMFCCSID(1208) RCDDLM(*LF) DTAFMT(*FIXED)\r\n",
		     fromfile, remotename);
	if (ftp_dfthandle(ftp, rc, 250) != 0) {
		print_error("failed to copy to import-file: %s\n",
			    ftp_strerror(ftp));
		return -1;
	}

	/* creat local file */
	strcpy(localname, "/tmp/zs-XXXXXX");
	fd = mkstemp(localname);
	if (fd == -1) {
		print_error("failed to create output file: %s\n",
			    strerror(errno));
		return -1;
	}

	/*
	 * download remote file => local file
	 * remote local file asap, as it will then be removed when "close(fd)"
	 * is called.
	 */
	if (ftp_get(ftp, localname, remotename) != 0) {
		print_error("failed to get file: %s\n", ftp_strerror(ftp));
		unlink(localname);
		close(fd);
		return -1;
	}
	unlink(localname);

	/* delete remote */
	rc = ftp_cmd(ftp, "DELETE %s\n", remotename);
	if (ftp_dfthandle(ftp, rc, 250) != 0) {
		print_error("failed to remove tempfile: %s\n",
			    ftp_strerror(ftp));
		close(fd);
		return -1;
	}

	return fd;
}

static int getobjects(struct ftp *ftp, char *lib, char *obj)
{
	struct dspdbrtab dbrtab;
	struct dspfdtab fdtab;
	int rc;
	int fd;
	char cmd[BUFSIZ];
	char wlib[Z_LIBSIZ];
	char wobj[Z_OBJSIZ];
	char *p;

	/* DSPFD */
	snprintf(cmd, sizeof(cmd),
		 "RCMD DSPFD FILE(%s/%s) TYPE(*ACCPTH) OUTPUT(*OUTFILE) OUTFILE(QTEMP/FD)\r\n",
		 lib, obj);

	fd = freadcmd(ftp, cmd, "QTEMP/FD");
	if (fd == -1)
		return 1;

	rc = ftp_cmd(ftp, "RCMD DLTF FILE(QTEMP/FD)\r\n");
	if (ftp_dfthandle(ftp, rc, 250) == -1) {
		print_error("failed to remove DFPFD file: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	while (read(fd, &fdtab, sizeof(struct dspfdtab))
		   == sizeof(struct dspfdtab)) {
		snprintf(wlib, sizeof(wlib), "%s", fdtab.apbol);
		if ((p = strchr(wlib, ' ')) != NULL)
			*p = '\0';
		snprintf(wobj, sizeof(wobj), "%s", fdtab.apbof);
		if ((p = strchr(wobj, ' ')) != NULL)
			*p = '\0';

		/* no more */
		if (*wlib == '\0' || *wobj == '\0')
			break;

		if (!YYY(wlib, wobj)) {
			ZZZ(wlib, wobj);
			printf("%s/%s*FILE\n", wlib, wobj);
			if (getobjects(ftp, wlib, wobj) == -1)
				return 1;
		}
	}
	close(fd);

	/* DSPDBR */
	snprintf(cmd, sizeof(cmd),
		 "RCMD DSPDBR FILE(%s/%s) OUTPUT(*OUTFILE) OUTFILE(QTEMP/DBR)\r\n",
		 lib, obj);

	fd = freadcmd(ftp, cmd, "QTEMP/DBR");
	if (fd == -1)
		return 1;

	rc = ftp_cmd(ftp, "RCMD DLTF FILE(QTEMP/DBR)\r\n");
	if (ftp_dfthandle(ftp, rc, 250) == -1) {
		print_error("failed to remove DSPDBR file: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	while (read(fd, &dbrtab, sizeof(struct dspdbrtab))
		   == sizeof(struct dspdbrtab)) {
		snprintf(wlib, sizeof(wlib), "%s", dbrtab.whreli);
		if ((p = strchr(wlib, ' ')) != NULL)
			*p = '\0';
		snprintf(wobj, sizeof(wobj), "%s", dbrtab.whrefi);
		if ((p = strchr(wobj, ' ')) != NULL)
			*p = '\0';

		/* no more */
		if (*wlib == '\0' || *wobj == '\0')
			break;

		if (!YYY(wlib, wobj)) {
			ZZZ(wlib, wobj);
			printf("%s/%s*FILE\n", wlib, wobj);
			if (getobjects(ftp, wlib, wobj) == -1)
				return 1;
		}
	}
	close(fd);

	return 0;
}

int main(int argc, char **argv)
{
	struct ftp ftp;
	int c, argind;
	int rc;
	char lib[Z_LIBSIZ];
	char obj[Z_OBJSIZ];
	char *p;

	program_name = strrchr(argv[0], '/');
	if (program_name)
		program_name++;
	else
		program_name = argv[0];

	ftp_init(&ftp);

	while ((c = getopt(argc, argv, "Vhvs:u:p:m:c:")) != -1) {
		switch (c) {
		case 'V':	/* version */
			print_version();
			return 0;
		case 'h':	/* help */
			print_help();
			return 0;
		case 'v':	/* verbosity */
			ftp_set_variable(&ftp, FTP_VAR_VERBOSE, "+1");
			break;
		case 's':	/* source host */
			ftp_set_variable(&ftp, FTP_VAR_HOST, optarg);
			break;
		case 'u':	/* source user */
			ftp_set_variable(&ftp, FTP_VAR_USER, optarg);
			break;
		case 'p':	/* source port */
			ftp_set_variable(&ftp, FTP_VAR_PORT, optarg);
			break;
		case 'm':	/* source max tries */
			ftp_set_variable(&ftp, FTP_VAR_MAXTRIES, optarg);
			break;
		case 'c':	/* source config */
			rc = util_parsecfg(&ftp, optarg);
			if (rc != 0)
				print_error("failed to parse config file: %s\n",
					    util_strerror(rc));
			break;
		default:
			return 2;
		}
	}

	if (optind >= argc) {
		print_error("missing object\n");
		return 2;
	}

	if (ftp_connect(&ftp) == -1) {
		print_error("failed to connect to server: %s\n",
			    ftp_strerror(&ftp));
		return 1;
	}

	for (argind = optind; argind < argc; argind++) {
		p = strchr(argv[argind], '/');
		if (p == NULL) {
			print_error("failed to parse object: %s\n",
				    argv[argind]);
			continue;
		}

		strncpy(lib, argv[argind], sizeof(lib) - 1);
		if ((ptrdiff_t) (p - argv[argind]) < (ptrdiff_t) sizeof(lib))
			lib[p - argv[argind]] = '\0';
		else
			lib[sizeof(lib) - 1] = '\0';
		strncpy(obj, p + 1, sizeof(obj) - 1);
		obj[sizeof(obj) - 1] = '\0';

		if (getobjects(&ftp, lib, obj) != 0) {
			ftp_close(&ftp);
			return 1;
		}
	}

	ftp_close(&ftp);
	return 0;
}
