/*----------------------------------------------------------------------------*/
/* Xymon communications tool.                                                 */
/*                                                                            */
/* This is used to send a single message using the Xymon/BB protocol to the   */
/* Xymon server.                                                              */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: xymon.c 6712 2011-07-31 21:01:52Z storner $";

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include "libxymon.h"

int main(int argc, char *argv[])
{
	int timeout = XYMON_TIMEOUT;
	int result = 1;
	int argi;
	int showhelp = 0;
	char *recipient = NULL;
	strbuffer_t *msg = newstrbuffer(0);
	FILE *respfd = stdout;
	char *envarea = NULL;
	sendreturn_t *sres;
	int wantresponse = 0, mergeinput = 0;

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strncmp(argv[argi], "--proxy=", 8) == 0) {
			char *p = strchr(argv[argi], '=');
			setproxy(p+1);
		}
		else if (strcmp(argv[argi], "--help") == 0) {
			showhelp = 1;
		}
		else if (strcmp(argv[argi], "--version") == 0) {
			fprintf(stdout, "Xymon version %s\n", VERSION);
			return 0;
		}
		else if (strcmp(argv[argi], "--str") == 0) {
			respfd = NULL;
		}
		else if (strncmp(argv[argi], "--out=", 6) == 0) {
			char *fn = argv[argi]+6;
			respfd = fopen(fn, "wb");
		}
		else if (strncmp(argv[argi], "--timeout=", 10) == 0) {
			char *p = strchr(argv[argi], '=');
			timeout = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--merge") == 0) {
			mergeinput = 1;
		}
		else if (strcmp(argv[argi], "--response") == 0) {
			wantresponse = 1;
		}
		else if (strcmp(argv[argi], "-?") == 0) {
			showhelp = 1;
		}
		else if ((*(argv[argi]) == '-') && (strlen(argv[argi]) > 1)) {
			fprintf(stderr, "Unknown option %s\n", argv[argi]);
		}
		else {
			/* No more options - pickup recipient and msg */
			if (recipient == NULL) {
				recipient = argv[argi];
			}
			else if (STRBUFLEN(msg) == 0) {
				msg = dupstrbuffer(argv[argi]);
			}
			else {
				showhelp=1;
			}
		}
	}

	if ((recipient == NULL) || (STRBUFLEN(msg) == 0) || showhelp) {
		fprintf(stderr, "Xymon version %s\n", VERSION);
		fprintf(stderr, "Usage: %s [--debug] [--merge] [--proxy=http://ip.of.the.proxy:port/] RECIPIENT DATA\n", argv[0]);
		fprintf(stderr, "  RECIPIENT: IP-address, hostname or URL\n");
		fprintf(stderr, "  DATA: Message to send, or \"-\" to read from stdin\n");
		return 1;
	}

	if (strcmp(STRBUF(msg), "-") == 0) {
		strbuffer_t *inpline = newstrbuffer(0);
		sres = newsendreturnbuf(0, NULL);

		initfgets(stdin);
		while (unlimfgets(inpline, stdin)) {
			result = sendmessage(STRBUF(inpline), recipient, timeout, sres);
			clearstrbuffer(inpline);
		}

		return result;
	}

	if (mergeinput || (strcmp(STRBUF(msg), "@") == 0)) {
		strbuffer_t *inpline = newstrbuffer(0);

		if (mergeinput) 
			/* Must add a new-line before the rest of the message */
			addtobuffer(msg, "\n");
		else
			/* Clear input buffer, we'll read it all from stdin */
			clearstrbuffer(msg);

		initfgets(stdin);
		while (unlimfgets(inpline, stdin)) addtostrbuffer(msg, inpline);
		freestrbuffer(inpline);
	}

	if      (strncmp(STRBUF(msg), "query ", 6) == 0) wantresponse = 1;
	else if (strncmp(STRBUF(msg), "client ", 7) == 0) wantresponse = 1;
	else if (strncmp(STRBUF(msg), "config ", 7) == 0) wantresponse = 1;
	else if (strncmp(STRBUF(msg), "download ", 9) == 0) wantresponse = 1;
	else if ((strncmp(STRBUF(msg), "xymondlog ", 10) == 0) || (strncmp(STRBUF(msg), "hobbitdlog ", 11) == 0)) wantresponse = 1;
	else if ((strncmp(STRBUF(msg), "xymondxlog ", 11) == 0) || (strncmp(STRBUF(msg), "hobbitdxlog ", 12) == 0)) wantresponse = 1;
	else if ((strncmp(STRBUF(msg), "xymondboard", 11) == 0) || (strncmp(STRBUF(msg), "hobbitdboard", 12) == 0)) wantresponse = 1;
	else if ((strncmp(STRBUF(msg), "xymondxboard", 12) == 0) || (strncmp(STRBUF(msg), "hobbitdxboard", 13) == 0)) wantresponse = 1;
	else if (strncmp(STRBUF(msg), "schedule", 8) == 0) wantresponse = 1;
	else if (strncmp(STRBUF(msg), "clientlog ", 10) == 0) wantresponse = 1;
	else if (strncmp(STRBUF(msg), "hostinfo", 8) == 0) wantresponse = 1;
	else if (strncmp(STRBUF(msg), "ping", 4) == 0) wantresponse = 1;
	else if (strncmp(STRBUF(msg), "pullclient", 10) == 0) wantresponse = 1;
	else if (strncmp(STRBUF(msg), "ghostlist", 9) == 0) wantresponse = 1;
	else if (strncmp(STRBUF(msg), "multisrclist", 12) == 0) wantresponse = 1;

	sres = newsendreturnbuf(wantresponse, respfd);
	result = sendmessage(STRBUF(msg), recipient, timeout, sres);

	if (sres->respstr) printf("Buffered response is '%s'\n", STRBUF(sres->respstr));

	return result;
}

