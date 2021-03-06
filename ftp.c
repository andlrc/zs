/*
 * zs - work with, and move objects from one AS/400 to another.
 * Copyright (C) 2018  Andreas Louv <andreas@louv.dk>
 * See LICENSE
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdarg.h>
#include <sys/time.h>
#include "ftp.h"

/*
 * error message follow the index of "enum ftp_errors"
 */
static const char *ftp_error_messages[] = {
    [FTP_SUCCESS] = "Success",
    [EFTP_OVERFLOW] = "Destination buffer would overflow",
    [EFTP_TIMEDOUT] = "Timed Out",
    [EFTP_BADRPLY] = "Unknown reply status",
    [EFTP_BADRESP] = "Bad response from server",
    [EFTP_CONTRESP] = "Multi line reply from server",
    [EFTP_NOLOGIN] = "Not Logged In",
    [EFTP_WOULDBLOCK] = "Reading from socket would block",
    [EFTP_BADVAR] = "Unknown variable",
    [EFTP_NOHOST] = "Missing host"
};

/*
 * print debug information based on the verbosity level set
 */
static void
print_debug(struct ftp *ftp, enum ftp_verbosity verbosity,
	    char *format, ...)
{
    char            buf[BUFSIZ];
    va_list         ap;

    if (ftp->verbosity >= verbosity) {
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	fputs(buf, stderr);
    }
}

/*
 * initialize the ftp struct, should always be called before anything else
 */
void
ftp_init(struct ftp *ftp)
{
    memset(ftp, 0, sizeof(struct ftp));

    ftp->recvline.buffer = NULL;
    ftp->server.port = FTP_PORT;
    ftp->server.maxtries = 100;
}

/*
 * close the ftp connection and cleanup
 */
void
ftp_close(struct ftp *ftp)
{
    if (ftp->sock != -1)
	close(ftp->sock);
    ftp->sock = -1;

    free(ftp->recvline.buffer);
    ftp->recvline.buffer = NULL;
}

/*
 * set ftp configuration
 */
int
ftp_set_variable(struct ftp *ftp, enum ftp_variable var, char *val)
{
    switch (var) {
    case FTP_VAR_HOST:
	strncpy(ftp->server.host, val, FTP_HOSTSIZ);
	ftp->server.host[FTP_HOSTSIZ - 1] = '\0';
	return 0;
    case FTP_VAR_USER:
	strncpy(ftp->server.user, val, FTP_USRSIZ);
	ftp->server.user[FTP_USRSIZ - 1] = '\0';
	return 0;
    case FTP_VAR_PASSWORD:
	strncpy(ftp->server.password, val, FTP_PASSSIZ);
	ftp->server.password[FTP_PASSSIZ - 1] = '\0';
	return 0;
    case FTP_VAR_PORT:
	ftp->server.port = atoi(val);
	return 0;
    case FTP_VAR_VERBOSE:
	if (*val == '+' || *val == '-') {
	    /*
	     * +1, -2, ...
	     */
	    ftp->verbosity += atoi(val);
	} else {
	    /*
	     * 1, 2, ...
	     */
	    ftp->verbosity = atoi(val);
	}
	return 0;
    case FTP_VAR_MAXTRIES:
	if (*val == '+' || *val == '-') {
	    /*
	     * +1, -2, ...
	     */
	    ftp->server.maxtries += atoi(val);
	} else {
	    /*
	     * 1, 2, ...
	     */
	    ftp->server.maxtries = atoi(val);
	}
	return 0;
    }

    ftp->errnum = EFTP_BADVAR;
    return -1;
}

/*
 * connect to the ftp server.
 * host, user, etc. should already be set using "ftp_set_variable"
 */
