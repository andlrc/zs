#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>

char *program_name;
#define PROGRAM_VERSION	"1.1"

#include "ftp.h"
#include "zs.h"
#include "util.h"

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
	       "  -l libl       set source library list\n"
	       "                comma separated list of libraries\n"
	       "  -t types      set type list\n"
	       "                comma separated list of types\n"
	       "  -m tries      set maximum tries for source to respond\n"
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
	       "  -V            output version information and exit\n"
	       "  -h            show this help message and exit\n"
	       "\n"
	       "See zs(1) for more information\n",
	       program_name);
}

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

static int downloadobj(struct sourceopt *sourceopt, struct ftp *ftp,
		       struct object *obj)
{
	int rc;
	int i, n, y;
	char *lib, *type;

	char remotename[PATH_MAX];
	char localname[PATH_MAX];
	int destfd;
	char buf[BUFSIZ];
	int len;

	rc = ftp_cmd(ftp, "RCMD CRTSAVF FILE(QTEMP/ZS)\r\n");
	if (ftp_dfthandle(ftp, rc, 250) == -1) {
		print_error("failed to create save file: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	for (i = 0, n = 0; i < Z_LIBLMAX; i++) {
		for (y = 0; y < Z_TYPEMAX; y++, n++) {
			lib = *obj->lib ? obj->lib : sourceopt->libl[i];
			type = *obj->type ? obj->type : sourceopt->types[y];

			if (*lib == '\0')
				break;

			if (*type == '\0') {
				if (y == 0) {
					type = "ALL";
				} else {
					break;
				}
			}

			/* try to save the object located in $lib */
			rc = ftp_cmd(ftp, "RCMD SAVOBJ OBJ(%s) OBJTYPE(*%s) LIB(%s) DEV(*SAVF) SAVF(QTEMP/ZS) DTACPR(*HIGH)\r\n",
				     obj->obj, type, lib);
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

			/* object was copied */
			if (rc == 250)
				goto upload;

			/* don't check all provided types, (object have own) */
			if (*obj->type)
				break;
		}

		/* no more libs in libl */
		if (*lib == '\0')
			break;

		/* don't check whole libl, (object have own) */
		if (*obj->lib)
			break;
	}

	print_error("object %s not found\n", obj->obj);
	return 1;

      upload:
	/* TODO: figure out a way to guarentee an unique file on the server */
	snprintf(remotename, sizeof(remotename), "/tmp/zs-%d-get.savf",
		 getpid());
	rc = ftp_cmd(ftp, "RCMD CPYTOSTMF FROMMBR('/QSYS.LIB/QTEMP.LIB/ZS.FILE') TOSTMF('%s') STMFOPT(*REPLACE)\r\n",
		     remotename);
	if (ftp_dfthandle(ftp, rc, 250) == -1) {
		print_error("failed to copy to stream file: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	rc = ftp_cmd(ftp, "RCMD DLTF FILE(QTEMP/ZS)\r\n");
	if (ftp_dfthandle(ftp, rc, 250) == -1) {
		print_error("failed to remove savf: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	strcpy(localname, "/tmp/zs-XXXXXX");
	destfd = mkstemp(localname);
	if (destfd == -1) {
		print_error("failed to create output file: %s\n",
			    strerror(errno));
		return 1;
	}
	close(destfd);

	printf("downloading %s/%s*%s to %s\n", lib, obj->obj, obj->type,
	       localname);

	if (ftp_get(ftp, localname, remotename) != 0) {
		unlink(localname);
		print_error("failed to get file: %s\n", ftp_strerror(ftp));
		return 1;
	}

	rc = ftp_cmd(ftp, "DELETE %s\n", remotename);
	if (ftp_dfthandle(ftp, rc, 250) == -1) {
		print_error("failed to remove tempfile: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	len = snprintf(buf, sizeof(buf), "%s:%s\n", lib, localname);
	if (write(sourceopt->pipe, buf, len) == -1) {
		print_error("failed to write to target process\n");
		return 1;
	}

	return 0;
}

static int uploadfile(struct targetopt *targetopt, struct ftp *ftp,
		      char *lib, char *localname)
{
	char remotename[PATH_MAX];
	int rc;

	/* TODO: figure out a way to guarentee an unique file on the server */
	snprintf(remotename, sizeof(remotename), "/tmp/zs-%d-put.savf",
		 getpid());

	printf("uploading %s to %s\n", localname, remotename);

	if (ftp_put(ftp, localname, remotename) != 0) {
		unlink(localname);
		print_error("failed to put file: %s\n", ftp_strerror(ftp));
		return 1;
	}
	unlink(localname);

	rc = ftp_cmd(ftp, "RCMD CPYFRMSTMF FROMSTMF('%s') TOMBR('/QSYS.LIB/QTEMP.LIB/ZS.FILE')\r\n",
		     remotename);
	if (ftp_dfthandle(ftp, rc, 250) == -1) {
		print_error("failed to copy from stream file: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	rc = ftp_cmd(ftp, "DELETE %s\n", remotename);
	if (ftp_dfthandle(ftp, rc, 250) == -1) {
		print_error("failed to remove tempfile: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	rc = ftp_cmd(ftp, "RCMD RSTOBJ OBJ(*ALL) SAVLIB(%s) DEV(*SAVF) SAVF(QTEMP/ZS) MBROPT(*ALL) RSTLIB(%s)\r\n",
		     lib, targetopt->lib);
	if (ftp_dfthandle(ftp, rc, 250) == -1) {
		print_error("failed to restore object: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	rc = ftp_cmd(ftp, "RCMD DLTF FILE(QTEMP/ZS)\r\n");
	if (ftp_dfthandle(ftp, rc, 250) == -1) {
		print_error("failed to remove savf: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	return 0;
}

static int sourcemain(struct sourceopt *sourceopt, struct ftp *ftp)
{
	struct object *obj;
	int i;

	if (ftp_connect(ftp) == -1) {
		print_error("failed to connect to source: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	for (i = 0; i < Z_OBJMAX; i++) {
		obj = &(sourceopt->objects[i]);
		if (*obj->obj == '\0')
			break;

		if (downloadobj(sourceopt, ftp, obj) != 0)
			return 1;
	}

	return 0;
}

static int targetmain(struct targetopt *targetopt, struct ftp *ftp)
{
	int returncode;
	int fd;
	FILE *fp;
	char *line;
	char *lib, *localname;
	char *saveptr;
	size_t linesiz;

	returncode = 0;

	if (ftp_connect(ftp) == -1) {
		print_error("failed to connect to target: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	fd = dup(targetopt->pipe);
	if (fd == -1) {
		print_error("failed to dup pipe: %s\n", strerror(errno));
		return 1;
	}

	fp = fdopen(fd, "r");
	line = NULL;
	linesiz = 0;

	while (getline(&line, &linesiz, fp) > 0) {
		/* no more objects */
		if (*line == '\n') {
			break;
		}

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

exit:	free(line);
	fclose(fp);

	return returncode;
}

int main(int argc, char **argv)
{
	int c, rc, argind, i;
	int childrc, pipefd[2];

	struct sourceopt sourceopt;
	struct targetopt targetopt;

	struct ftp sourceftp;
	struct ftp targetftp;

	program_name = strrchr(argv[0], '/');
	if (program_name)
		program_name++;
	else
		program_name = argv[0];

	ftp_init(&sourceftp);
	ftp_init(&targetftp);

	memset(&sourceopt, 0, sizeof(sourceopt));
	memset(&targetopt, 0, sizeof(targetopt));

	while ((c = getopt(argc, argv, "Vhvs:u:p:l:t:m:c:S:U:P:L:M:C:")) != -1) {
		switch (c) {
		case 'V':	/* version */
			print_version();
			return 0;
		case 'h':	/* help */
			print_help();
			return 0;
		case 'v':	/* verbosity */
			ftp_set_variable(&sourceftp, FTP_VAR_VERBOSE, "+1");
			ftp_set_variable(&targetftp, FTP_VAR_VERBOSE, "+1");
			break;
		case 's':	/* source host */
			ftp_set_variable(&sourceftp, FTP_VAR_HOST, optarg);
			break;
		case 'u':	/* source user */
			ftp_set_variable(&sourceftp, FTP_VAR_USER, optarg);
			break;
		case 'p':	/* source port */
			ftp_set_variable(&sourceftp, FTP_VAR_PORT, optarg);
			break;
		case 'l':	/* source libl */
			rc = util_parselibl(&sourceopt, optarg);
			if (rc != 0)
				print_error("failed to parse library list: %s\n",
					    util_strerror(rc));
			break;
		case 't':	/* source types */
			rc = util_parsetypes(&sourceopt, optarg);
			if (rc != 0)
				print_error("failed to parse types: %s\n",
					    util_strerror(rc));
			break;
		case 'm':	/* source max tries */
			ftp_set_variable(&sourceftp, FTP_VAR_MAXTRIES, optarg);
			break;
		case 'c':	/* source config */
			rc = util_parsecfg(&sourceftp, optarg);
			if (rc != 0)
				print_error("failed to parse config file: %s\n",
					    util_strerror(rc));
			break;
		case 'S':	/* target host */
			ftp_set_variable(&targetftp, FTP_VAR_HOST, optarg);
			break;
		case 'U':	/* target user */
			ftp_set_variable(&targetftp, FTP_VAR_USER, optarg);
			break;
		case 'P':	/* target port */
			ftp_set_variable(&targetftp, FTP_VAR_PORT, optarg);
			break;
		case 'L':	/* target lib */
			strncpy(targetopt.lib, optarg, Z_LIBSIZ);
			targetopt.lib[Z_LIBSIZ - 1] = '\0';
			break;
		case 'M':	/* target max tries */
			ftp_set_variable(&targetftp, FTP_VAR_MAXTRIES, optarg);
			break;
		case 'C':	/* target config */
			rc = util_parsecfg(&targetftp, optarg);
			if (rc != 0)
				print_error("failed to parse config file: %s\n",
					    util_strerror(rc));
			break;
		default:
			return 2;
		}
	}

	/* slurp objects */
	for (argind = optind, i = 0; argind < argc; argind++, i++) {
		if (i == Z_OBJMAX) {
			print_error("maximum of %d objects reached\n",
				    Z_OBJMAX);
			break;
		}
		rc = util_parseobj(&sourceopt.objects[i], argv[argind]);
		if (rc != 0)
			print_error("failed to parse object: %s\n",
				    util_strerror(rc));
	}

	if (*sourceopt.objects[0].obj == '\0') {
		print_error("missing an object\n");
		return 2;
	}

	if (pipe(pipefd) != 0) {
		print_error("failed to create pipe: %s\n",
			    strerror(errno));
		return 1;
	}

	switch (fork()) {
	case -1:
		print_error("failed to fork: %s\n", strerror(errno));
		return 1;
	case 0:
		targetopt.pipe = pipefd[0];
		close(pipefd[1]);
		rc = targetmain(&targetopt, &targetftp);
		close(sourceopt.pipe);
		ftp_close(&targetftp);
		return rc;
		break;
	default:
		sourceopt.pipe = pipefd[1];
		close(pipefd[0]);
		rc = sourcemain(&sourceopt, &sourceftp);

		/* tell child that everything is done */
		write(sourceopt.pipe, "\n", 1);

		/* cleanup */
		close(sourceopt.pipe);
		ftp_close(&sourceftp);
		wait(&childrc);
		if (WIFEXITED(childrc)) {
			if (rc == 0) {
				rc = WEXITSTATUS(childrc);
			}
		} else {
			rc = 1;
		}
		return rc;
		break;
	}
}
