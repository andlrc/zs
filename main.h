#ifndef _H_MAIN_
#define _H_MAIN_ 1

#define PROGRAM_NAME	"zs"
#define	PROGRAM_VERSION	"0.3"
#define	PROGRAM_USAGE	"Usage " PROGRAM_NAME " { "			\
	"{ -s=server | -u=user | -p=password | -c=file }...\n"		\
	"\t   { -S=server | -U=user | -P=password | -C=file }...\n"	\
	"\t   { LIB/OBJ [ *TYPE ] }...\n"				\
	"\t   | --help | --version }\n"

#define	CMD_SAVE1	"CRTSAVF FILE(QTEMP/ZS%d)"
#define CMD_SAVE2	"SAVOBJ OBJ(%s) OBJTYPE(*%s) LIB(%s) DEV(*SAVF) SAVF(QTEMP/ZS%d) DTACPR(*HIGH)"
#define	CMD_SAVE3	"CPYTOSTMF FROMMBR('/QSYS.LIB/QTEMP.LIB/ZS%d.FILE') TOSTMF('/tmp/zs%d.savf') STMFOPT(*REPLACE)"
#define CMD_RESTORE1	"CPYFRMSTMF FROMSTMF('/tmp/zs%d.savf') TOMBR('/QSYS.LIB/QTEMP.LIB/ZS%d.FILE')"
#define CMD_RESTORE2	"RSTOBJ OBJ(*ALL) SAVLIB(%s) DEV(*SAVF) SAVF(QTEMP/ZS%d) MBROPT(*ALL) RSTLIB(%s)"

#endif