int
ftp_connect(struct ftp *ftp)
{
    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *ressave;
    int             rc;
    int             errno_;
    char            sport[6];	/* connection port */

    snprintf(sport, sizeof(sport), "%d", ftp->server.port);

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    if (*ftp->server.host == '\0') {
	ftp->errnum = EFTP_NOHOST;
	return -1;
    }

    print_debug(ftp, FTP_VERBOSE_DEBUG,
		"CONNECT: getaddrinfo(\"%s\", \"%s\", ..., ...)\n",
		ftp->server.host, sport);

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

    errno_ = errno;
    for (ftp->sock = -1, ressave = res; res; res = res->ai_next) {
	print_debug(ftp, FTP_VERBOSE_DEBUG,
		    "CONNECT: socket(%d, %d, %d)\n",
		    res->ai_family, res->ai_socktype, res->ai_protocol);
	if ((ftp->sock = socket(res->ai_family, res->ai_socktype,
				res->ai_protocol)) < 0)
	    continue;		/* ignore */
	print_debug(ftp, FTP_VERBOSE_DEBUG,
		    "CONNECT: connect(%d, %u, %d)\n",
		    ftp->sock, res->ai_addr, res->ai_addrlen);
	if (connect(ftp->sock, res->ai_addr, res->ai_addrlen) == 0)
	    break;		/* success */
	errno_ = errno;
	close(ftp->sock);
	ftp->sock = -1;
    }

    freeaddrinfo(ressave);

    if (ftp->sock == -1) {
	errno = errno_;
	ftp->errnum = EFTP_SYSTEM;
	return -1;
    }

    fcntl(ftp->sock, F_SETFL, O_NONBLOCK);

    /*
     * read welcome message
     */
    ftp->cmd.tries = 0;
    rc = ftp_cmdcontinue(ftp);
    if (ftp_dfthandle(ftp, rc, 220) == -1)
	return -1;

    /*
     * FIXME: AUTH TLS | AUTH SSL
     */

    /*
     * login
     */
    if (*ftp->server.user) {
	rc = ftp_cmd(ftp, "USER %s\r\n", ftp->server.user);
	if (ftp_dfthandle(ftp, rc, 331) == -1)
	    return -1;

	rc = ftp_cmd(ftp, "PASS %s\r\n", ftp->server.password);
	if (ftp_dfthandle(ftp, rc, 230) == -1)
	    return -1;
    }

    /*
     * binary mode
     */
    rc = ftp_cmd(ftp, "type I\r\n");
    if (ftp_dfthandle(ftp, rc, 200) == -1)
	return -1;

    return 0;
}

/*
 * run a ftp command, note each command should be terminated with "\r\n"
 */
int
ftp_cmd(struct ftp *ftp, char *format, ...)
{
    char            cmd[BUFSIZ];
    int             len;
    va_list         ap;

    va_start(ap, format);
    len = vsnprintf(cmd, sizeof(cmd), format, ap);
    va_end(ap);

    ftp_write(ftp, cmd, len);

    ftp->cmd.tries = 0;

    return ftp_cmdcontinue(ftp);
}

/*
 * same as "ftp_cmd" but a re-entrant interface that supports reading the
 * response
 */
int
ftp_cmd_r(struct ftp *ftp, struct ftpansbuf *ftpans, char *format, ...)
{
    char            cmd[BUFSIZ];
    int             len;
    va_list         ap;

    va_start(ap, format);
    len = vsnprintf(cmd, sizeof(cmd), format, ap);
    va_end(ap);

    ftp_write(ftp, cmd, len);

    ftp->cmd.tries = 0;

    return ftp_cmdcontinue_r(ftp, ftpans);
}

/*
 * pull the server for a response to "ftp_cmd".
 * the return value is:
 * - 0 when no data is available,
 * - -1 on error,
 * - and the ftp reply code on success
 */
int
ftp_cmdcontinue(struct ftp *ftp)
{
    struct ftpansbuf ftpans;

    memset(&ftpans, 0, sizeof(struct ftpansbuf));

    return ftp_cmdcontinue_r(ftp, &ftpans);
}

/*
 * same as "ftp_cmdcontinue", but with the same re-entrant properties as
 * "ftp_cmd_r"
 */
int
ftp_cmdcontinue_r(struct ftp *ftp, struct ftpansbuf *ansbuf)
{
    struct timespec sleeper;

    sleeper.tv_sec = 0;
    sleeper.tv_nsec = 250L * 1000L * 1000L;
    nanosleep(&sleeper, NULL);

    if (ftp->cmd.tries++ < ftp->server.maxtries) {
	if (ftp_recvans(ftp, ansbuf) == 0) {
	    if (ansbuf->continues) {
		ftp->errnum = EFTP_CONTRESP;
		return 0;
	    }
	    return ansbuf->reply;
	}
	return 0;
    }

    ftp->errnum = EFTP_TIMEDOUT;
    return -1;
}

/*
 * default handler for "ftp_cmd".
 * the return value is:
 * - 0 if the expected reply is received,
 * - or -1 on error
 */
int
ftp_dfthandle(struct ftp *ftp, int rc, int reply)
{
    struct ftpansbuf ftpans;

    memset(&ftpans, 0, sizeof(struct ftpansbuf));

    return ftp_dfthandle_r(ftp, &ftpans, rc, reply);
}

/*
 * same as "ftp_dfthandle", but with the same re-entrant properties as
 * "ftp_cmd_r"
 */
int
ftp_dfthandle_r(struct ftp *ftp, struct ftpansbuf *ftpans,
		int rc, int reply)
{
    while (rc != reply) {
	switch (rc) {
	case 0:
	    rc = ftp_cmdcontinue_r(ftp, ftpans);
	    break;
	case 530:
	    ftp->errnum = EFTP_NOLOGIN;
	    return -1;
	case -1:		/* timeout */
	    return -1;
	default:
	    ftp->errnum = EFTP_BADRPLY;
	    return -1;
	}
    }
    return 0;
}

