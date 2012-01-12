/*----------------------------------------------------------------------------*/
/* Xymon hostlist report generator.                                           */
/*                                                                            */
/* Copyright (C) 2007-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hostlist.c 6766 2011-10-13 11:55:42Z storner $";

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "libxymon.h"

cgidata_t *cgidata = NULL;
char *pagefilter = NULL;
char *testfilter = "cpu";
enum { SORT_IP, SORT_HOSTNAME } sortkey = SORT_HOSTNAME;
char *fields = NULL;
char csvchar = ',';

void parse_query(void)
{
	cgidata_t *cwalk;

	fields = strdup("XMH_HOSTNAME,XMH_IP");
	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */

		if (strcasecmp(cwalk->name, "page") == 0) {
			pagefilter = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "filter") == 0) {
			testfilter = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "sort") == 0) {
			if (strcasecmp(cwalk->value, "hostname") == 0)
				sortkey = SORT_HOSTNAME;
			else if (strcasecmp(cwalk->value, "ip") == 0)
				sortkey = SORT_IP;
		}
		else if (strncasecmp(cwalk->name, "XMH_", 4) == 0) {
			if (strcasecmp(cwalk->value, "on") == 0) {
				fields = (char *)realloc(fields, strlen(fields) + strlen(cwalk->name) + 2);
				strcat(fields, ",");
				strcat(fields, cwalk->name);
			}
		}
		else if (strcasecmp(cwalk->name, "csvdelim") == 0) {
			csvchar = *(cwalk->value);
		}

		cwalk = cwalk->next;
	}
}


int main(int argc, char *argv[])
{
	char *envarea = NULL;
	char *req, *board, *l;
	int argi, res;
	sendreturn_t *sres;
	char *cookie;
	pcre *dummy;

	init_timestamp();
	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
	}

	cookie = get_cookie("pagepath");
	if (cookie) sethostenv_pagepath(cookie);

	cgidata = cgi_request();
	if (cgidata == NULL) {
		/* Present the query form */
		sethostenv("", "", "", colorname(COL_BLUE), NULL);
		printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		showform(stdout, "hostlist", "hostlist_form", COL_BLUE, getcurrenttime(NULL), NULL, NULL);
		return 0;
	}
	parse_query();

	dummy = (testfilter ? compileregex(testfilter) : NULL);
	if (dummy == NULL) return 1; else freeregex(dummy);
	dummy = (pagefilter ? compileregex(pagefilter) : NULL);
	if (dummy == NULL) return 1; else freeregex(dummy);

	sres = newsendreturnbuf(1, NULL);
	req = malloc(1024 + strlen(fields) + strlen(testfilter) + strlen(pagefilter));
	sprintf(req, "xymondboard fields=%s test=%s page=%s",
		fields, testfilter, pagefilter);
	res = sendmessage(req, NULL, XYMON_TIMEOUT, sres);
	if (res != XYMONSEND_OK) return 1;
	board = getsendreturnstr(sres, 1);
	freesendreturnbuf(sres);

	printf("Content-type: text/csv\n\n");
	l = strtok(fields, ",");
	while (l) {
		printf("%s;", l);
		l = strtok(NULL, ",\n");
	}
	printf("\n");

	l = board;
	while (l && *l) {
		char *p;
		char *eoln = strchr(l, '\n');
		if (eoln) *eoln = '\0';

		do {
			p = strchr(l, '|');
			if (p) *p = csvchar;
		} while (p);
		printf("%s\n", l);

		if (eoln) l = eoln+1; else l = NULL;
	}

	return 0;
}

