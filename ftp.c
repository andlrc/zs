#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdarg.h>
#include <time.h>
#include "ftp.h"

static const char *ftp_error_messages[] = {
	"success",
	"destination buffer would overflow",
	"timed out",
	"unknown response status",
	"bad response from server",
	"no reply from server",
	"multi line reply from server",
	"not logged in",
	"reading from socket would block",
	"unknown variable",
	"missing host"
};

static void print_debug(struct ftp *ftp, enum ftp_verbosity verbosity,
			char *format, ...)
{
	int len;
	char buf[BUFSIZ];
	va_list ap;

	if (ftp->verbosity >= verbosity) {
		len = sprintf(buf, "%d: ", ftp->sock);

		va_start(ap, format);
		vsnprintf(buf + len, sizeof(buf) - len, format, ap);
		va_end(ap);

		fputs(buf, stderr);

	}
}

void ftp_init(struct ftp *ftp)
{
	memset(ftp, 0, sizeof(struct ftp));
	ftp->recvline.buffer = NULL;
	ftp->server.port = FTP_PORT;
}

void ftp_close(struct ftp *ftp)
{
	close(ftp->sock);
	ftp->sock = -1;

	free(ftp->recvline.buffer);
	ftp->recvline.buffer = NULL;
}

int ftp_set_variable(struct ftp *ftp, enum ftp_variable var, char *val)
{
	switch (var) {
	case FTP_VAR_HOST:
		strncpy(ftp->server.host, val, FTP_HSTSIZ);
		ftp->server.host[FTP_HSTSIZ - 1] = '\0';
		break;
	case FTP_VAR_USER:
		strncpy(ftp->server.user, val, FTP_USRSIZ);
		ftp->server.user[FTP_USRSIZ - 1] = '\0';
		break;
	case FTP_VAR_PASSWORD:
		strncpy(ftp->server.password, val, FTP_PWDSIZ);
		ftp->server.password[FTP_PWDSIZ - 1] = '\0';
		break;
	case FTP_VAR_PORT:
		ftp->server.port = atoi(val);
		break;
	case FTP_VAR_VERBOSE:
		if (*val == '+' || *val == '-') {
			ftp->verbosity += atoi(val);
		} else {
			ftp->verbosity = atoi(val);
		}
		break;
	default:
		ftp->errnum = EFTP_UNKWNVAR;
		return -1;
	}

	return 0;
}

