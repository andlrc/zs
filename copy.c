/*
 * zs - work with, and move objects from one AS/400 to another.
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>

#include "ftp.h"
#include "zs.h"
#include "util.h"

static void
print_help(void)
{
    printf("Usage %s copy [OPTION]... OBJECT...\n"
	   "Copy objects from one AS/400 to another\n"
	   "\n"
	   "  -s host       set source host\n"
	   "  -u user       set source user\n"
	   "  -p port       set source port\n"
	   "  -l libl       set source library list\n"
	   "                comma separated list of libraries\n"
	   "  -t types      set type list\n"
	   "                comma separated list of types\n"
	   "  -m tries      set maximum tries for source to respond\n"
	   "  -r release    set target release\n"
	   "  -c file       source config file\n"
	   "\n"
	   "  -S host       set target host\n"
	   "  -U user       set target user\n"
	   "  -P port       set target port\n"
	   "  -L lib        set target destination library\n"
	   "  -M tries      set maximum tries for target to respond\n"
	   "  -C file       source config file\n"
	   "\n"
	   "  -v            level of verbosity, can be set multiple times\n"
	   "  -h            show this help message and exit\n"
	   "\n" "See zs-copy(1) for more information\n", program_name);
}

static int
downloadobj(struct sourceopt *sourceopt, struct ftp *ftp,
	    struct object *obj)
{
    int             rc;
    int             i;
    int             n;
    int             y;
    int             dltries;
    char           *lib,
                   *type;

    char            remotename[PATH_MAX];
    char            localname[PATH_MAX];
    int             destfd;
    char            buf[BUFSIZ];
    int             len;

    rc = ftp_cmd(ftp, "RCMD CRTSAVF FILE(QTEMP/ZS)\r\n");
    if (ftp_dfthandle(ftp, rc, 250) == -1) {
	print_error("failed to create save file: %s\n", ftp_strerror(ftp));
	return 1;
    }

    for (i = 0, n = 0; i < Z_LIBLMAX; i++) {
	for (y = 0; y < Z_TYPEMAX; y++, n++) {
	    lib = *obj->lib ? obj->lib : sourceopt->libl[i];
	    type = *obj->type ? obj->type : sourceopt->types[y];

	    if (*lib == '\0' || *type == '\0')
		break;

	    /*
	     * try to save the object located in $lib
	     */
	    rc = ftp_cmd(ftp,
			 "RCMD SAVOBJ OBJ(%s) OBJTYPE(%s) LIB(%s) TGTRLS(%s) DEV(*SAVF) SAVF(QTEMP/ZS) DTACPR(*HIGH)\r\n",
			 obj->obj, type, lib, sourceopt->release);
	    while (rc != 250 && rc != 550) {
		switch (rc) {
		case 0:
		    rc = ftp_cmdcontinue(ftp);
		    break;
		default:
		    print_error("failed to save object: %s\n",
				ftp_strerror(ftp));
		    return 1;
		}
	    }

	    /*
	     * object was copied
	     */
	    if (rc == 250)
		goto download;

	    /*
	     * don't check all provided types, (object have own)
	     */
	    if (*obj->type)
		break;
	}

	/*
	 * no more libs in libl
	 */
	if (*lib == '\0')
	    break;

	/*
	 * don't check whole libl, (object have own)
	 */
	if (*obj->lib)
	    break;
    }

    print_error("failed to save object '%s'\n", obj->obj);
    return 1;

  download:
    for (dltries = 0; dltries < 50; dltries++) {
	snprintf(remotename, sizeof(remotename), "/tmp/zs-get%d", dltries);
	rc = ftp_cmd(ftp,
		     "RCMD CPYTOSTMF FROMMBR('/QSYS.LIB/QTEMP.LIB/ZS.FILE') TOSTMF('%s')\r\n",
		     remotename);
	if (ftp_dfthandle(ftp, rc, 250) == 0)
	    break;
    }
    if (dltries == 50) {
	print_error("failed to copy to stream file: %s\n",
		    ftp_strerror(ftp));
	return 1;
    }

    rc = ftp_cmd(ftp, "RCMD DLTF FILE(QTEMP/ZS)\r\n");
    if (ftp_dfthandle(ftp, rc, 250) == -1) {
	print_error("failed to remove savf: %s\n", ftp_strerror(ftp));
	return 1;
    }

    strcpy(localname, "/tmp/zs-XXXXXX");
    destfd = mkstemp(localname);
    if (destfd == -1) {
	print_error("failed to create output file: %s\n", strerror(errno));
	return 1;
    }
    close(destfd);

    if (ftp_get(ftp, localname, remotename) != 0) {
	unlink(localname);
	print_error("failed to get file: %s\n", ftp_strerror(ftp));
	return 1;
    }

    rc = ftp_cmd(ftp, "DELETE %s\n", remotename);
    if (ftp_dfthandle(ftp, rc, 250) == -1) {
	print_error("failed to remove tempfile: %s\n", ftp_strerror(ftp));
	return 1;
    }

    len = snprintf(buf, sizeof(buf), "%s:%s\n", lib, localname);
    if (write(sourceopt->pipe, buf, len) == -1) {
	print_error("failed to write to target process\n");
	return 1;
    }

    return 0;
}

