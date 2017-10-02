#include "main.h"
#include "zs.h"
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

static void print_version(void)
{
	printf("%s: %s\n", PROGRAM_NAME, PROGRAM_VERSION);
}

static void print_usage(void)
{
	fprintf(stderr, PROGRAM_USAGE);
}

static void print_help(void)
{
	printf(PROGRAM_USAGE
	       "\n"
	       "Mandatory arguments to long options are mandatory for short options too.\n"
	       "  -s, --source-server    set source server\n"
	       "  -u, --source-user      set source user profile\n"
	       "  -p, --source-password  set source user password\n"
	       "  -c, --source-config    path to source config\n"
	       "  -j, --source-job-log   path to source joblog\n"
	       "\n"
	       "  -S, --target-server    set target server\n"
	       "  -U, --target-user      set target user profile\n"
	       "  -P, --target-password  set target user password\n"
	       "  -L, --target-library   set target library\n"
	       "  -C, --target-config    path to target config\n"
	       "  -J, --target-job-log   path to target joblog\n"
	       "\n"
	       "  -v, --verbose          level of verbosity\n"
	       "  -V, --version          output version information and exit\n"
	       "  -h, --help             show this help message and exit\n");
}

static struct option const long_options[] = {
	{ "verbose",		no_argument,		0,	'v' },
	{ "version",		no_argument,		0,	'V' },
	{ "help",		no_argument,		0,	'h' },

	{ "source-server",	required_argument,	0,	's' },
	{ "source-user",	required_argument,	0,	'u' },
	{ "source-password",	required_argument,	0,	'p' },
	{ "source-config",	required_argument,	0,	'c' },
	{ "source-job-log",	required_argument,	0,	'j' },

	{ "target-server",	required_argument,	0,	'S' },
	{ "target-user",	required_argument,	0,	'U' },
	{ "target-password",	required_argument,	0,	'P' },
	{ "target-library",	required_argument,	0,	'L' },
	{ "target-config",	required_argument,	0,	'C' },
	{ "target-job-log",	required_argument,	0,	'J' },
	{ 0,			0,			0,	0   }
};

static char short_options[] = "vVhs:u:p:c:j:S:U:P:L:C:J:";

int save(struct Z_server *server)
{
	char reqbuf[BUFSIZ];
	int i;
	struct Z_object *pobj;

	if (Z_connect(server) == -1) {
		perror("Z_connect()");
		goto error;
	}

	if (Z_signon(server) == -1) {
		perror("Z_signon()");
		goto error;
	}

	if (Z_cmd(server, "type I") == -1) {
		perror("Z_cmd()");
		goto error;
	}

	for (i = 0; i < server->objectlen; i++) {
		pobj = &server->objects[i];

		if (Z_system(server, CMD_SAVE1, i) == -1) {
			perror("Z_system()");
			goto error;
		}

		if (Z_system(server, CMD_SAVE2, pobj->object,
			     pobj->type, pobj->library, i) == -1) {
			perror("Z_system()");
			goto error;
		}

		if (Z_system(server, CMD_SAVE3, i, i) == -1) {
			perror("Z_system()");
			goto error;
		}

		snprintf(reqbuf, sizeof(reqbuf), "/tmp/zs%d.savf", i);
		if (Z_get(server, reqbuf, reqbuf) == -1) {
			perror("Z_get()");
			goto error;
		}
	}

	if (server->joblog && Z_joblog(server, server->joblog) == -1) {
		perror("Z_joblog()");
		return 1;
	}

	Z_quit(server);

	return 0;

      error:
	if (server->joblog && Z_joblog(server, server->joblog) == -1)
		return 1;
	return 1;

}

int restore(struct Z_server *server, struct Z_server *srcserver)
{
	char reqbuf[BUFSIZ];
	int i;
	struct Z_object *pobj;

	if (Z_connect(server) == -1) {
		perror("Z_connect()");
		return 1;
	}

	if (Z_signon(server) == -1) {
		perror("Z_signon()");
		return 1;
	}

	if (Z_cmd(server, "type I") == -1) {
		perror("Z_cmd()");
		return 1;
	}

	for (i = 0; i < srcserver->objectlen; i++) {
		pobj = &srcserver->objects[i];

		snprintf(reqbuf, sizeof(reqbuf), "/tmp/zs%d.savf", i);
		if (Z_put(server, reqbuf, reqbuf) == -1) {
			perror("Z_put()");
			return 1;
		}

		if (Z_system(server, CMD_RESTORE1, i, i) == -1) {
			perror("Z_system()");
			return 1;
		}

		if (Z_system(server, CMD_RESTORE2, pobj->library,
			     i, pobj->library) == -1) {
			perror("Z_system()");
			return 1;
		}

	}

	if (server->joblog && Z_joblog(server, server->joblog) == -1) {
		perror("Z_joblog()");
		return 1;
	}

	Z_quit(server);

	return 0;
}

int main(int argc, char **argv)
{

#define VALSERVER(__server, __member, __type)				\
	VALSERVER_MSG(__server, __member, __type, #__member);

#define VALSERVER_MSG(__server, __member, __type, __text)		\
		if (!__server.__member) {				\
			fprintf(stderr,					\
				"Missing " __type " " __text "\n");	\
			return 1;					\
		}

	int c;
	int i;
	struct Z_server srcserver;
	struct Z_server destserver;

	/* FIXME: Don't set pointers to int "0" */
	memset(&srcserver, 0, sizeof(srcserver));
	memset(&destserver, 0, sizeof(destserver));

	while ((c = getopt_long(argc, argv, short_options, long_options,
				NULL)) != -1) {
		switch (c) {
		case 'v':
			srcserver.verbose++;
			destserver.verbose++;
			break;
		case 'V':
			print_version();
			return 0;
		case 'h':
			print_help();
			return 0;
		/* Source */
		case 's':
			srcserver.server = optarg;
			break;
		case 'u':
			srcserver.user = optarg;
			break;
		case 'p':
			srcserver.password = optarg;
			break;
		case 'c':
			if (Z_cfgfile(&srcserver, optarg) == -1)
				perror("Z_cfgfile");
			break;
		case 'j':
			srcserver.joblog = optarg;
			break;
		/* Target */
		case 'S':
			destserver.server = optarg;
			break;
		case 'U':
			destserver.user = optarg;
			break;
		case 'P':
			destserver.password = optarg;
			break;
		case 'L':
			destserver.library = optarg;
			break;
		case 'C':
			if (Z_cfgfile(&destserver, optarg) == -1)
				perror("Z_cfgfile");
			break;
		case 'J':
			destserver.joblog = optarg;
			break;
		default:
			print_usage();
			return 1;
		}
	}

	for (i = optind; i < argc; i++) {
		if (Z_addobject(&srcserver, argv[i]) == -1) {
			perror("Z_addobject()");
			continue;
		}
	}

	VALSERVER(srcserver, server, "source");
	VALSERVER(srcserver, user, "source");
	VALSERVER(srcserver, password, "source");

	VALSERVER(destserver, server, "target");
	VALSERVER(destserver, user, "target");
	VALSERVER(destserver, password, "target");
	VALSERVER(destserver, library, "target");

	VALSERVER_MSG(srcserver, objectlen, "source", "object(s)");

	if (save(&srcserver))
		return 1;
	if (restore(&destserver, &srcserver))
		return 1;
	return 0;
}