/*
 * lower level interface to write to the ftp socket, works like "write(2)"
 */
ssize_t
ftp_write(struct ftp * ftp, void *buf, size_t count)
{
    ssize_t         rc;

    rc = write(ftp->sock, buf, count);
    switch (rc) {
    case 0:
	print_debug(ftp, FTP_VERBOSE_MORE, "WRITE: [NOTHING]");
	break;
    case -1:
	print_debug(ftp, FTP_VERBOSE_MORE, "WRITE: %s", strerror(errno));
	break;
    default:
	if (strncmp(buf, "PASS ", 5) == 0) {
	    print_debug(ftp, FTP_VERBOSE_SOME, "WRITE: PASS ******\n");
	} else {
	    print_debug(ftp, FTP_VERBOSE_SOME, "WRITE: %*s",
			(int) rc, (char *) buf);
	}
    }

    return rc;
}

/*
 * lower level interface to read from the ftp socket, works like "recv(2)"
 */
ssize_t
ftp_recv(struct ftp * ftp, void *buf, size_t len, int flags)
{
    ssize_t         rc;
    int             errno_;

    rc = recv(ftp->sock, buf, len, flags);
    errno_ = errno;

    switch (rc) {
    case 0:
	print_debug(ftp, FTP_VERBOSE_MORE, "RECV: [NOTHING]\n");
	break;
    case -1:
	print_debug(ftp, FTP_VERBOSE_MORE, "RECV: [%s]\n",
		    strerror(errno));
	break;
    }

    errno = errno_;
    return rc;
}

/*
 * lower level interface used by "ftp_continue" and "ftp_continue_r"
 */