int ftp_connect(struct ftp *ftp)
{
	struct addrinfo hints, *res, *ressave;
	int rc;
	char sport[6];

	snprintf(sport, sizeof(sport), "%d", ftp->server.port);

	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;

	if (*ftp->server.host == '\0') {
		ftp->errnum = EFTP_MISHOST;
		return -1;
	}

	switch (getaddrinfo(ftp->server.host, sport, &hints, &res)) {
	case EAI_SYSTEM:
		ftp->errnum = EFTP_SYSTEM;
		return -1;
	case EAI_BADFLAGS:
		ftp->errnum = EFTP_GAI_BADFLAGS;
		return -1;
	case EAI_NONAME:
		ftp->errnum = EFTP_GAI_NONAME;
		return -1;
	case EAI_AGAIN:
		ftp->errnum = EFTP_GAI_AGAIN;
		return -1;
	case EAI_FAIL:
		ftp->errnum = EFTP_GAI_FAIL;
		return -1;
	case EAI_FAMILY:
		ftp->errnum = EFTP_GAI_FAMILY;
		return -1;
	case EAI_SOCKTYPE:
		ftp->errnum = EFTP_GAI_SOCKTYPE;
		return -1;
	case EAI_SERVICE:
		ftp->errnum = EFTP_GAI_SERVICE;
		return -1;
	case EAI_MEMORY:
		ftp->errnum = EFTP_GAI_MEMORY;
		return -1;
	case EAI_OVERFLOW:
		ftp->errnum = EFTP_GAI_OVERFLOW;
		return -1;
	}

	for (ftp->sock = -1, ressave = res; res; res = res->ai_next) {
		if ((ftp->sock = socket(res->ai_family, res->ai_socktype,
					res->ai_protocol)) < 0)
			continue;	/* ignore */
		if (connect(ftp->sock, res->ai_addr, res->ai_addrlen) == 0)
			break;	/* success */
		close(ftp->sock);
		ftp->sock = -1;
	}

	freeaddrinfo(ressave);

	if (ftp->sock == -1) {
		ftp->errnum = EFTP_SYSTEM;
		return -1;
	}

	fcntl(ftp->sock, F_SETFL, O_NONBLOCK);

	/* read welcome message */
	ftp->cmd.tries = 0;
	rc = ftp_cmdcontinue(ftp);
	if (ftp_dfthandle(ftp, rc, 220) == -1)
		return -1;

	/* FIXME: figure out a proper AUTH */
	/* AUTH TLS | AUTH SSL */

	/* login */
	if (*ftp->server.user) {
		rc = ftp_cmd(ftp, "USER %s\r\n", ftp->server.user);
		if (ftp_dfthandle(ftp, rc, 331) == -1)
			return -1;

		rc = ftp_cmd(ftp, "PASS %s\r\n", ftp->server.password);
		if (ftp_dfthandle(ftp, rc, 230) == -1)
			return -1;
	}

	/* binary mode */
	rc = ftp_cmd(ftp, "type I\r\n");
	if (ftp_dfthandle(ftp, rc, 200) == -1)
		return -1;

	return 0;
}

int ftp_cmd(struct ftp *ftp, char *format, ...)
{
	char cmd[BUFSIZ];
	int len;
	va_list ap;
	va_start(ap, format);
	len = vsnprintf(cmd, sizeof(cmd), format, ap);
	va_end(ap);

	ftp_write(ftp, cmd, len);

	ftp->cmd.tries = 0;

	return ftp_cmdcontinue(ftp);
}

int ftp_cmd_r(struct ftp *ftp, struct ftpansbuf *ftpans, char *format, ...)
{
	char cmd[BUFSIZ];
	int len;
	va_list ap;
	va_start(ap, format);
	len = vsnprintf(cmd, sizeof(cmd), format, ap);
	va_end(ap);

	ftp_write(ftp, cmd, len);

	ftp->cmd.tries = 0;

	return ftp_cmdcontinue_r(ftp, ftpans);
}

int ftp_cmdcontinue(struct ftp *ftp)
{
	struct ftpansbuf ftpans;
	memset(&ftpans, 0, sizeof(struct ftpansbuf));

	return ftp_cmdcontinue_r(ftp, &ftpans);
}

int ftp_cmdcontinue_r(struct ftp *ftp, struct ftpansbuf *ansbuf)
{
	struct timespec sleeper;
	sleeper.tv_sec = 0;
	sleeper.tv_nsec = 250L * 1000L * 1000L;

	nanosleep(&sleeper, NULL);

	if (ftp->cmd.tries++ < 100) {
		if (ftp_recvans(ftp, ansbuf) == 0) {
			if (ansbuf->continues) {
				ftp->errnum = EFTP_CONTREPLY;
				return 0;
			}
			return ansbuf->reply;
		}
		return 0;
	}

	ftp->errnum = EFTP_TIMEOUT;
	return -1;
}

int ftp_dfthandle(struct ftp *ftp, int rc, int reply)
{
	struct ftpansbuf ftpans;
	memset(&ftpans, 0, sizeof(struct ftpansbuf));
	return ftp_dfthandle_r(ftp, &ftpans, rc, reply);
}