static int
uploadfile(struct targetopt *targetopt, struct ftp *ftp,
	   char *lib, char *localname)
{
    char            remotename[PATH_MAX];
    struct ftpansbuf ftpans;
    int             rstcnt;
    int             rc;

    /*
     * ftp_put can change remotename
     */
    strcpy(remotename, "/tmp/zs-put");

    if (ftp_put(ftp, localname, remotename) != 0) {
	unlink(localname);
	print_error("failed to put file: %s\n", ftp_strerror(ftp));
	return 1;
    }
    unlink(localname);

    rc = ftp_cmd(ftp,
		 "RCMD CPYFRMSTMF FROMSTMF('%s') TOMBR('/QSYS.LIB/QTEMP.LIB/ZS.FILE')\r\n",
		 remotename);
    if (ftp_dfthandle(ftp, rc, 250) == -1) {
	print_error("failed to copy from stream file: %s\n",
		    ftp_strerror(ftp));
	return 1;
    }

    rc = ftp_cmd(ftp, "DELETE %s\n", remotename);
    if (ftp_dfthandle(ftp, rc, 250) == -1) {
	print_error("failed to remove tempfile: %s\n", ftp_strerror(ftp));
	return 1;
    }

    memset(&ftpans, 0, sizeof(struct ftpansbuf));
    rc = ftp_cmd_r(ftp, &ftpans,
		   "RCMD RSTOBJ OBJ(*ALL) SAVLIB(%s) DEV(*SAVF) SAVF(QTEMP/ZS) MBROPT(*ALL) RSTLIB(%s)\r\n",
		   lib, targetopt->lib);
    if (ftp_dfthandle_r(ftp, &ftpans, rc, 250) == -1) {
	/*
	 * reply = 550 but the object is still still restored, this can
	 * be due to authentication on the object, etc.
	 */
	if (ftpans.reply != 550
	    || sscanf(ftpans.buffer, "%d objects restored.",
		      &rstcnt) != 1 || rstcnt != 1) {
	    print_error("failed to restore object: %s", ftpans.buffer);
	    return 1;
	}
    }

    rc = ftp_cmd(ftp, "RCMD DLTF FILE(QTEMP/ZS)\r\n");
    if (ftp_dfthandle(ftp, rc, 250) == -1) {
	print_error("failed to remove savf: %s\n", ftp_strerror(ftp));
	return 1;
    }

    return 0;
}

static int
sourcemain(struct sourceopt *sourceopt, struct ftp *ftp)
{
    struct object  *obj;
    int             i;

    for (i = 0; i < Z_OBJMAX; i++) {
	obj = &(sourceopt->objects[i]);
	if (*obj->obj == '\0')
	    break;

	if (downloadobj(sourceopt, ftp, obj) != 0)
	    return 1;
    }

    return 0;
}

static int
targetmain(struct targetopt *targetopt, struct ftp *ftp)
{
    int             returncode;
    int             fd;
    FILE           *fp;
    char           *line;
    char           *lib;
    char           *localname;
    char           *saveptr;
    size_t          linesiz;

    returncode = 0;

    fd = dup(targetopt->pipe);
    if (fd == -1) {
	print_error("failed to dup pipe: %s\n", strerror(errno));
	return 1;
    }

    fp = fdopen(fd, "r");
    line = NULL;
    linesiz = 0;

    while (getline(&line, &linesiz, fp) > 0) {
	lib = strtok_r(line, ":", &saveptr);
	localname = strtok_r(NULL, "\n", &saveptr);

	if (lib == NULL || localname == NULL) {
	    print_error("failed to understand payload\n");
	    returncode = 1;
	    goto exit;
	}

	if (uploadfile(targetopt, ftp, lib, localname) != 0) {
	    returncode = 1;
	    goto exit;
	}
    }

  exit:free(line);
    fclose(fp);

    return returncode;
}