int
ftp_recvans(struct ftp *ftp, struct ftpansbuf *ansbuf)
{
    char            buf[BUFSIZ];
    ssize_t         recvlen;
    int             rc;

    recvlen = ftp_recvline(ftp, buf, sizeof(buf));
    if (recvlen <= 0)
	return -1;
    if (recvlen < 4) {
	ftp->errnum = EFTP_BADRPLY;
	return -1;
    }
    /*
     * assume that ftpansbuf.buffer is of size BUFSIZ
     */

    rc = sscanf(buf, "%d", &ansbuf->reply);
    if (rc == 0 || rc == EOF) {
	ftp->errnum = EFTP_BADRESP;
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
    ansbuf->buffer[recvlen - 4] = '\0';

    return 0;
}

/*
 * read line from ftp socket
 */
ssize_t
ftp_recvline(struct ftp * ftp, char *resbuf, size_t ressiz)
{
    char            recvbuf[BUFSIZ];
    char           *pbufnl;
    char           *ptmp;
    size_t          nloffset;
    ssize_t         recvlen;

    /*
     * allocate temporary storage
     */
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
	recvlen = ftp_recv(ftp, recvbuf, sizeof(recvbuf), 0);
	if (recvlen == -1) {
	    /*
	     * no line ready just yet
	     */
	    if (errno == EWOULDBLOCK) {
		ftp->errnum = EFTP_WOULDBLOCK;
		return 0;
	    } else {
		ftp->errnum = EFTP_SYSTEM;
		return -1;
	    }
	}

	/*
	 * realloc if needed
	 */
	while (ftp->recvline.end - ftp->recvline.buffer +
	       recvlen >= (ssize_t) ftp->recvline.size) {
	    ftp->recvline.size *= 2;
	    ptmp = realloc(ftp->recvline.buffer, ftp->recvline.size);
	    if (!ptmp) {
		ftp->errnum = EFTP_SYSTEM;
		return -1;
	    }
	    ftp->recvline.end =
		ptmp + (ftp->recvline.end - ftp->recvline.buffer);
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
	ftp->recvline.end =
	    ftp->recvline.buffer + (ftp->recvline.end - (pbufnl + 2));
    }

    print_debug(ftp, FTP_VERBOSE_SOME, "RECVLINE: %s", resbuf);
    return nloffset + 1;
}

/*
 * store unique file on ftp server.
 * NOTE: remotename can be updated with the actual stored name
 */
int
ftp_put(struct ftp *ftp, char *localname, char *remotename)
{
    int             rc;
    struct ftpansbuf ftpans;
    int             hostp[4];	/* host IP */
    int             portp[2];	/* host port */
    int             pasvfd;
    int             localfd;
    struct sockaddr_in addr;
    char            buf[BUFSIZ];
    char            resbuf[BUFSIZ];
    ssize_t         reslen;
    int             errno_;

    memset(&ftpans, 0, sizeof(struct ftpansbuf));
    rc = ftp_cmd_r(ftp, &ftpans, "PASV\r\n");
    if (ftp_dfthandle_r(ftp, &ftpans, rc, 227) == -1)
	return -1;

    if (sscanf(ftpans.buffer,
	       "Entering Passive Mode (%d,%d,%d,%d,%d,%d).",
	       &hostp[0], &hostp[1], &hostp[2], &hostp[3], &portp[0],
	       &portp[1]) != 6) {
	ftp->errnum = EFTP_BADRESP;
	return -1;
    }

    pasvfd = socket(AF_INET, SOCK_STREAM, 0);
    if (pasvfd == -1) {
	ftp->errnum = EFTP_SYSTEM;
	return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr =
	(hostp[3] << 24) + (hostp[2] << 16) + (hostp[1] << 8) + hostp[0];
    addr.sin_port = htons(portp[0] * 256 + portp[1]);

    if (connect(pasvfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
	ftp->errnum = EFTP_SYSTEM;
	return -1;
    }

    snprintf(buf, sizeof(buf), "STOU %s\r\n", remotename);
    rc = ftp_write(ftp, buf, strlen(buf));

    localfd = open(localname, O_RDONLY);
    if (localfd == -1) {
	ftp->errnum = EFTP_SYSTEM;
	return -1;
    }

    /*
     * read STOU ack. reply
     */
    ftp->cmd.tries = 0;
    memset(&ftpans, 0, sizeof(struct ftpansbuf));
    rc = ftp_cmdcontinue_r(ftp, &ftpans);
    if (ftp_dfthandle_r(ftp, &ftpans, rc, 150) == -1)
	return -1;
    if (sscanf(ftpans.buffer, "Sending file to %s", remotename) != 1) {
	ftp->errnum = EFTP_BADRESP;
	return -1;
    }

    do {
	reslen = read(localfd, resbuf, sizeof(resbuf));
	if (reslen == -1 || write(pasvfd, resbuf, reslen) != reslen) {
	    errno_ = errno;
	    close(pasvfd);
	    close(localfd);
	    errno = errno_;
	    ftp->errnum = EFTP_SYSTEM;
	    return -1;
	}
    } while (reslen != 0);

    close(localfd);
    close(pasvfd);

    /*
     * read STOR ok reply
     */
    ftp->cmd.tries = 0;
    rc = ftp_cmdcontinue(ftp);
    if (ftp_dfthandle(ftp, rc, 226) == -1)
	return -1;

    return 0;
}

/*
 * download file from server
 */
int
ftp_get(struct ftp *ftp, char *localname, char *remotename)
{
    int             rc;
    struct ftpansbuf ftpans;
    int             hostp[4];	/* host IP */
    int             portp[2];	/* host port */
    int             pasvfd;
    int             localfd;
    struct sockaddr_in addr;
    char            buf[BUFSIZ];
    char            resbuf[BUFSIZ];
    ssize_t         reslen;
    int             errno_;

    memset(&ftpans, 0, sizeof(struct ftpansbuf));
    rc = ftp_cmd_r(ftp, &ftpans, "PASV\r\n");
    if (ftp_dfthandle_r(ftp, &ftpans, rc, 227) == -1)
	return -1;

    if (sscanf(ftpans.buffer,
	       "Entering Passive Mode (%d,%d,%d,%d,%d,%d).",
	       &hostp[0], &hostp[1], &hostp[2], &hostp[3], &portp[0],
	       &portp[1]) != 6) {
	ftp->errnum = EFTP_BADRESP;
	return -1;
    }

    pasvfd = socket(AF_INET, SOCK_STREAM, 0);
    if (pasvfd == -1) {
	ftp->errnum = EFTP_SYSTEM;
	return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr =
	(hostp[3] << 24) + (hostp[2] << 16) + (hostp[1] << 8) + hostp[0];
    addr.sin_port = htons(portp[0] * 256 + portp[1]);

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

    /*
     * read RETR ack. reply
     */
    ftp->cmd.tries = 0;
    rc = ftp_cmdcontinue(ftp);
    if (ftp_dfthandle(ftp, rc, 150) == -1)
	return -1;

    do {
	reslen = recv(pasvfd, resbuf, sizeof(resbuf), 0);
	if (reslen == -1 || write(localfd, resbuf, reslen) != reslen) {
	    errno_ = errno;
	    close(pasvfd);
	    close(localfd);
	    errno = errno_;
	    ftp->errnum = EFTP_SYSTEM;
	    return -1;
	}
    } while (reslen != 0);

    close(localfd);
    close(pasvfd);

    /*
     * read RETR reply
     */
    ftp->cmd.tries = 0;
    rc = ftp_cmdcontinue(ftp);
    if (ftp_dfthandle(ftp, rc, 226) == -1)
	return -1;

    return 0;
}

/*
 * print errors.
 * should always be called immediately after an error occurred as the value of
 * "errno" cannot change.
 */
const char     *
ftp_strerror(struct ftp *ftp)
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
