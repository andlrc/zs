#ifndef _H_ZS_
#define _H_ZS_ 1

#include <string.h>

#define	Z_MAXOBJ	16
#define	Z_FTPPORT	21

#define	Z_CMD_JOBLOG1	"CRTPF FILE(QTEMP/ZSLOG) RCDLEN(528) SIZE(1000000 10000 100)"
#define Z_CMD_JOBLOG2	"OVRPRTF FILE(QPJOBLOG) HOLD(*YES) SPLFOWN(*JOB) OVRSCOPE(*JOB)"
#define	Z_CMD_JOBLOG3	"DSPJOBLOG OUTPUT(*PRINT)"
#define	Z_CMD_JOBLOG4	"CPYSPLF JOB(*) FILE(QPJOBLOG) TOFILE(QTEMP/ZSLOG) SPLNBR(*LAST)"
#define	Z_CMD_JOBLOG5	"CPYTOSTMF FROMMBR('/QSYS.LIB/QTEMP.LIB/ZSLOG.FILE/ZSLOG.MBR') TOSTMF('/tmp/zslog') STMFOPT(*REPLACE) STMFCCSID(1208)"

struct Z_object {
	char library[11];
	char object[11];
	char type[7];
};

struct Z_server {
	int sock;
	int verbose;
	char *server;
	char *user;
	char *password;
	struct Z_object objects[Z_MAXOBJ];	/* Source */
	int objectlen;				/* Source */
	char *library;				/* Target */
	char *joblog;
};

enum Z_outputtype {
	Z_OUTFTP,
	Z_OUTCMD
};

int Z_addobject(struct Z_server *srcserver, char *obj);
int Z_cfgfile(struct Z_server *server, char *cfgfile);
int Z_cmd(struct Z_server *server, char *reqbuf);
int Z_connect(struct Z_server *server);
int Z_get(struct Z_server *server, char *local, char *remote);
void Z_initserver(struct Z_server *server);
int Z_joblog(struct Z_server *server, char *output);
int Z_pasv(struct Z_server *server);
int Z_printf(struct Z_server *server, enum Z_outputtype flags, char *msg, ...);
int Z_put(struct Z_server *server, char *remote, char *local);
int Z_quit(struct Z_server *server);
int Z_recvline(struct Z_server *server, char *destbuf,
	       size_t destbufsiz, int flags);
int Z_signon(struct Z_server *server);
int Z_system(struct Z_server *server, char *reqbuf, ...);
int Z_write(struct Z_server *server, char *reqbuf);

#endif
