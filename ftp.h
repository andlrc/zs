/*
 * zs - copy objects from one AS/400 to another
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#ifndef FTP_H
#define FTP_H 1

#define FTP_PORT 21

enum ftp_errors {
	FTP_SUCCESS = 0,
	EFTP_OVERFLOW,
	EFTP_TIMEDOUT,
	EFTP_BADRPLY,
	EFTP_BADRESP,
	EFTP_CONTRESP,
	EFTP_NOLOGIN,
	EFTP_WOULDBLOCK,
	EFTP_BADVAR,
	EFTP_NOHOST,

	/* system errors */
	EFTP_SYSTEM = 99,

	/* getaddrinfo */
	EFTP_GAI_BADFLAGS,
	EFTP_GAI_NONAME,
	EFTP_GAI_AGAIN,
	EFTP_GAI_FAIL,
	EFTP_GAI_FAMILY,
	EFTP_GAI_SOCKTYPE,
	EFTP_GAI_SERVICE,
	EFTP_GAI_MEMORY,
	EFTP_GAI_OVERFLOW
};

enum ftp_verbosity {
	FTP_VERBOSE_SOME = 1,
	FTP_VERBOSE_MORE = 2
};

enum ftp_variable {
	FTP_VAR_HOST = 0,
	FTP_VAR_USER,
	FTP_VAR_PASSWORD,
	FTP_VAR_VERBOSE,
	FTP_VAR_PORT,
	FTP_VAR_MAXTRIES
};

#define FTP_HOSTSIZ	256
#define FTP_USRSIZ	128
#define FTP_PASSSIZ	128

struct ftpserver {
	char host[FTP_HOSTSIZ];
	int port;
	char user[FTP_USRSIZ];
	char password[FTP_PASSSIZ];
	int maxtries;
};

struct ftp {
	enum ftp_errors errnum;
	enum ftp_verbosity verbosity;
	int sock;
	struct ftpserver server;
	struct {
		int tries;
	} cmd;
	struct {
		char *buffer;
		char *end;
		size_t size;
	} recvline;
};

/* buffer used by "ftp_recvans" */
struct ftpansbuf {
	int reply;
	int continues;		/* boolean */
	char buffer[BUFSIZ];
};

void ftp_init(struct ftp *);
void ftp_close(struct ftp *);
int ftp_set_variable(struct ftp *, enum ftp_variable, char *);
int ftp_connect(struct ftp *);
ssize_t ftp_recvline(struct ftp *, char *, size_t);
int ftp_recvans(struct ftp *, struct ftpansbuf *);
ssize_t ftp_recv(struct ftp *, void *, size_t, int);
int ftp_cmd(struct ftp *, char *, ...);
int ftp_cmdcontinue(struct ftp *);
int ftp_cmd_r(struct ftp *, struct ftpansbuf *, char *, ...);
int ftp_cmdcontinue_r(struct ftp *, struct ftpansbuf *);
int ftp_dfthandle(struct ftp *, int, int);
int ftp_dfthandle_r(struct ftp *, struct ftpansbuf *, int, int);
int ftp_put(struct ftp *ftp, char *, char *);
int ftp_get(struct ftp *ftp, char *, char *);
ssize_t ftp_write(struct ftp *, void *, size_t);
const char *ftp_strerror(struct ftp *);

#endif
