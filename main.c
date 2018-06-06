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
#define PROGRAM_VERSION	"1.0"

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
	       "  -s host  set source host\n"
	       "  -u user  set source user\n"
	       "  -p port  set source port\n"
	       "  -l libl  set source library list\n"
	       "           comma seperated list of libraries\n"
	       "  -c file  source config file\n"
	       "\n"
	       "  -S host  set target host\n"
	       "  -U user  set target user\n"
	       "  -P port  set target port\n"
	       "  -C file  source config file\n"
	       "  -L       set target destination library\n"
	       "\n"
	       "  -v       level of verbosity, can be set multiple times\n"
	       "  -V       output version information and exit\n"
	       "  -h       show this help message and exit\n"
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
	char *lib;
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

	for (int y = 0; y < Z_LIBLMAX; y++) {
		switch (*obj->lib) {
		case 0:	/* use library list */
			lib = sourceopt->libl[y];
			if (*lib == '\0') {
				print_error("object %s not found in library list\n",
					    obj->obj);
				return 1;
			}
			break;
		default:	/* object have own library */
			lib = obj->lib;
			if (y > 0) {
				/* failed in first iteration */
				print_error("object %s not found in library %s\n",
					    obj->obj, lib);
				return 1;
			}
			break;
		}

		/* try to save the object located in $lib */
		rc = ftp_cmd(ftp, "RCMD SAVOBJ OBJ(%s) OBJTYPE(*%s) LIB(%s) DEV(*SAVF) SAVF(QTEMP/ZS) DTACPR(*HIGH)\r\n",
			     obj->obj, obj->type, lib);
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
			break;
	}

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

	if (ftp_connect(ftp) == -1) {
		print_error("failed to connect to source: %s\n",
			    ftp_strerror(ftp));
		return 1;
	}

	for (int i = 0; i < Z_OBJMAX; i++) {
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

	for (int i = 0; getline(&line, &linesiz, fp) > 0; i++) {
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
	int c, rc, childrc;

	program_name = strrchr(argv[0], '/');
	if (program_name)
		program_name++;
	else
		program_name = argv[0];

	struct sourceopt sourceopt;
	struct targetopt targetopt;

	struct ftp sourceftp;
	struct ftp targetftp;

	ftp_init(&sourceftp);
	ftp_init(&targetftp);

	memset(&sourceopt, 0, sizeof(sourceopt));
	memset(&targetopt, 0, sizeof(targetopt));

	while ((c = getopt(argc, argv, "Vhvs:u:p:l:c:S:U:P:C:L:")) != -1) {
		switch (c) {
		case 'V':
			print_version();
			return 0;
		case 'h':
			print_help();
			return 0;
		case 'v':
			ftp_set_variable(&sourceftp, FTP_VAR_VERBOSE, "+1");
			ftp_set_variable(&targetftp, FTP_VAR_VERBOSE, "+1");
			break;
		case 's':
			ftp_set_variable(&sourceftp, FTP_VAR_HOST, optarg);
			break;
		case 'u':
			ftp_set_variable(&sourceftp, FTP_VAR_USER, optarg);
			break;
		case 'p':
			ftp_set_variable(&sourceftp, FTP_VAR_PORT, optarg);
			break;
		case 'l':
			rc = util_parselibl(&sourceopt, optarg);
			if (rc != 0)
				print_error("failed to parse library list: %s\n",
					    util_strerror(rc));
			break;
		case 'c':
			rc = util_parsecfg(&sourceftp, optarg);
			if (rc != 0)
				print_error("failed to parse config file: %s\n",
					    util_strerror(rc));
			break;
		case 'S':
			ftp_set_variable(&targetftp, FTP_VAR_HOST, optarg);
			break;
		case 'U':
			ftp_set_variable(&targetftp, FTP_VAR_USER, optarg);
			break;
		case 'P':
			ftp_set_variable(&targetftp, FTP_VAR_PORT, optarg);
			break;
		case 'L':
			strncpy(targetopt.lib, optarg, Z_LIBSIZ);
			targetopt.lib[Z_LIBSIZ - 1] = '\0';
			break;
		case 'C':
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
	for (int i = optind, y = 0; i < argc; i++) {
		rc = util_parseobj(&sourceopt.objects[y++], argv[i]);
		if (rc != 0)
			print_error("failed to parse object: %s\n",
				    util_strerror(rc));
		if (y == Z_OBJMAX) {
			print_error("maximum of %d objects reached\n",
				    Z_OBJMAX);
		}
	}

	if (*sourceopt.objects[0].obj == '\0') {
		print_error("missing an object\n");
		return 2;
	}

	int pipefd[2];
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
