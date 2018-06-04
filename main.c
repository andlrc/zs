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

#include "zs.h"
#include "util.h"
#include "ftp.h"

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

static int sourcemain(struct sourceopt *sourceopt, struct ftp *ftp)
{
	int rc;
	struct object *obj;
	char *lib;
	char remotename[PATH_MAX];
	char localname[PATH_MAX];
	int destfd;
	char buf[BUFSIZ];
	int len;

	if (ftp_connect(ftp, sourceopt->server.host, sourceopt->server.port, sourceopt->server.user, sourceopt->server.password) == -1) {
		print_error("failed to connect to source: %s\n", ftp_strerror(ftp));
		return 1;
	}

	for (int i = 0; i < Z_OBJMAX; i++) {
		obj = &(sourceopt->objects[i]);
		if (*obj->obj == '\0')
			break;

		rc = ftp_cmd(ftp, "RCMD CRTSAVF FILE(QTEMP/ZS%d)\r\n", i);
		while (rc != 250) {
			switch (rc) {
			case 0:
				rc = ftp_cmdcontinue(ftp);
				break;
			case -1:
				if (ftp->errnum == EFTP_NOREPLY) {
					rc = ftp_cmdcontinue(ftp);
				} else {
					print_error("failed to create save file: %s\n", ftp_strerror(ftp));
					return 1;
				}
				break;
			default:
				print_error("unknown reply code: %d\n", rc);
				return 1;
			}
		}

		if (*obj->lib) {
			lib = obj->lib;
			rc = ftp_cmd(ftp, "RCMD SAVOBJ OBJ(%s) OBJTYPE(*%s) LIB(%s) DEV(*SAVF) SAVF(QTEMP/ZS%d) DTACPR(*HIGH)\r\n",
				     obj->obj, obj->type, obj->lib, i);
			while (rc != 250) {
				switch (rc) {
				case 0:
					rc = ftp_cmdcontinue(ftp);
					break;
				case -1:
					if (ftp->errnum == EFTP_NOREPLY) {
						rc = ftp_cmdcontinue(ftp);
					} else {
						print_error("failed to save object: %s\n", ftp_strerror(ftp));
						return 1;
					}
					break;
				default:
					print_error("unknown reply code: %d\n", rc);
					return 1;
				}
			}
		} else {
			for (int y = 0; y < Z_LIBLMAX; y++) {
				lib = sourceopt->libl[y];
				/* no more libraries on the list to copy the
				 * object from */
				if (*lib == '\0') {
					print_error("object %s not found in library list\n", obj->obj);
					return 1;
				}
				rc = ftp_cmd(ftp, "RCMD SAVOBJ OBJ(%s) OBJTYPE(*%s) LIB(%s) DEV(*SAVF) SAVF(QTEMP/ZS%d) DTACPR(*HIGH)\r\n",
					     obj->obj, obj->type, lib, i);
				while (rc != 250 && rc != 550) {
					switch (rc) {
					case 0:
						rc = ftp_cmdcontinue(ftp);
						break;
					case -1:
						if (ftp->errnum == EFTP_NOREPLY) {
							rc = ftp_cmdcontinue(ftp);
						} else {
							print_error("failed to save object: %s\n", ftp_strerror(ftp));
							return 1;
						}
						break;
					default:
						print_error("unknown reply code: %d\n", rc);
						return 1;
					}
				}
				/* object was copied */
				if (rc == 250)
					break;
			}
		}

		snprintf(remotename, sizeof(remotename), "/tmp/zs%d.savf", i);
		rc = ftp_cmd(ftp, "RCMD CPYTOSTMF FROMMBR('/QSYS.LIB/QTEMP.LIB/ZS%d.FILE') TOSTMF('%s') STMFOPT(*REPLACE)\r\n",
			     i, remotename);
		while (rc != 250) {
			switch (rc) {
			case 0:
				rc = ftp_cmdcontinue(ftp);
				break;
			case -1:
				if (ftp->errnum == EFTP_NOREPLY) {
					rc = ftp_cmdcontinue(ftp);
				} else {
					print_error("failed to copy to stream file: %s\n", ftp_strerror(ftp));
					return 1;
				}
				break;
			default:
				print_error("unknown reply code: %d\n", rc);
				return 1;
			}
		}

		strcpy(localname, "/tmp/zs-XXXXXX");
		destfd = mkstemp(localname);
		if (destfd == -1) {
			print_error("failed to create output file: %s\n", strerror(errno));
			return 1;
		}
		close(destfd);

		printf("downloading %s/%s*%s to %s\n", lib, obj->obj, obj->type, localname);

		if (ftp_get(ftp, localname, remotename) != 0) {
			unlink(localname);
			print_error("failed to get file: %s\n", ftp_strerror(ftp));
			return 1;
		}

		len = snprintf(buf, sizeof(buf), "%s:%s\n", lib, localname);
		if (write(sourceopt->pipe, buf, len) == -1) {
			print_error("failed to write to target process\n");
			return 1;
		}
	}

	return 0;
}

