/*
 * zs - work with, and move objects from one AS/400 to another.
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
#include <stdbool.h>
#include <errno.h>
#include <getopt.h>

#include "ftp.h"
#include "zs.h"
#include "util.h"
#include "analyze.h"

static int      getobjects(struct ctx *ctx, struct object *obj);

static void
print_help(void)
{
    printf("Usage %s analyze [OPTION]... OBJECT...\n"
	   "Print depends and dependencies for objects\n"
	   "\n"
	   "  -s host       set source host\n"
	   "  -u user       set source user\n"
	   "  -p port       set source port\n"
	   "  -l libl       set source library list\n"
	   "                comma separated list of libraries\n"
	   "  -m tries      set maximum tries for source to respond\n"
	   "  -c file       source config file\n"
	   "\n"
	   "  -v            level of verbosity, can be set multiple times\n"
	   "  -h            show this help message and exit\n"
	   "\n" "See zs-analyze(1) for more information\n", program_name);
}

/*
 * get fd with output of cmd, returns -1 on error
 */
static int
freadcmd(struct ftp *ftp, char *cmd, char *fromfile)
{
    int             rc;
    int             fd;
    char            localname[PATH_MAX];
    char            remotename[PATH_MAX];

    rc = ftp_cmd(ftp, cmd);
    if (ftp_dfthandle(ftp, rc, 250) != 0) {
	print_error("failed to run command: %s\n", ftp_strerror(ftp));
	return -1;
    }

    /*
     * FIXME: ensure unique file on remote
     */
    snprintf(remotename, sizeof(remotename), "/tmp/zs-readcmd");
    rc = ftp_cmd(ftp,
		 "RCMD CPYTOIMPF FROMFILE(%s) TOSTMF('%s') MBROPT(*REPLACE) STMFCCSID(1208) RCDDLM(*LF) DTAFMT(*FIXED)\r\n",
		 fromfile, remotename);
    if (ftp_dfthandle(ftp, rc, 250) != 0) {
	print_error("failed to copy to import-file: %s\n",
		    ftp_strerror(ftp));
	return -1;
    }

    /*
     * create local file
     */
    strcpy(localname, "/tmp/zs-XXXXXX");
    fd = mkstemp(localname);
    if (fd == -1) {
	print_error("failed to create output file: %s\n", strerror(errno));
	return -1;
    }

    /*
     * download
     */
    if (ftp_get(ftp, localname, remotename) != 0) {
	print_error("failed to get file: %s\n", ftp_strerror(ftp));
	unlink(localname);
	close(fd);
	return -1;
    }

    /*
     * delete the "localname"-file already,
     * as it is then GC'd when "close(fd)" is called
     */
    unlink(localname);

    /*
     * delete remote
     */
    rc = ftp_cmd(ftp, "DELETE %s\n", remotename);
    if (ftp_dfthandle(ftp, rc, 250) != 0) {
	print_error("failed to remove tempfile: %s\n", ftp_strerror(ftp));
	close(fd);
	return -1;
    }

    return fd;
}

static int
processobj(struct ctx *ctx, struct object *obj)
{
    unsigned int    size;
    unsigned int    i;
    struct object  *ptab;

    /*
     * lookup
     */
    for (i = 0; i < ctx->tablen; i++) {
	if (memcmp(&(ctx->tab[i]), obj, sizeof(struct object)) == 0) {
	    return 0;		/* found */
	}
    }

    /*
     * realloc
     */
    if (ctx->tablen == ctx->tabsiz) {
	size = ctx->tabsiz * 2;
	ptab = realloc(ctx->tab, sizeof(struct object) * size);
	if (ptab == NULL) {
	    print_error("failed to re-allocate table\n");
	    return 1;
	}
	ctx->tabsiz = size;
	ctx->tab = ptab;
    }

    /*
     * add
     */
    memcpy(&(ctx->tab[ctx->tablen++]), obj, sizeof(struct object));
    printf("%s/%s%s\n", obj->lib, obj->obj, obj->type);


    /*
     * process object
     */
    if (getobjects(ctx, obj) != 0)
	return 1;

    return 0;
}

