#ifndef FTP_H
#define FTP_H 1

#define FTP_PORT 21

enum ftp_errors {
	FTP_SUCCESS = 0,
	EFTP_OVERFLOW,
	EFTP_TIMEOUT,
	EFTP_UNKWNRPLY,
	EFTP_BADRES,
	EFTP_NOREPLY,
	EFTP_CONTREPLY,
	EFTP_NOTLOGGEDIN,
	EFTP_WOULDBLOCK,

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

struct ftp {
	enum ftp_errors errnum;
	int verbosity;
	int sock;
	struct {
		int tries;
	} cmd;
	struct {
		char *buffer;
		char *end;
		size_t size;
	} recvline;
};

struct ftpansbuf {
	int reply;
	int continues; /* boolean */
	char buffer[BUFSIZ];
};

void ftp_init(struct ftp *);
int ftp_connect(struct ftp *, char *, int, char *, char *);
void ftp_close(struct ftp *);
ssize_t ftp_recvline(struct ftp *, char *, size_t);
int ftp_recvans(struct ftp *, struct ftpansbuf *);
ssize_t ftp_recv(struct ftp *, void *, size_t, int);
int ftp_cmd(struct ftp *, char *, ...);
int ftp_cmdcontinue(struct ftp *);
int ftp_cmd_r(struct ftp *, struct ftpansbuf *, char *, ...);
int ftp_cmdcontinue_r(struct ftp *, struct ftpansbuf *);
int ftp_put(struct ftp *ftp, char *, char *);
int ftp_get(struct ftp *ftp, char *, char *);
ssize_t ftp_write(struct ftp *, void *, size_t);
const char *ftp_strerror(struct ftp *);

#endif