int ftp_dfthandle_r(struct ftp *ftp, struct ftpansbuf *ftpans,
		    int rc, int reply)
{
	while (rc != reply) {
		switch (rc) {
		case 0:
			rc = ftp_cmdcontinue_r(ftp, ftpans);
			break;
		case 530:
			ftp->errnum = EFTP_NOTLOGGEDIN;
			return -1;
		case -1:	/* timeout */
			return -1;
		default:
			ftp->errnum = EFTP_UNKWNRPLY;
			return -1;
		}
	}
	return 0;
}

ssize_t ftp_write(struct ftp * ftp, void *buf, size_t count)
{
	ssize_t rc;
	rc = write(ftp->sock, buf, count);
	switch (rc) {
	case 0:
		print_debug(ftp, FTP_VERBOSE_MORE, "::WRITE: [NOTHING]");
		break;
	case -1:
		print_debug(ftp, FTP_VERBOSE_MORE, "::WRITE: %s",
			    strerror(errno));
		break;
	default:
		if (strncmp(buf, "PASS ", 5) == 0) {
			print_debug(ftp, FTP_VERBOSE_SOME,
				    "WRITE: PASS ******\n");
		} else {
			print_debug(ftp, FTP_VERBOSE_SOME, "WRITE: %*s",
				    (int) rc, (char *) buf);
		}
	}

	return rc;
}

ssize_t ftp_recv(struct ftp * ftp, void *buf, size_t len, int flags)
{
	ssize_t rc;
	rc = recv(ftp->sock, buf, len, flags);
	switch (rc) {
	case 0:
		print_debug(ftp, FTP_VERBOSE_MORE, "::RECV: [NOTHING]\n");
		break;
	case -1:
		print_debug(ftp, FTP_VERBOSE_MORE, "::RECV: [%s]\n",
			    strerror(errno));
		break;
	default:
		print_debug(ftp, FTP_VERBOSE_MORE, "RECV: <%*s>",
			    (int) rc, (char *) buf);
	}

	return rc;
}

int ftp_recvans(struct ftp *ftp, struct ftpansbuf *ansbuf)
{
	char buf[BUFSIZ];
	ssize_t recvlen;
	int rc;

	recvlen = ftp_recvline(ftp, buf, sizeof(buf));
	if (recvlen <= 0)
		return -1;
	if (recvlen < 4) {
		ftp->errnum = EFTP_UNKWNRPLY;
		return -1;
	}
	/* assume that ftpansbuf.buffer is of size BUFSIZ */

	rc = sscanf(buf, "%d", &ansbuf->reply);
	if (rc == 0 || rc == EOF) {
		ftp->errnum = EFTP_BADRES;
		return -1;
	}
	/*
	 * buf = NNNIMMM
	 * NNN = reply status
	 * I   = inditator, "-" for more lines, and " " (space) for single line
	 * MMM = message
	 */
	ansbuf->continues = (buf[3] == '-');
	memcpy(ansbuf->buffer, buf + 4, recvlen - 4);
	ansbuf->buffer[recvlen - 4 + 1] = '\0';

	return 0;
}

