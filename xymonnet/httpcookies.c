/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2008-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: httpcookies.c 6745 2011-09-04 06:01:06Z storner $";

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "libxymon.h"
#include "httpcookies.h"

cookielist_t *cookiehead = NULL;

void * cookietree;
typedef struct hcookie_t {
	char *key;
	char *val;
} hcookie_t;

void init_session_cookies(char *urlhost, char *cknam, char *ckpath, char *ckval)
{
	hcookie_t *itm;

	itm = (hcookie_t *)malloc(sizeof(hcookie_t));
	itm->key = (char *)malloc(strlen(urlhost) + strlen(cknam) + strlen(ckpath) + 3);
	sprintf(itm->key, "%s\t%s\t%s", urlhost, cknam, ckpath);
	itm->val = strdup(ckval);
	xtreeAdd(cookietree, itm->key, itm);
}

void update_session_cookies(char *hostname, char *urlhost, char *headers)
{
	char *ckhdr, *onecookie;

	if (!headers) return;

	ckhdr = headers;
	do {
		ckhdr = strstr(ckhdr, "\nSet-Cookie:"); 
		if (ckhdr) {
			/* Set-Cookie: JSESSIONID=rKy8HZbLgT5W9N8; path=/ */
			char *eoln, *cknam, *ckval, *ckpath;

			cknam = ckval = ckpath = NULL;

			onecookie = strchr(ckhdr, ':') + 1; onecookie += strspn(onecookie, " \t");
			eoln = strchr(onecookie, '\n'); if (eoln) *eoln = '\0';
			ckhdr = (eoln ? eoln : NULL);
			onecookie = strdup(onecookie);
			if (eoln) *eoln = '\n';

			cknam = strtok(onecookie, "=");
			if (cknam) ckval = strtok(NULL, ";");
			if (ckval) {
				char *tok, *key;
				xtreePos_t h;
				hcookie_t *itm;

				do {
					tok = strtok(NULL, ";\r");
					if (tok) {
						tok += strspn(tok, " ");
						if (strncmp(tok, "path=", 5) == 0) {
							ckpath = tok+5;
						}
					}
				} while (tok);

				if (ckpath == NULL) ckpath = "/";
				key = (char *)malloc(strlen(urlhost) + strlen(cknam) + strlen(ckpath) + 3);
				sprintf(key, "%s\t%s\t%s", urlhost, cknam, ckpath);
				h = xtreeFind(cookietree, key);
				if (h == xtreeEnd(cookietree)) {
					itm = (hcookie_t *)malloc(sizeof(hcookie_t));
					itm->key = key;
					itm->val = strdup(ckval);
					xtreeAdd(cookietree, itm->key, itm);
				}
				else {
					itm = (hcookie_t *)xtreeData(cookietree, h);
					xfree(itm->val);
					itm->val = strdup(ckval);
					xfree(key);
				}
			}

			xfree(onecookie);
		}
	} while (ckhdr);
}

void save_session_cookies(void)
{
	FILE *fd = NULL;
	char cookiefn[PATH_MAX];
	xtreePos_t h;
	hcookie_t *itm;

	sprintf(cookiefn, "%s/etc/cookies.session", xgetenv("XYMONHOME"));
	fd = fopen(cookiefn, "w");
	if (fd == NULL) return;

	for (h=xtreeFirst(cookietree); (h != xtreeEnd(cookietree)); h = xtreeNext(cookietree, h)) {
		char *urlhost, *ckpath, *cknam;

		itm = (hcookie_t *)xtreeData(cookietree, h);
		urlhost = strtok(itm->key, "\t");
		cknam = strtok(NULL, "\t");
		ckpath = strtok(NULL, "\t");

		fprintf(fd, "%s\tFALSE\t%s\tFALSE\t0\t%s\t%s\n",
			urlhost, ckpath, cknam, itm->val);
	}

	fclose(fd);
}


/* This loads the cookies from the cookie-storage file */
static void load_cookies_one(char *cookiefn)
{
	FILE *fd;
	char l[4096];
	char *c_host, *c_path, *c_name, *c_value;
	int c_tailmatch, c_secure;
	time_t c_expire;
	char *p;

	fd = fopen(cookiefn, "r");
	if (fd == NULL) return;

	c_host = c_path = c_name = c_value = NULL;
	c_tailmatch = c_secure = 0;
	c_expire = 0;

	while (fgets(l, sizeof(l), fd)) {
		p = strchr(l, '\n'); 
		if (p) {
			*p = '\0';
			p--;
			if ((p > l) && (*p == '\r')) *p = '\0';
		}

		if ((l[0] != '#') && strlen(l)) {
			int fieldcount = 0;
			p = strtok(l, "\t");
			if (p) { fieldcount++; c_host = p; p = strtok(NULL, "\t"); }
			if (p) { fieldcount++; c_tailmatch = (strcmp(p, "TRUE") == 0); p = strtok(NULL, "\t"); }
			if (p) { fieldcount++; c_path = p; p = strtok(NULL, "\t"); }
			if (p) { fieldcount++; c_secure = (strcmp(p, "TRUE") == 0); p = strtok(NULL, "\t"); }
			if (p) { fieldcount++; c_expire = atol(p); p = strtok(NULL, "\t"); }
			if (p) { fieldcount++; c_name = p; p = strtok(NULL, "\t"); }
			if (p) { fieldcount++; c_value = p; p = strtok(NULL, "\t"); }
			if ((fieldcount == 7) && (c_expire > getcurrenttime(NULL))) {
				/* We have a valid cookie */
				cookielist_t *ck = (cookielist_t *)malloc(sizeof(cookielist_t));
				ck->host = strdup(c_host);
				ck->tailmatch = c_tailmatch;
				ck->path = strdup(c_path);
				ck->secure = c_secure;
				ck->name = strdup(c_name);
				ck->value = strdup(c_value);
				ck->next = cookiehead;
				cookiehead = ck;
			}
		}
	}

	fclose(fd);
}

void load_cookies(void)
{
	char cookiefn[PATH_MAX];

	sprintf(cookiefn, "%s/etc/cookies", xgetenv("XYMONHOME"));
	load_cookies_one(cookiefn);

	sprintf(cookiefn, "%s/etc/cookies.session", xgetenv("XYMONHOME"));
	load_cookies_one(cookiefn);
}


