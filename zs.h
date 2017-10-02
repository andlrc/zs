#ifndef _H_ZS_
#define _H_ZS_ 1

#include <string.h>

#define	Z_MAXOBJ	16
#define	Z_FTPPORT	21

#define	Z_CMD_CRTSAVF	"CRTSAVF FILE(QTEMP/ZS%d)"
#define Z_CMD_SAVOBJ	"SAVOBJ OBJ(%s) OBJTYPE(*%s) LIB(%s) DEV(*SAVF) SAVF(QTEMP/ZS%d) DTACPR(*HIGH)"
#define	Z_CMD_CPYTOSTMF	"CPYTOSTMF FROMMBR('/QSYS.LIB/QTEMP.LIB/ZS%d.FILE') TOSTMF('/tmp/zs%d.savf') STMFOPT(*REPLACE)"
#define Z_CMD_CPYFRMSTMF	"CPYFRMSTMF FROMSTMF('/tmp/zs%d.savf') TOMBR('/QSYS.LIB/QTEMP.LIB/ZS%d.FILE')"
#define Z_CMD_RSTOBJ	"RSTOBJ OBJ(*ALL) SAVLIB(%s) DEV(*SAVF) SAVF(QTEMP/ZS%d) MBROPT(*ALL) RSTLIB(%s)"

struct Z_object {
	char library[11];
	char object[11];
	char type[7];
};

struct Z_server {
	int sock;
	int datasock;
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
int Z_joblog(struct Z_server *server, char *output);
int Z_pasv(struct Z_server *server);
int Z_printf(struct Z_server *server, enum Z_outputtype flags, char *msg);
int Z_put(struct Z_server *server, char *remote, char *local);
int Z_quit(struct Z_server *server);
int Z_recvline(struct Z_server *server, char *destbuf,
	       size_t destbufsiz, int flags);
int Z_signon(struct Z_server *server);
int Z_system(struct Z_server *server, char *reqbuf);
int Z_write(struct Z_server *server, char *reqbuf);

#endif