ssize_t ftp_recvline(struct ftp * ftp, char *resbuf, size_t ressiz)
{
	char recvbuf[BUFSIZ];
	char *pbufnl, *ptmp;
	size_t nloffset;

	/* allocate temporary storage */
	if (!ftp->recvline.buffer) {
		ftp->recvline.size = BUFSIZ;
		ftp->recvline.buffer = malloc(ftp->recvline.size);
		if (!ftp->recvline.buffer) {
			ftp->errnum = EFTP_SYSTEM;
			return -1;
		}
		ftp->recvline.buffer[0] = '\0';
		ftp->recvline.end = ftp->recvline.buffer;
	}

	while ((pbufnl = strstr(ftp->recvline.buffer, "\r\n")) == NULL) {
		ssize_t recvlen = ftp_recv(ftp, recvbuf, sizeof(recvbuf), 0);
		if (recvlen == -1) {
			/* no line ready just yet */
			if (errno == EWOULDBLOCK) {
				ftp->errnum = EFTP_WOULDBLOCK;
				return 0;
			} else {
				ftp->errnum = EFTP_SYSTEM;
				return -1;
			}
		}

		/* realloc if needed */
		while (ftp->recvline.end - ftp->recvline.buffer +
		       recvlen >= (ssize_t) ftp->recvline.size) {
			ftp->recvline.size *= 2;
			ptmp = realloc(ftp->recvline.buffer, ftp->recvline.size);
			if (!ptmp) {
				ftp->errnum = EFTP_SYSTEM;
				return -1;
			}
			ftp->recvline.end = ptmp + (ftp->recvline.end - ftp->recvline.buffer);
			ftp->recvline.buffer = ptmp;
		}

		strncpy(ftp->recvline.end, recvbuf, recvlen);
		ftp->recvline.end[recvlen] = '\0';
		ftp->recvline.end += recvlen;
	}

	nloffset = pbufnl - ftp->recvline.buffer;	/* CR NL */
	if (nloffset + 1 > ressiz) {
		ftp->errnum = EFTP_OVERFLOW;
		return -1;
	}

	strncpy(resbuf, ftp->recvline.buffer, nloffset);
	resbuf[nloffset] = '\n';	/* CR */
	resbuf[nloffset + 1] = '\0';	/* NL */

	memmove(ftp->recvline.buffer, pbufnl + 2,
		ftp->recvline.end - (pbufnl + 1));
	if (*ftp->recvline.buffer == '\0') {
		free(ftp->recvline.buffer);
		ftp->recvline.buffer = 0;
	} else {
		ftp->recvline.end = ftp->recvline.buffer + (ftp->recvline.end - (pbufnl + 2));
	}

	print_debug(ftp, FTP_VERBOSE_SOME, "RECVLINE: %s", resbuf);
	return nloffset + 1;
}