static int
getobjects(struct ctx *ctx, struct object *obj)
{
    struct dsppgmref reftab;
    int             rc;
    int             fd;
    char            cmd[BUFSIZ];
    struct object   wobj;
    char           *p;

    rc = 0;
    fd = 0;
    memset(cmd, 0, sizeof(cmd));
    memset(&(wobj), 0, sizeof(struct object));

    /*
     * DSPPGMREF
     */
    snprintf(cmd, sizeof(cmd),
	     "RCMD DSPPGMREF PGM(%s/%s) OBJTYPE(%s) OUTPUT(*OUTFILE) OUTFILE(QTEMP/REF)\r\n",
	     obj->lib, obj->obj, obj->type);

    fd = freadcmd(&ctx->ftp, cmd, "QTEMP/REF");
    if (fd == -1)
	return 1;

    rc = ftp_cmd(&ctx->ftp, "RCMD DLTF FILE(QTEMP/REF)\r\n");
    if (ftp_dfthandle(&ctx->ftp, rc, 250) == -1) {
	print_error("failed to remove DSPPGMREF file: %s\n",
		    ftp_strerror(&ctx->ftp));
	return 1;
    }

    while (read(fd, &reftab, sizeof(struct dsppgmref))
	   == sizeof(struct dsppgmref)) {

	/* dump struct */
	//printf("%*s", (int) sizeof(struct dsppgmref), (char *) &(reftab));
	//printf("\n");
	//continue;

	snprintf(wobj.lib, sizeof(wobj.lib), "%s", reftab.whlnam);
	if ((p = strchr(wobj.lib, ' ')) != NULL)
	    *p = '\0';
	snprintf(wobj.obj, sizeof(wobj.obj), "%s", reftab.whfnam);
	if ((p = strchr(wobj.obj, ' ')) != NULL)
	    *p = '\0';
	snprintf(wobj.type, sizeof(wobj.type), "%s", reftab.whotyp);
	if ((p = strchr(wobj.type, ' ')) != NULL)
	    *p = '\0';

	/* ignore */
	if (*wobj.lib == '\0' || *wobj.obj == '\0')
	    continue;

	/* ignore variable libraries */
	if (*wobj.lib == '&')
	    continue;

	/* ignore system dependencies */
	if (strcmp(wobj.lib, "QSYS") == 0)
	    continue;

	/* ignore, wrong type */
	if (strcmp(wobj.type, "*SRVPGM") != 0 && strcmp(wobj.type, "*PGM") != 0)
	    continue;

	/* follow dependencies */
	if (processobj(ctx, &wobj) != 0)
	    return 1;

    }
    close(fd);

    return 0;
}

int
main_analyze(int argc, char **argv)
{
    struct ctx      ctx;
    int             c;
    int             argind;
    int             rc;
    int             exit_code;
    struct object   obj;

    memset(&ctx, 0, sizeof(struct ctx));
    ctx.tab = malloc(sizeof(struct object) * INIT_TAB_SIZE);
    if (ctx.tab == NULL) {
	print_error("failed to allocate table\n");
	return 1;
    }
    ctx.tablen = 0;
    ctx.tabsiz = INIT_TAB_SIZE;

    ftp_init(&ctx.ftp);

    while ((c = getopt(argc, argv, "hvs:u:p:l:m:c:")) != -1) {
	switch (c) {
	case 'h':		/* help */
	    print_help();
	    return 0;
	case 'v':		/* verbosity */
	    ftp_set_variable(&ctx.ftp, FTP_VAR_VERBOSE, "+1");
	    break;
	case 's':		/* source host */
	    ftp_set_variable(&ctx.ftp, FTP_VAR_HOST, optarg);
	    break;
	case 'u':		/* source user */
	    ftp_set_variable(&ctx.ftp, FTP_VAR_USER, optarg);
	    break;
	case 'p':		/* source port */
	    ftp_set_variable(&ctx.ftp, FTP_VAR_PORT, optarg);
	    break;
	case 'l':		/* source libl */
	    rc = util_parselibl(ctx.libl, optarg);
	    if (rc != 0)
		print_error("failed to parse library list: %s\n",
			    util_strerror(rc));
	    break;
	case 'm':		/* source max tries */
	    ftp_set_variable(&ctx.ftp, FTP_VAR_MAXTRIES, optarg);
	    break;
	case 'c':		/* source config */
	    rc = util_parsecfg(&ctx.ftp, optarg);
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

    if (ftp_connect(&ctx.ftp) == -1) {
	print_error("failed to connect to server: %s\n",
		    ftp_strerror(&ctx.ftp));
	return 1;
    }

    for (int i = 0; i < Z_LIBLMAX; i++) {
	if (*ctx.libl[i] == '\0')
	    break;

	rc = ftp_cmd(&ctx.ftp, "RCMD ADDLIBLE %s\r\n", ctx.libl[i]);
	if (ftp_dfthandle(&ctx.ftp, rc, 250) == -1) {
	    print_error("failed to add %s to library list: %s\n",
			ctx.libl[i], ftp_strerror(&ctx.ftp));
	    return 1;
	}
    }

    exit_code = 0;
    // argv[argind] = library/object{*srvpgm,pgm}
    for (argind = optind; argind < argc; argind++) {
	rc = util_parseobj(&obj, argv[argind]);
	if (rc != 0) {
	    print_error("failed to parse object: %s\n", util_strerror(rc));
	}
	if (getobjects(&ctx, &obj) != 0) {
	    exit_code = 1;
	}
    }

    free(ctx.tab);
    ftp_close(&ctx.ftp);
    return exit_code;
}
