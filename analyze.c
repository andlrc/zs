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


/*
 * Pseudo Radix tree:
 *             _____|_____
 *             |          |
 *            CSM     NAVUSRET00
 *        _____|_____
 *        |         |
 *       HDR       STG
 *   _____|_____    |____
 *   |         |   ET0   |
 * ET00      RL01 __|__ RL00
 *                |   |
 *                0   1
 */

struct radix_edges {
	struct radix_entry **list;
#define INIT_EDGE_SIZE	8
	unsigned int size;
	unsigned int length;
};

struct radix_entry {
	char *name;
	struct radix_edges edges;
};

/*
 * Example tree:
 * {
 *   name => "",
 *   edges => [
 *     {
 *       name => "CSM",
 *       edges => [
 *         {
 *           name => "HDR",
 *           edges => [
 *             {
 *               name => "ET00",
 *               edges => null
 *             },
 *             {
 *               name => "RL01",
 *               edges => null
 *             }
 *           ]
 *         },
 *         {
 *           name => "STG",
 *           edges => [
 *             {
 *               name => "ET0",
 *               edges => [
 *                 {
 *                   name => "0",
 *                   edges => null
 *                 },
 *                 {
 *                   name => "1",
 *                   edges => null
 *                 }
 *               ]
 *             },
 *             {
 *               name => "RL00",
 *               edges => null
 *             }
 *           ]
 *         }
 *       ]
 *     },
 *     {
 *       name => "NAVUSRET00",
 *       edges => null
 *     }
 *   ]
 * }
 */

static struct radix_entry tree_root;

static struct radix_entry *create_edge(struct radix_entry *tnode, char *key)
{
	unsigned int size;
	struct radix_entry *p, *enode;

	if (tnode->edges.length == 0) {
		tnode->edges.size = INIT_EDGE_SIZE;
		tnode->edges.length = 0;
		tnode->edges.list = malloc(sizeof(struct radix_entry *) * tnode->edges.size);
	} else if (tnode->edges.length == tnode->edges.size) {
		size = tnode->edges.size * 2;
		p = realloc(tnode->edges.list, sizeof(struct radix_entry *) *tnode->edges.size);
		if (p == NULL) {
			return NULL;
		}
		tnode->edges.size = size;
	}

	enode = malloc(sizeof(struct radix_entry));
	memset(enode, 0, sizeof(struct radix_entry));
	enode->name = strdup(key);


	tnode->edges.list[tnode->edges.length++] = enode;

	return enode;
}

/* returns length of prefix if match is found, otherwise 0 */
static int lookup_edge(struct radix_entry *tnode,
		       struct radix_entry **enode, char *key)
{
	unsigned int i;
	char *pnam, *pkey;

	for (i = 0; i < tnode->edges.length; i++) {
		*enode = tnode->edges.list[i];
		pnam = (*enode)->name;
		if (*pnam == *key) {
			for (pkey = key; *pnam == *pkey; pnam++, pkey++) {
				if (*pkey == '\0')
					break;
			}

			/* "key" was only part of the found edge */
			if (*pnam != '\0') {
				return 0;
			}

			return pkey - key;
		}
	}

	return 0;	/* not found */
}

static int goc_edge(struct radix_entry *tnode,
		    struct radix_entry **enode, char *key)
{
	struct radix_entry *spnode;
	unsigned int i;
	char *pnam, *pkey;

	for (i = 0; i < tnode->edges.length; i++) {
		*enode = tnode->edges.list[i];
		pnam = (*enode)->name;
		if (*pnam == *key) {
			for (pkey = key; *pnam == *pkey; pnam++, pkey++) {
				if (*pkey == '\0')
					break;
			}

			/* split */
			if (*pnam != '\0') {
				spnode = malloc(sizeof(struct radix_entry));
				spnode->edges.size = INIT_EDGE_SIZE;
				spnode->edges.length = 0;
				spnode->edges.list = malloc(sizeof(struct radix_entry *) * spnode->edges.size);
				spnode->name = strndup((*enode)->name, pnam - (*enode)->name);
				tnode->edges.list[i] = spnode;

				/* create previous edge */
				spnode->edges.list[spnode->edges.length] = *enode;
				pnam = strdup(pnam);
				free(spnode->edges.list[spnode->edges.length]->name);
				spnode->edges.list[spnode->edges.length]->name = pnam;
				spnode->edges.length++;

				/* create our edge */
				*enode = create_edge(spnode, pkey);
			}

			return pkey - key;
		}
	}

	create_edge(tnode, key);
	return strlen(key);
}

static bool radix_have(char *key)
{
	char *pkey;
	struct radix_entry *tnode, *enode;
	int rc;

	tnode = &tree_root;
	pkey = key;

	while (tnode->edges.length && *pkey != '\0') {
		rc = lookup_edge(tnode, &enode, pkey);
		if (rc == 0) {	/* not found */
			return false;
		}
		tnode = enode;
		pkey += rc;
	}

	return (tnode && *pkey == '\0');
}

static bool radix_add(char *key)
{
	char *pkey;
	struct radix_entry *tnode, *enode;

	tnode = &tree_root;
	pkey = key;

	while (tnode->edges.length && *pkey != '\0') {
		pkey += goc_edge(tnode, &enode, pkey);
		tnode = enode;
	}

	if (*pkey != '\0') {
		create_edge(tnode, pkey);
	}

	return (tnode && *pkey == '\0');
}

static void print_help(void)
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
	       "\n"
	       "See zs-analyze(1) for more information\n",
	       program_name);
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

		if (!radix_have(wobj)) {
			radix_add(wobj);
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

		if (!radix_have(wobj)) {
			radix_add(wobj);
			printf("%s/%s*FILE\n", wlib, wobj);
			if (getobjects(ftp, wlib, wobj) == -1)
				return 1;
		}
	}
	close(fd);

	return 0;
}

int main_analyze(int argc, char **argv)
{
	struct ftp ftp;
	int c, argind;
	int rc;
	char lib[Z_LIBSIZ];
	char obj[Z_OBJSIZ];
	char *p;

	memset(&tree_root, 0, sizeof(struct radix_entry));
	tree_root.name = NULL;
	ftp_init(&ftp);

	while ((c = getopt(argc, argv, "hvs:u:p:m:c:")) != -1) {
		switch (c) {
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