int
main_copy(int argc, char **argv)
{
    int             c;
    int             rc;
    int             exit_status;
    int             argind;
    int             i;
    int             childrc;
    int             pipefd[2];
    struct ftp      sourceftp;
    struct ftp      targetftp;
    struct sourceopt sourceopt;
    struct targetopt targetopt;

    ftp_init(&sourceftp);
    ftp_init(&targetftp);

    memset(&sourceopt, 0, sizeof(sourceopt));
    memset(&targetopt, 0, sizeof(targetopt));

    while ((c = getopt(argc, argv,
		       "hvs:u:p:l:t:m:r:c:S:U:P:L:M:C:")) != -1) {
	switch (c) {
	case 'h':		/* help */
	    print_help();
	    return 0;
	case 'v':		/* verbosity */
	    ftp_set_variable(&sourceftp, FTP_VAR_VERBOSE, "+1");
	    ftp_set_variable(&targetftp, FTP_VAR_VERBOSE, "+1");
	    break;
	case 's':		/* source host */
	    ftp_set_variable(&sourceftp, FTP_VAR_HOST, optarg);
	    break;
	case 'u':		/* source user */
	    ftp_set_variable(&sourceftp, FTP_VAR_USER, optarg);
	    break;
	case 'p':		/* source port */
	    ftp_set_variable(&sourceftp, FTP_VAR_PORT, optarg);
	    break;
	case 'l':		/* source libl */
	    rc = util_parselibl(&sourceopt, optarg);
	    if (rc != 0)
		print_error("failed to parse library list: %s\n",
			    util_strerror(rc));
	    break;
	case 't':		/* source types */
	    rc = util_parsetypes(&sourceopt, optarg);
	    if (rc != 0)
		print_error("failed to parse types: %s\n",
			    util_strerror(rc));
	    break;
	case 'm':		/* source max tries */
	    ftp_set_variable(&sourceftp, FTP_VAR_MAXTRIES, optarg);
	    break;
	case 'r':		/* source release version */
	    strncpy(sourceopt.release, optarg, Z_RLSSIZ);
	    sourceopt.release[Z_RLSSIZ - 1] = '\0';
	    break;
	case 'c':		/* source config */
	    rc = util_parsecfg(&sourceftp, optarg);
	    if (rc != 0)
		print_error("failed to parse config file: %s\n",
			    util_strerror(rc));
	    break;
	case 'S':		/* target host */
	    ftp_set_variable(&targetftp, FTP_VAR_HOST, optarg);
	    break;
	case 'U':		/* target user */
	    ftp_set_variable(&targetftp, FTP_VAR_USER, optarg);
	    break;
	case 'P':		/* target port */
	    ftp_set_variable(&targetftp, FTP_VAR_PORT, optarg);
	    break;
	case 'L':		/* target lib */
	    strncpy(targetopt.lib, optarg, Z_LIBSIZ);
	    targetopt.lib[Z_LIBSIZ - 1] = '\0';
	    break;
	case 'M':		/* target max tries */
	    ftp_set_variable(&targetftp, FTP_VAR_MAXTRIES, optarg);
	    break;
	case 'C':		/* target config */
	    rc = util_parsecfg(&targetftp, optarg);
	    if (rc != 0)
		print_error("failed to parse config file: %s\n",
			    util_strerror(rc));
	    break;
	default:
	    return 2;
	}
    }

    /*
     * slurp objects
     */
    for (argind = optind, i = 0; argind < argc; argind++, i++) {
	if (i == Z_OBJMAX) {
	    print_error("maximum of %d objects reached\n", Z_OBJMAX);
	    break;
	}
	rc = util_parseobj(&sourceopt.objects[i], argv[argind]);
	if (rc != 0)
	    print_error("failed to parse object: %s\n", util_strerror(rc));
    }

    if (*sourceopt.objects[0].obj == '\0') {
	print_error("missing object\n");
	return 2;
    }

    if (pipe(pipefd) != 0) {
	print_error("failed to create pipe: %s\n", strerror(errno));
	return 1;
    }

    if (ftp_connect(&sourceftp) == -1) {
	print_error("failed to connect to source: %s\n",
		    ftp_strerror(&sourceftp));
	return 1;
    }

    if (ftp_connect(&targetftp) == -1) {
	print_error("failed to connect to target: %s\n",
		    ftp_strerror(&targetftp));
	return 1;
    }

    /*
     * try to guess target release if none is specified
     */
    if (*sourceopt.release == '\0') {
	util_guessrelease(sourceopt.release, &sourceftp, &targetftp);
    }
    /*
     * ... fallback to *CURRENT
     */
    if (*sourceopt.release == '\0') {
	strcpy(sourceopt.release, "*CURRENT");
    }

    /*
     * if no types are specified then fallback to *ALL
     */
    if (*sourceopt.types[0] == '\0') {
	strcpy(sourceopt.types[0], "*ALL");
    }

    switch (fork()) {
    case -1:
	print_error("failed to fork: %s\n", strerror(errno));
	return 1;
    case 0:
	targetopt.pipe = pipefd[0];
	close(pipefd[1]);
	exit_status = targetmain(&targetopt, &targetftp);

	close(targetopt.pipe);
	break;
    default:
	sourceopt.pipe = pipefd[1];
	close(pipefd[0]);
	exit_status = sourcemain(&sourceopt, &sourceftp);

	/*
	 * cleanup
	 */
	close(sourceopt.pipe);
	wait(&childrc);
	if (WIFEXITED(childrc)) {
	    if (exit_status == 0) {
		exit_status = WEXITSTATUS(childrc);
	    }
	} else {
	    exit_status = 1;
	}
	break;
    }

    ftp_close(&sourceftp);
    ftp_close(&targetftp);
    return exit_status;
}
