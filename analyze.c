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

static int      getobjects(struct ctx *ctx, char *lib, char *obj);

static void
print_help(void)
{
    printf("Usage %s analyze [OPTION]... OBJECT...\n"
	   "Print depends and dependencies for objects\n"
	   "\n"
	   "  -s host       set source host\n"
	   "  -u user       set source user\n"
	   "  -p port       set source port\n"
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
processobj(struct ctx *ctx, char *lib, char *obj)
{
    unsigned int    size;
    unsigned int    i;
    char          **p;

    /*
     * lookup
     */
    for (i = 0; i < ctx->tablen; i++) {
	if (strcmp(ctx->tab[i], obj) == 0)
	    return 0;		/* found */
    }

    /*
     * realloc
     */
    if (ctx->tablen == ctx->tabsiz) {
	size = ctx->tabsiz * 2;
	p = realloc(ctx->tab, sizeof(char *) * size);
	if (p == NULL) {
	    print_error("failed to re-allocate table\n");
	    return 1;
	}
	ctx->tabsiz = size;
    }

    /*
     * add
     */
    if ((ctx->tab[ctx->tablen++] = strdup(obj)) == NULL)
	return 1;
    printf("%s/%s*FILE\n", lib, obj);

    /*
     * process object
     */
    if (getobjects(ctx, lib, obj) != 0)
	return 1;

    return 0;
}

static int
getobjects(struct ctx *ctx, char *lib, char *obj)
{
    struct dspdbrtab dbrtab;
    int             rc;
    int             fd;
    char            cmd[BUFSIZ];
    char            wlib[Z_LIBSIZ];
    char            wobj[Z_OBJSIZ];
    char           *p;

    memset(&dbrtab, -1, sizeof(struct dspdbrtab));
    rc = 0;
    fd = 0;
    memset(cmd, 0, sizeof(cmd));
    memset(wlib, 0, sizeof(wlib));
    memset(wobj, 0, sizeof(wobj));
    p = NULL;

    /*
     * DSPDBR
     */
    snprintf(cmd, sizeof(cmd),
	     "RCMD DSPDBR FILE(%s/%s) OUTPUT(*OUTFILE) OUTFILE(QTEMP/DBR)\r\n",
	     lib, obj);

    fd = freadcmd(&ctx->ftp, cmd, "QTEMP/DBR");
    if (fd == -1)
	return 1;

    rc = ftp_cmd(&ctx->ftp, "RCMD DLTF FILE(QTEMP/DBR)\r\n");
    if (ftp_dfthandle(&ctx->ftp, rc, 250) == -1) {
	print_error("failed to remove DSPDBR file: %s\n",
		    ftp_strerror(&ctx->ftp));
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

	/*
	 * no more
	 */
	if (*wlib == '\0' || *wobj == '\0')
	    break;

	if (processobj(ctx, wlib, wobj) != 0)
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
    char            lib[Z_LIBSIZ];
    char            obj[Z_OBJSIZ];
    char           *p;

    ctx.tab = malloc(sizeof(char *) * INIT_TAB_SIZE);
    if (!ctx.tab) {
	print_error("failed to allocate table\n");
	return 1;
    }
    ctx.tablen = 0;
    ctx.tabsiz = INIT_TAB_SIZE;

    ftp_init(&ctx.ftp);

    while ((c = getopt(argc, argv, "hvs:u:p:m:c:")) != -1) {
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

    exit_code = 0;
    for (argind = optind; argind < argc; argind++) {
	p = strchr(argv[argind], '/');
	if (p == NULL) {
	    print_error("failed to parse object: %s\n", argv[argind]);
	    continue;
	}

	strncpy(lib, argv[argind], sizeof(lib) - 1);
	if ((ptrdiff_t) (p - argv[argind]) < (ptrdiff_t) sizeof(lib))
	    lib[p - argv[argind]] = '\0';
	else
	    lib[sizeof(lib) - 1] = '\0';
	strncpy(obj, p + 1, sizeof(obj) - 1);
	obj[sizeof(obj) - 1] = '\0';

	if (getobjects(&ctx, lib, obj) != 0) {
	    exit_code = 1;
	}
    }

    free(ctx.tab);
    ftp_close(&ctx.ftp);
    return exit_code;
}
