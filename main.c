/*
 * zs - work with, and move objects from one AS/400 to another.
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

char *program_name;
#define PROGRAM_VERSION	"2.13"

int main_copy(int, char **);
int main_analyze(int, char **);

static void print_version(void)
{
	printf("%s %s\n", program_name, PROGRAM_VERSION);
}

static void print_help(FILE *fp)
{
	fprintf(fp, "Usage %s { OPTION | COMMAND [OPTION]... }\n"
	       "Copy objects from one AS/400 to another\n"
	       "\n"
	       "Available subcommands are:\n"
	       "  copy     copy objects from one AS/400 to another\n"
	       "  analyze  print depends and dependencies for objects\n"
	       "\n"
	       "Available options are:\n"
	       "  -V       print version information and exit\n"
	       "  -h       show this help message and exit\n"
	       "\n"
	       "See zs(1) for more information\n",
	       program_name);
}

/* print error prefixed with "program_name" */
void print_error(char *format, ...)
{
	va_list ap, ap2;
	char *buf;
	int len;

	va_start(ap, format);
	va_copy(ap2, ap);
	len = vsnprintf(NULL, 0, format, ap2);

	buf = malloc(len + strlen(program_name) + 1);
	if (buf == NULL)
		goto exit;

	len = sprintf(buf, "%s: ", program_name);
	vsprintf(buf + len, format, ap);

	fputs(buf, stderr);

exit:	free(buf);
	va_end(ap);
	va_end(ap2);
}

int main(int argc, char **argv)
{
	program_name = strrchr(argv[0], '/');
	if (program_name)
		program_name++;
	else
		program_name = argv[0];

	if (argc < 2) {
		print_help(stderr);
		return 2;
	}

	if (strcmp(argv[1], "-V") == 0) {
		print_version();
		return 0;
	}

	if (strcmp(argv[1], "-h") == 0) {
		print_help(stdout);
		return 0;
	}

	if (strcmp(argv[1], "copy") == 0) {
		return main_copy(argc - 1, argv + 1);
	}

	if (strcmp(argv[1], "analyze") == 0) {
		return main_analyze(argc - 1, argv + 1);
	}

	print_help(stderr);

	return 2;
}