static int targetmain(struct targetopt *targetopt, struct ftp *ftp)
{
	FILE *fp;
	char *line;
	char *lib, *localname;
	char *saveptr;
	char remotename[PATH_MAX];
	size_t linesiz;
	int rc;

	if (ftp_connect(ftp, targetopt->server.host, targetopt->server.port, targetopt->server.user, targetopt->server.password) == -1) {
		print_error("failed to connect to target: %s\n", ftp_strerror(ftp));
		return 1;
	}

	fp = fdopen(targetopt->pipe, "r");
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
			return 1;
		}

		snprintf(remotename, sizeof(remotename), "/tmp/zs%d.savf", i);

		printf("uploading %s to %s\n", localname, remotename);

		if (ftp_put(ftp, localname, remotename) != 0) {
			unlink(localname);
			print_error("failed to put file: %s\n", ftp_strerror(ftp));
			return 1;
		}

		rc = ftp_cmd(ftp, "RCMD CPYFRMSTMF FROMSTMF('%s') TOMBR('/QSYS.LIB/QTEMP.LIB/ZS%d.FILE')\r\n",
			     remotename, i);
		while (rc != 250) {
			switch (rc) {
			case 0:
				rc = ftp_cmdcontinue(ftp);
				break;
			case -1:
				if (ftp->errnum == EFTP_NOREPLY) {
					rc = ftp_cmdcontinue(ftp);
				} else {
					print_error("failed to copy from stream file: %s\n", ftp_strerror(ftp));
					return 1;
				}
				break;
			default:
				print_error("unknown reply code: %d\n", rc);
				return 1;
			}
		}

		rc = ftp_cmd(ftp, "RCMD RSTOBJ OBJ(*ALL) SAVLIB(%s) DEV(*SAVF) SAVF(QTEMP/ZS%d) MBROPT(*ALL) RSTLIB(%s)\r\n",
			     lib, i, targetopt->lib);
		while (rc != 250) {
			switch (rc) {
			case 0:
				rc = ftp_cmdcontinue(ftp);
				break;
			case -1:
				if (ftp->errnum == EFTP_NOREPLY) {
					rc = ftp_cmdcontinue(ftp);
				} else {
					print_error("failed to restore object: %s\n", ftp_strerror(ftp));
					return 1;
				}
				break;
			default:
				print_error("unknown reply code: %d\n", rc);
				return 1;
			}
		}

		unlink(line);
	}

	free(line);
	ftp_close(ftp);

	return 0;
}

int main(int argc, char **argv)
{
	int c, rc;

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
			sourceftp.verbosity++;
			targetftp.verbosity++;
			break;
		case 's':
			strncpy(sourceopt.server.host, optarg, Z_HSTSIZ);
			sourceopt.server.host[Z_HSTSIZ - 1] = '\0';
			break;
		case 'u':
			strncpy(sourceopt.server.user, optarg, Z_USRSIZ);
			sourceopt.server.user[Z_USRSIZ - 1] = '\0';
			break;
		case 'p':
			sourceopt.server.port = atoi(optarg);
			break;
		case 'l':
			rc = util_parselibl(&sourceopt, optarg);
			if (rc != 0)
				print_error("failed to parse library list: %s\n", util_strerror(rc));
			break;
		case 'c':
			rc = util_parsecfg(&sourceopt.server, optarg);
			if (rc != 0)
				print_error("failed to parse config file: %s\n", util_strerror(rc));
			break;
		case 'S':
			strncpy(targetopt.server.host, optarg, Z_HSTSIZ);
			targetopt.server.host[Z_HSTSIZ - 1] = '\0';
			break;
		case 'U':
			strncpy(targetopt.server.user, optarg, Z_USRSIZ);
			targetopt.server.user[Z_USRSIZ - 1] = '\0';
			break;
		case 'P':
			targetopt.server.port = atoi(optarg);
			break;
		case 'L':
			strncpy(targetopt.lib, optarg, Z_LIBSIZ);
			targetopt.lib[Z_LIBSIZ - 1] = '\0';
			break;
		case 'C':
			rc = util_parsecfg(&targetopt.server, optarg);
			if (rc != 0)
				print_error("failed to parse config file: %s\n", util_strerror(rc));
			break;
		default:
			return 2;
		}
	}

	if (*sourceopt.server.host == '\0') {
		print_error("missing source host (-s)\n");
		return 2;
	}

	if (sourceopt.server.port == 0)
		sourceopt.server.port = FTP_PORT;

	if (*targetopt.server.host == '\0') {
		print_error("missing target host (-S)\n");
		return 2;
	}

	if (targetopt.server.port == 0)
		targetopt.server.port = FTP_PORT;

	if (*targetopt.lib == '\0') {
		print_error("missing target library (-L)\n");
		return 2;
	}

	if (optind == argc) {
		print_error("missing an object\n");
		return 2;
	}

	/* slurp objects */
	for (int i = optind, y = 0; i < argc; i++) {
		rc = util_parseobj(&sourceopt.objects[y++], argv[i]);
		if (rc != 0)
			print_error("failed to parse object: %s\n", util_strerror(rc));
		if (y == Z_OBJMAX) {
			print_error("maximum of %d objects reached\n", Z_OBJMAX);
		}
	}

	int pipefd[2];
	if (pipe(pipefd) != 0) {
		print_error("failed to create pipe: %s\n", strerror(errno));
		return 1;
	}

	switch(fork()) {
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
		wait(NULL);
		return rc;
		break;
	}
}