int ftp_put(struct ftp *ftp, char *localname, char *remotename)
{
	int rc;
	struct ftpansbuf ftpans;
	int h1, h2, h3, h4, p1, p2;
	int pasvfd, localfd;
	struct sockaddr_in addr;
	char buf[BUFSIZ];
	char resbuf[BUFSIZ];
	ssize_t reslen;

	memset(&ftpans, 0, sizeof(struct ftpansbuf));
	rc = ftp_cmd_r(ftp, &ftpans, "PASV\r\n");
	if (ftp_dfthandle_r(ftp, &ftpans, rc, 227) == -1)
		return -1;

	if (sscanf(ftpans.buffer,
		   "Entering Passive Mode (%d,%d,%d,%d,%d,%d).",
		   &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
		ftp->errnum = EFTP_BADRES;
		return -1;
	}

	pasvfd = socket(AF_INET, SOCK_STREAM, 0);
	if (pasvfd == -1) {
		ftp->errnum = EFTP_SYSTEM;
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = (h4 << 24) + (h3 << 16) + (h2 << 8) + h1;
	addr.sin_port = htons(p1 * 256 + p2);

	if (connect(pasvfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
		ftp->errnum = EFTP_SYSTEM;
		return -1;
	}

	snprintf(buf, sizeof(buf), "STOR %s\r\n", remotename);
	rc = ftp_write(ftp, buf, strlen(buf));

	localfd = open(localname, O_RDONLY);
	if (localfd == -1) {
		ftp->errnum = EFTP_SYSTEM;
		return -1;
	}

	/* read STOR ack. reply */
	ftp->cmd.tries = 0;
	rc = ftp_cmdcontinue(ftp);
	if (ftp_dfthandle(ftp, rc, 150) == -1)
		return -1;

	do {
		reslen = read(localfd, resbuf, sizeof(resbuf));
		if (reslen == -1
		    || write(pasvfd, resbuf, reslen) != reslen) {
			close(pasvfd);
			close(localfd);
			ftp->errnum = EFTP_SYSTEM;
			return -1;
		}
	} while (reslen != 0);

	close(localfd);
	close(pasvfd);

	/* read STOR ok reply */
	ftp->cmd.tries = 0;
	rc = ftp_cmdcontinue(ftp);
	if (ftp_dfthandle(ftp, rc, 226) == -1)
		return -1;

	return 0;
}

int ftp_get(struct ftp *ftp, char *localname, char *remotename)
{
	int rc;
	struct ftpansbuf ftpans;
	int h1, h2, h3, h4, p1, p2;
	int pasvfd, localfd;
	struct sockaddr_in addr;
	char buf[BUFSIZ];
	char resbuf[BUFSIZ];
	ssize_t reslen;

	memset(&ftpans, 0, sizeof(struct ftpansbuf));
	rc = ftp_cmd_r(ftp, &ftpans, "PASV\r\n");
	if (ftp_dfthandle_r(ftp, &ftpans, rc, 227) == -1)
		return -1;

	if (sscanf(ftpans.buffer,
		   "Entering Passive Mode (%d,%d,%d,%d,%d,%d).",
		   &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
		ftp->errnum = EFTP_BADRES;
		return -1;
	}

	pasvfd = socket(AF_INET, SOCK_STREAM, 0);
	if (pasvfd == -1) {
		ftp->errnum = EFTP_SYSTEM;
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = (h4 << 24) + (h3 << 16) + (h2 << 8) + h1;
	addr.sin_port = htons(p1 * 256 + p2);

	if (connect(pasvfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
		ftp->errnum = EFTP_SYSTEM;
		return -1;
	}

	snprintf(buf, sizeof(buf), "RETR %s\r\n", remotename);
	rc = ftp_write(ftp, buf, strlen(buf));

	localfd = open(localname, O_WRONLY);
	if (localfd == -1) {
		ftp->errnum = EFTP_SYSTEM;
		return -1;
	}

	/* read RETR ack. reply */
	ftp->cmd.tries = 0;
	rc = ftp_cmdcontinue(ftp);
	if (ftp_dfthandle(ftp, rc, 150) == -1)
		return -1;

	do {
		reslen = recv(pasvfd, resbuf, sizeof(resbuf), 0);
		if (reslen == -1
		    || write(localfd, resbuf, reslen) != reslen) {
			close(pasvfd);
			close(localfd);
			ftp->errnum = EFTP_SYSTEM;
			return -1;
		}
	} while (reslen != 0);

	close(localfd);
	close(pasvfd);

	/* read RETR reply */
	ftp->cmd.tries = 0;
	rc = ftp_cmdcontinue(ftp);
	if (ftp_dfthandle(ftp, rc, 226) == -1)
		return -1;

	return 0;
}

const char *ftp_strerror(struct ftp *ftp)
{
	switch (ftp->errnum) {
	case EFTP_SYSTEM:
		return strerror(errno);
	case EFTP_GAI_BADFLAGS:
		return gai_strerror(EAI_BADFLAGS);
	case EFTP_GAI_NONAME:
		return gai_strerror(EAI_NONAME);
	case EFTP_GAI_AGAIN:
		return gai_strerror(EAI_AGAIN);
	case EFTP_GAI_FAIL:
		return gai_strerror(EAI_FAIL);
	case EFTP_GAI_FAMILY:
		return gai_strerror(EAI_FAMILY);
	case EFTP_GAI_SOCKTYPE:
		return gai_strerror(EAI_SOCKTYPE);
	case EFTP_GAI_SERVICE:
		return gai_strerror(EAI_SERVICE);
	case EFTP_GAI_MEMORY:
		return gai_strerror(EAI_MEMORY);
	case EFTP_GAI_OVERFLOW:
		return gai_strerror(EAI_OVERFLOW);
	default:
		return ftp_error_messages[ftp->errnum];
	}
}
