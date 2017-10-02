#include "zs.h"
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

int Z_addobject(struct Z_server *server, char *objbuf)
{
	struct Z_object *pobj;
	char buf[BUFSIZ];
	char *pbuf;
	char *pbufsep;

	if (server->objectlen + 1 == Z_MAXOBJ) {
		errno = EOVERFLOW;
		return -1;
	}

	pobj = &server->objects[server->objectlen];
	memset(pobj, 0, sizeof(struct Z_object));

	strncpy(buf, objbuf, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	pbufsep = buf;
	pbuf = strsep(&pbufsep, "/");
	strncpy(pobj->library, pbuf, sizeof(pobj->library));
	pobj->library[sizeof(pobj->library) - 1] = '\0';

	if (!pbufsep) {
		errno = EINVAL;
		return -1;
	}

	pbuf = strsep(&pbufsep, "*");
	strncpy(pobj->object, pbuf, sizeof(pobj->object));
	pobj->object[sizeof(pobj->object) - 1] = '\0';

	if (pbufsep) {
		pbuf = pbufsep;
		strncpy(pobj->type, pbuf, sizeof(pobj->type));
		pobj->type[sizeof(pobj->type) - 1] = '\0';
	}
	else {
		strcpy(pobj->type, "ALL");
	}

	server->objectlen++;

	return 0;
}

int Z_cfgfile(struct Z_server *server, char *cfgfile)
{
	FILE *fp;
	char *linebuf = 0;
	size_t linesiz = 0;
	char *plinebuf;
	char *pkey;
	char *pval;
	int vallen;

	if (!(fp = fopen(cfgfile, "r")))
		return -1;

	while (getline(&linebuf, &linesiz, fp) != -1) {
		plinebuf = linebuf;

		/* Trim leading spaces */
		while (isspace(*plinebuf))
			plinebuf++;

		/* Skip comments */
		if (*plinebuf == '#' || *plinebuf == '\0')
			continue;

		pkey = strsep(&plinebuf, " \t");
		if (plinebuf) {
			while (isspace(*plinebuf))
				plinebuf++;
		}
		pval = plinebuf;
		vallen = strlen(pval);
		if (pval[vallen - 1] == '\n')
			pval[vallen - 1] = '\0';
		if (pval[vallen - 2] == '\r')
			pval[vallen - 2] = '\0';

		/* TODO: A bit of memory leak? */
		switch (*pkey) {
		case 's':
			if (strcmp(pkey, "server") == 0) {
				if (!(server->server = strdup(pval)))
					return -1;
				continue; /* while */
			}
			break;
		case 'u':
			if (strcmp(pkey, "user") == 0) {
				if (!(server->user = strdup(pval)))
					return -1;
				continue; /* while */
			}
			break;
		case 'p':
			if (strcmp(pkey, "password") == 0) {
				if (!(server->password = strdup(pval)))
					return -1;
				continue; /* while */
			}
			break;
		}

		errno = EBADMSG;
		return -1;
	}

	free(linebuf);
	fclose(fp);
	return 0;
}

int Z_cmd(struct Z_server *server, char *reqbuf)
{
	char resbuf[BUFSIZ];

	if (Z_write(server, reqbuf) == -1)
		return -1;

	if (Z_recvline(server, resbuf, sizeof(resbuf), 0) == -1)
		return -1;

	if (Z_printf(server, Z_OUTFTP, resbuf) == -1)
		return -1;

	if (*resbuf == '5') {
		errno = EIO;
		return -1;
	}

	return 0;
}

int Z_connect(struct Z_server *server)
{
	struct sockaddr_in addr;
	char resbuf[BUFSIZ];
	int i;

	if ((server->sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		return -1;

	addr.sin_addr.s_addr = inet_addr(server->server);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(Z_FTPPORT);

	if (connect(server->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return -1;

	/* Welcome message */
	for (i = 0; i < 2; i++) {
		if (Z_recvline(server, resbuf, sizeof(resbuf), 0) == -1)
			return -1;
		if (Z_printf(server, Z_OUTFTP, resbuf) == -1)
			return -1;
	}

	return 0;
}

int Z_get(struct Z_server *server, char *local, char *remote)
{
	char reqbuf[BUFSIZ];
	char resbuf[BUFSIZ];
	int reslen;
	int localfd;

	int sock;

	if ((sock = Z_pasv(server)) == -1)
		return -1;

	snprintf(reqbuf, sizeof(reqbuf), "RETR %s", remote);
	if (Z_cmd(server, reqbuf) == -1)
		return -1;

	if ((localfd = creat(local, S_IWUSR | S_IRUSR)) == -1)
		return -1;
	do {
		reslen = recv(sock, resbuf, sizeof(resbuf), 0);
		if (reslen == -1) {
			goto recverror;
		}
		if (write(localfd, resbuf, reslen) != reslen)
			goto recverror;

	} while(reslen != 0);

	close(localfd);
	close(sock);

	do {
		if (Z_recvline(server, resbuf, sizeof(resbuf), 0) == -1)
			return -1;

		if (Z_printf(server, Z_OUTFTP, resbuf) == -1)
			return -1;

		if (*resbuf == '5') {
			errno = EIO;
			return -1;
		}

	} while (strncmp(resbuf, "226", 3) != 0);

	return 0;

recverror:
	close(localfd);
	close(sock);
	return -1;
}

int Z_joblog(struct Z_server *server, char *output)
{
	fprintf(stderr, "No implemeted\n");
	return 0;
}

int Z_pasv(struct Z_server *server)
{
	struct sockaddr_in addr;
	char resbuf[BUFSIZ];
	int _;
	int porta, portb, port;
	int sock;

	if (Z_write(server, "PASV") == -1)
		return -1;

	if (Z_recvline(server, resbuf, sizeof(resbuf), 0) == -1)
		return -1;

	if (Z_printf(server, Z_OUTFTP, resbuf) == -1)
		return -1;

	if (sscanf(resbuf, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).",
	       &_, &_, &_, &_, &porta, &portb) != 6) {
		errno = EIO;
		return -1;
	}
	port = porta * 256 + portb;

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		return -1;

	addr.sin_addr.s_addr = inet_addr(server->server);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return -1;

	return sock;
}

int Z_printf(struct Z_server *server, enum Z_outputtype type, char *msg)
{
	switch (type)
	{
	case Z_OUTFTP:
		if (server->verbose > 0)
			printf("response: %s", msg);
		break;
	case Z_OUTCMD:
		if (strncasecmp(msg, "PASS", 4) == 0) {
			msg = "PASS ********";
		}
		if (server->verbose > 1)
			printf("command: %s\n", msg);
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int Z_put(struct Z_server *server, char *remote, char *local)
{
	char reqbuf[BUFSIZ];
	char resbuf[BUFSIZ];
	int reslen;
	int localfd;

	int sock;

	if ((sock = Z_pasv(server)) == -1)
		return -1;

	snprintf(reqbuf, sizeof(reqbuf), "STOR %s", remote);
	if (Z_cmd(server, reqbuf) == -1)
		return -1;

	if ((localfd = open(local, O_RDONLY)) == -1)
		return -1;
	do {
		reslen = read(localfd, resbuf, sizeof(resbuf));
		if (reslen == -1) {
			goto writeerror;
		}
		if (write(sock, resbuf, reslen) != reslen)
			goto writeerror;

	} while(reslen != 0);

	close(localfd);
	close(sock);

	do {
		if (Z_recvline(server, resbuf, sizeof(resbuf), 0) == -1)
			return -1;

		if (Z_printf(server, Z_OUTFTP, resbuf) == -1)
			return -1;

		if (*resbuf == '5') {
			errno = EIO;
			return -1;
		}

	} while (strncmp(resbuf, "226", 3) != 0);

	return 0;

writeerror:
	close(localfd);
	close(sock);
	return -1;
}

int Z_quit(struct Z_server *server)
{
	if (Z_cmd(server, "QUIT") == -1)
		return -1;

	if (close(server->sock) == -1)
		return -1;

	return 0;
}

int Z_recvline(struct Z_server *server, char *destbuf,
	       size_t destbufsiz, int flags)
{
	static char *pbuf = 0;
	static char *pbufend;
	static size_t bufsiz;

	char resbuf[BUFSIZ];
	char *pbuftmp, *pbufnl = 0;
	int reslen;
	size_t nloff;

	if (!pbuf) {
		bufsiz = BUFSIZ;
		if (!(pbuf = malloc(bufsiz)))
			return -1;
		pbufend = pbuf;
	}

	do {
		reslen = recv(server->sock, resbuf, sizeof(resbuf), flags);
		if (reslen == -1)
			return -1;
		/* Realloc when needed */
		while (pbufend - pbuf + reslen >= bufsiz) {
			bufsiz *= 2;
			if (!(pbuftmp = realloc(pbuf, bufsiz)))
				return -1;
			pbufend = pbuftmp + (pbufend - pbuf);
			pbuf = pbuftmp;
		}

		strncpy(pbufend, resbuf, reslen);
		*(pbufend + reslen) = '\0';
		pbufnl = strstr(pbufend, "\r\n");
		pbufend += reslen;
	} while (!pbufnl);

	nloff = pbufnl - pbuf;		/* CR NL */
	if (nloff + 1 > destbufsiz) {
		errno = EOVERFLOW;
		return -1;
	}

	strncpy(destbuf, pbuf, nloff);
	destbuf[nloff] = '\n';		/* CR */
	destbuf[nloff + 1] = '\0';	/* NL */

	memmove(pbuf, pbufnl + 2, pbufend - (pbufnl + 1));
	if (*pbuf == '\0') {
		free(pbuf);
		pbuf = 0;
	} else {
		pbufend = pbuf + (pbufend - (pbufnl + 2));
	}

	return 0;
}

int Z_signon(struct Z_server *server)
{
	char reqbuf[BUFSIZ];

	snprintf(reqbuf, sizeof(reqbuf), "USER %s", server->user);
	if (Z_cmd(server, reqbuf) == -1)
		return -1;

	snprintf(reqbuf, sizeof(reqbuf), "PASS %s", server->password);
	if (Z_cmd(server, reqbuf) == -1)
		return -1;

	if (Z_cmd(server, "site namefmt 1") == -1)
		return -1;

	return 0;
}

int Z_system(struct Z_server *server, char *cmd)
{
	char reqbuf[BUFSIZ];

	snprintf(reqbuf, sizeof(reqbuf), "RCMD %s", cmd);
	if (Z_cmd(server, reqbuf) == -1)
		return -1;

	return 0;
}

int Z_write(struct Z_server *server, char *reqbuf)
{
	char buf[BUFSIZ];
	int buflen;
	int ret;

	buflen = strlen(reqbuf) + 3;

	if (Z_printf(server, Z_OUTCMD, reqbuf) == -1)
		return -1;

	if (snprintf(buf, sizeof(buf), "%s\r\n", reqbuf) != buflen - 1) {
		return -1;
	}

	ret = write(server->sock, buf, buflen);
	return ret;
}
