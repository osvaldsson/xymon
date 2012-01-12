/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for handling header- and footer-files.                */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: headfoot.c 6745 2011-09-04 06:01:06Z storner $";

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <pcre.h>

#include "libxymon.h"
#include "version.h"

/* Stuff for headfoot - variables we can set dynamically */
static char *hostenv_hikey = NULL;
static char *hostenv_host = NULL;
static char *hostenv_ip = NULL;
static char *hostenv_svc = NULL;
static char *hostenv_color = NULL;
static char *hostenv_pagepath = NULL;

static time_t hostenv_reportstart = 0;
static time_t hostenv_reportend = 0;

static char *hostenv_repwarn = NULL;
static char *hostenv_reppanic = NULL;

static time_t hostenv_snapshot = 0;
static char *hostenv_logtime = NULL;
static char *hostenv_templatedir = NULL;
static int hostenv_refresh = 60;

static char *statusboard = NULL;
static char *scheduleboard = NULL;

static char *hostpattern_text = NULL;
static pcre *hostpattern = NULL;
static char *pagepattern_text = NULL;
static pcre *pagepattern = NULL;
static char *ippattern_text = NULL;
static pcre *ippattern = NULL;
static void * hostnames;
static void * testnames;

typedef struct treerec_t {
	char *name;
	int flag;
} treerec_t;

static int backdays = 0, backhours = 0, backmins = 0, backsecs = 0;
static char hostenv_eventtimestart[20];
static char hostenv_eventtimeend[20];

typedef struct listrec_t {
	char *name, *val, *extra;
	int selected;
	struct listrec_t *next;
} listrec_t;
typedef struct listpool_t {
	char *name;
	struct listrec_t *listhead, *listtail;
	struct listpool_t *next;
} listpool_t;
static listpool_t *listpoolhead = NULL;

typedef struct bodystorage_t {
	char *id;
	strbuffer_t *txt;
} bodystorage_t;


static void clearflags(void * tree)
{
	xtreePos_t handle;
	treerec_t *rec;

	if (!tree) return;

	for (handle = xtreeFirst(tree); (handle != xtreeEnd(tree)); handle = xtreeNext(tree, handle)) {
		rec = (treerec_t *)xtreeData(tree, handle);
		rec->flag = 0;
	}
}

void sethostenv(char *host, char *ip, char *svc, char *color, char *hikey)
{
	if (hostenv_hikey) xfree(hostenv_hikey);
	if (hostenv_host)  xfree(hostenv_host);
	if (hostenv_ip)    xfree(hostenv_ip);
	if (hostenv_svc)   xfree(hostenv_svc);
	if (hostenv_color) xfree(hostenv_color);

	hostenv_hikey = (hikey ? strdup(htmlquoted(hikey)) : NULL);
	hostenv_host = strdup(htmlquoted(host));
	hostenv_ip = strdup(htmlquoted(ip));
	hostenv_svc = strdup(htmlquoted(svc));
	hostenv_color = strdup(color);
}

void sethostenv_report(time_t reportstart, time_t reportend, double repwarn, double reppanic)
{
	if (hostenv_repwarn == NULL) hostenv_repwarn = malloc(10);
	if (hostenv_reppanic == NULL) hostenv_reppanic = malloc(10);

	hostenv_reportstart = reportstart;
	hostenv_reportend = reportend;

	sprintf(hostenv_repwarn, "%.2f", repwarn);
	sprintf(hostenv_reppanic, "%.2f", reppanic);
}

void sethostenv_snapshot(time_t snapshot)
{
	hostenv_snapshot = snapshot;
}

void sethostenv_histlog(char *histtime)
{
	if (hostenv_logtime) xfree(hostenv_logtime);
	hostenv_logtime = strdup(histtime);
}

void sethostenv_template(char *dir)
{
	if (hostenv_templatedir) xfree(hostenv_templatedir);
	hostenv_templatedir = strdup(dir);
}

void sethostenv_refresh(int n)
{
	hostenv_refresh = n;
}

void sethostenv_pagepath(char *s)
{
	if (!s) return;
	if (hostenv_pagepath) xfree(hostenv_pagepath);
	hostenv_pagepath = strdup(s);
}

void sethostenv_filter(char *hostptn, char *pageptn, char *ipptn)
{
	const char *errmsg;
	int errofs;

	if (hostpattern_text) xfree(hostpattern_text);
	if (hostpattern) { pcre_free(hostpattern); hostpattern = NULL; }
	if (pagepattern_text) xfree(pagepattern_text);
	if (pagepattern) { pcre_free(pagepattern); pagepattern = NULL; }
	if (ippattern_text) xfree(ippattern_text);
	if (ippattern) { pcre_free(ippattern); ippattern = NULL; }

	/* Setup the pattern to match names against */
	if (hostptn) {
		hostpattern_text = strdup(hostptn);
		hostpattern = pcre_compile(hostptn, PCRE_CASELESS, &errmsg, &errofs, NULL);
	}
	if (pageptn) {
		pagepattern_text = strdup(pageptn);
		pagepattern = pcre_compile(pageptn, PCRE_CASELESS, &errmsg, &errofs, NULL);
	}
	if (ipptn) {
		ippattern_text = strdup(ipptn);
		ippattern = pcre_compile(ipptn, PCRE_CASELESS, &errmsg, &errofs, NULL);
	}
}

static listpool_t *find_listpool(char *listname)
{
	listpool_t *pool = NULL;
	listrec_t *zombie;

	if (!listname) listname = "";
	for (pool = listpoolhead; (pool && strcmp(pool->name, listname)); pool = pool->next);
	if (!pool) {
		pool = (listpool_t *)calloc(1, sizeof(listpool_t));
		pool->name = strdup(listname);
		pool->next = listpoolhead;
		listpoolhead = pool;
	}

	return pool;
}

void sethostenv_clearlist(char *listname)
{
	listpool_t *pool = NULL;
	listrec_t *zombie;

	pool = find_listpool(listname);
	while (pool->listhead) {
		zombie = pool->listhead;
		pool->listhead = pool->listhead->next;

		xfree(zombie->name); xfree(zombie->val); xfree(zombie);
	}
}

void sethostenv_addtolist(char *listname, char *name, char *val, char *extra, int selected)
{
	listpool_t *pool = NULL;
	listrec_t *newitem = (listrec_t *)calloc(1, sizeof(listrec_t));

	pool = find_listpool(listname);
	newitem->name = strdup(name);
	newitem->val = strdup(val);
	newitem->extra = (extra ? strdup(extra) : NULL);
	newitem->selected = selected;
	if (pool->listtail) {
		pool->listtail->next = newitem;
		pool->listtail = newitem;
	}
	else {
		pool->listhead = pool->listtail = newitem;
	}
}

static int critackttprio = 0;
static char *critackttgroup = NULL;
static char *critackttextra = NULL;
static char *ackinfourl = NULL;
static char *critackdocurl = NULL;

void sethostenv_critack(int prio, char *ttgroup, char *ttextra, char *infourl, char *docurl)
{
	critackttprio = prio;
	if (critackttgroup) xfree(critackttgroup); critackttgroup = strdup((ttgroup && *ttgroup) ? ttgroup : "&nbsp;");
	if (critackttextra) xfree(critackttextra); critackttextra = strdup((ttextra && *ttextra) ? ttextra : "&nbsp;");
	if (ackinfourl) xfree(ackinfourl); ackinfourl = strdup(infourl);
	if (critackdocurl) xfree(critackdocurl); critackdocurl = strdup((docurl && *docurl) ? docurl : "");
}

static char *criteditupdinfo = NULL;
static int criteditprio = -1;
static char *criteditgroup = NULL;
static time_t criteditstarttime = 0;
static time_t criteditendtime = 0;
static char *criteditextra = NULL;
static char *criteditslawkdays = NULL;
static char *criteditslastart = NULL;
static char *criteditslaend = NULL;
static char **criteditclonelist = NULL;
static int criteditclonesize = 0;

void sethostenv_critedit(char *updinfo, int prio, char *group, time_t starttime, time_t endtime, char *crittime, char *extra)
{
	char *p;

	if (criteditupdinfo) xfree(criteditupdinfo);
	criteditupdinfo = strdup(updinfo);

	criteditprio = prio;
	criteditstarttime = starttime;
	criteditendtime = endtime;

	if (criteditgroup) xfree(criteditgroup);
	criteditgroup = strdup(group ? group : "");

	if (criteditextra) xfree(criteditextra);
	criteditextra = strdup(extra ? extra : "");

	if (criteditslawkdays) xfree(criteditslawkdays);
	criteditslawkdays = criteditslastart = criteditslaend = NULL;

	if (crittime) {
		criteditslawkdays = strdup(crittime);
		p = strchr(criteditslawkdays, ':');
		if (p) {
			*p = '\0';
			criteditslastart = p+1;

			p = strchr(criteditslastart, ':');
			if (p) {
				*p = '\0';
				criteditslaend = p+1;
			}
		}

		if (criteditslawkdays && (!criteditslastart || !criteditslaend)) {
			xfree(criteditslawkdays);
			criteditslawkdays = criteditslastart = criteditslaend = NULL;
		}
	}
}

void sethostenv_critclonelist_clear(void)
{
	int i;

	if (criteditclonelist) {
		for (i=0; (criteditclonelist[i]); i++) xfree(criteditclonelist[i]);
		xfree(criteditclonelist);
	}
	criteditclonelist = malloc(sizeof(char *));
	criteditclonelist[0] = NULL;
	criteditclonesize = 0;
}

void sethostenv_critclonelist_add(char *hostname)
{
	char *p;

	criteditclonelist = (char **)realloc(criteditclonelist, (criteditclonesize + 2)*sizeof(char *));
	criteditclonelist[criteditclonesize] = strdup(hostname);
	p = criteditclonelist[criteditclonesize];
	criteditclonelist[++criteditclonesize] = NULL;

	p += (strlen(p) - 1);
	if (*p == '=') *p = '\0';
}


void sethostenv_backsecs(int seconds)
{
	backdays = seconds / 86400; seconds -= backdays*86400;
	backhours = seconds / 3600; seconds -= backhours*3600;
	backmins = seconds / 60; seconds -= backmins*60;
	backsecs = seconds;
}

void sethostenv_eventtime(time_t starttime, time_t endtime)
{
	*hostenv_eventtimestart = *hostenv_eventtimeend = '\0';
	if (starttime) strftime(hostenv_eventtimestart, sizeof(hostenv_eventtimestart), "%Y/%m/%d@%H:%M:%S", localtime(&starttime));
	if (endtime) strftime(hostenv_eventtimeend, sizeof(hostenv_eventtimeend), "%Y/%m/%d@%H:%M:%S", localtime(&endtime));
}

char *wkdayselect(char wkday, char *valtxt, int isdefault)
{
	static char result[100];
	char *selstr;

	if (!criteditslawkdays) {
		if (isdefault) selstr = "SELECTED";
		else selstr = "";
	}
	else {
		if (strchr(criteditslawkdays, wkday)) selstr = "SELECTED";
		else selstr = "";
	}

	sprintf(result, "<option value=\"%c\" %s>%s</option>\n", wkday, selstr, valtxt);

	return result;
}


static void *wanted_host(char *hostname)
{
	void *hinfo = hostinfo(hostname);
	int result, ovector[30];

	if (!hinfo) return NULL;

	if (hostpattern) {
		result = pcre_exec(hostpattern, NULL, hostname, strlen(hostname), 0, 0,
				ovector, (sizeof(ovector)/sizeof(int)));
		if (result < 0) return NULL;
	}

	if (pagepattern && hinfo) {
		char *pname = xmh_item(hinfo, XMH_PAGEPATH);
		result = pcre_exec(pagepattern, NULL, pname, strlen(pname), 0, 0,
				ovector, (sizeof(ovector)/sizeof(int)));
		if (result < 0) return NULL;
	}

	if (ippattern && hinfo) {
		char *hostip = xmh_item(hinfo, XMH_IP);
		result = pcre_exec(ippattern, NULL, hostip, strlen(hostip), 0, 0,
				ovector, (sizeof(ovector)/sizeof(int)));
		if (result < 0) return NULL;
	}

	return hinfo;
}


static void fetch_board(void)
{
	static int haveboard = 0;
	char *walk, *eoln;
	sendreturn_t *sres;

	if (haveboard) return;

	sres = newsendreturnbuf(1, NULL);
	if (sendmessage("xymondboard fields=hostname,testname,disabletime,dismsg", 
			NULL, XYMON_TIMEOUT, sres) != XYMONSEND_OK) {
		freesendreturnbuf(sres);
		return;
	}

	haveboard = 1;
	statusboard = getsendreturnstr(sres, 1);
	freesendreturnbuf(sres);

	hostnames = xtreeNew(strcasecmp);
	testnames = xtreeNew(strcasecmp);
	walk = statusboard;
	while (walk) {
		eoln = strchr(walk, '\n'); if (eoln) *eoln = '\0';
		if (strlen(walk) && (strncmp(walk, "summary|", 8) != 0)) {
			char *buf, *hname = NULL, *tname = NULL;
			treerec_t *newrec;

			buf = strdup(walk);

			hname = gettok(buf, "|");

			if (hname && wanted_host(hname) && hostinfo(hname)) {
				newrec = (treerec_t *)malloc(sizeof(treerec_t));
				newrec->name = strdup(hname);
				newrec->flag = 0;
				xtreeAdd(hostnames, newrec->name, newrec);

				tname = gettok(NULL, "|");
				if (tname) {
					newrec = (treerec_t *)malloc(sizeof(treerec_t));
					newrec->name = strdup(tname);
					newrec->flag = 0;
					xtreeAdd(testnames, strdup(tname), newrec);
				}
			}

			xfree(buf);
		}

		if (eoln) {
			*eoln = '\n';
			walk = eoln + 1;
		}
		else
			walk = NULL;
	}

	sres = newsendreturnbuf(1, NULL);
	if (sendmessage("schedule", NULL, XYMON_TIMEOUT, sres) != XYMONSEND_OK) {
		freesendreturnbuf(sres);
		return;
	}

	scheduleboard = getsendreturnstr(sres, 1);
	freesendreturnbuf(sres);
}

static char *eventreport_timestring(time_t timestamp)
{
	static char result[20];

	strftime(result, sizeof(result), "%Y/%m/%d@%H:%M:%S", localtime(&timestamp));
	return result;
}

static void build_pagepath_dropdown(FILE *output)
{
	void * ptree;
	void *hwalk;
	xtreePos_t handle;

	ptree = xtreeNew(strcmp);

	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		char *path = xmh_item(hwalk, XMH_PAGEPATH);
		char *ptext;

		handle = xtreeFind(ptree, path);
		if (handle != xtreeEnd(ptree)) continue;

		ptext = xmh_item(hwalk, XMH_PAGEPATHTITLE);
		xtreeAdd(ptree, ptext, path);
	}

	for (handle = xtreeFirst(ptree); (handle != xtreeEnd(ptree)); handle = xtreeNext(ptree, handle)) {
		fprintf(output, "<option value=\"%s\">%s</option>\n", (char *)xtreeData(ptree, handle), xtreeKey(ptree, handle));
	}

	xtreeDestroy(ptree);
}

char *xymonbody(char *id)
{
	static void * bodystorage;
	static int firsttime = 1;
	xtreePos_t handle;
	bodystorage_t *bodyelement;

	strbuffer_t *rawdata, *parseddata;
	char *envstart, *envend, *outpos;
	char *idtag, *idval;
	int idtaglen;

	if (firsttime) {
		bodystorage = xtreeNew(strcmp);
		firsttime = 0;
	}

	idtaglen = strspn(id, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	idtag = (char *)malloc(idtaglen + 1);
	strncpy(idtag, id, idtaglen);
	*(idtag+idtaglen) = '\0';

	handle = xtreeFind(bodystorage, idtag);
	if (handle != xtreeEnd(bodystorage)) {
		bodyelement = (bodystorage_t *)xtreeData(bodystorage, handle);
		xfree(idtag);
		return STRBUF(bodyelement->txt);
	}

	rawdata = newstrbuffer(0);
	idval = xgetenv(idtag);
	if (idval == NULL) return "";

	if (strncmp(idval, "file:", 5) == 0) {
		FILE *fd;
		strbuffer_t *inbuf = newstrbuffer(0);

		fd = stackfopen(idval+5, "r", NULL);
		if (fd != NULL) {
			while (stackfgets(inbuf, NULL)) addtostrbuffer(rawdata, inbuf);
			stackfclose(fd);
		}

		freestrbuffer(inbuf);
	}
	else {
		addtobuffer(rawdata, idval);
	}

	/* Output the body data, but expand any environment variables along the way */
	parseddata = newstrbuffer(0);
	outpos = STRBUF(rawdata);
	while (*outpos) {
		envstart = strchr(outpos, '$');
		if (envstart) {
			char savechar;
			char *envval = NULL;

			*envstart = '\0';
			addtobuffer(parseddata, outpos);

			envstart++;
			envend = envstart + strspn(envstart, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
			savechar = *envend; *envend = '\0';
			if (*envstart) envval = xgetenv(envstart);

			*envend = savechar;
			outpos = envend;

			if (envval) {
				addtobuffer(parseddata, envval);
			}
			else {
				addtobuffer(parseddata, "$");
				addtobuffer(parseddata, envstart);
			}
		}
		else {
			addtobuffer(parseddata, outpos);
			outpos += strlen(outpos);
		}
	}

	freestrbuffer(rawdata);

	bodyelement = (bodystorage_t *)calloc(1, sizeof(bodystorage_t));
	bodyelement->id = idtag;
	bodyelement->txt = parseddata;
	xtreeAdd(bodystorage, bodyelement->id, bodyelement);

	return STRBUF(bodyelement->txt);
}

typedef struct distest_t {
	char *name;
	char *cause;
	time_t until;
	struct distest_t *next;
} distest_t;

typedef struct dishost_t {
	char *name;
	struct distest_t *tests;
	struct dishost_t *next;
} dishost_t;

void output_parsed(FILE *output, char *templatedata, int bgcolor, time_t selectedtime)
{
	char	*t_start, *t_next;
	char	savechar;
	time_t	now = getcurrenttime(NULL);
	time_t  yesterday = getcurrenttime(NULL) - 86400;
	struct  tm *nowtm;

	for (t_start = templatedata, t_next = strchr(t_start, '&'); (t_next); ) {
		/* Copy from t_start to t_next unchanged */
		*t_next = '\0'; t_next++;
		fprintf(output, "%s", t_start);

		/* Find token */
		t_start = t_next;
		/* Dont include lower-case letters - reserve those for eg "&nbsp;" */
		t_next += strspn(t_next, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
		savechar = *t_next; *t_next = '\0';

		if ((strcmp(t_start, "XYMWEBDATE") == 0) || (strcmp(t_start, "BBDATE") == 0)) {
			char *datefmt = xgetenv("XYMONDATEFORMAT");
			char datestr[100];

			MEMDEFINE(datestr);

			/*
			 * If no XYMONDATEFORMAT setting, use a format string that
			 * produces output similar to that from ctime()
			 */
			if (datefmt == NULL) datefmt = "%a %b %d %H:%M:%S %Y\n";

			if (hostenv_reportstart != 0) {
				char starttime[20], endtime[20];

				MEMDEFINE(starttime); MEMDEFINE(endtime);

				strftime(starttime, sizeof(starttime), "%b %d %Y", localtime(&hostenv_reportstart));
				strftime(endtime, sizeof(endtime), "%b %d %Y", localtime(&hostenv_reportend));
				if (strcmp(starttime, endtime) == 0)
					fprintf(output, "%s", starttime);
				else
					fprintf(output, "%s - %s", starttime, endtime);

				MEMUNDEFINE(starttime); MEMUNDEFINE(endtime);
			}
			else if (hostenv_snapshot != 0) {
				strftime(datestr, sizeof(datestr), datefmt, localtime(&hostenv_snapshot));
				fprintf(output, "%s", datestr);
			}
			else {
				strftime(datestr, sizeof(datestr), datefmt, localtime(&now));
				fprintf(output, "%s", datestr);
			}

			MEMUNDEFINE(datestr);
		}

		else if ((strcmp(t_start, "XYMWEBBACKGROUND") == 0) || (strcmp(t_start, "BBBACKGROUND") == 0)) {
			fprintf(output, "%s", colorname(bgcolor));
		}
		else if ((strcmp(t_start, "XYMWEBCOLOR") == 0) || (strcmp(t_start, "BBCOLOR") == 0))
			fprintf(output, "%s", hostenv_color);
		else if ((strcmp(t_start, "XYMWEBSVC") == 0) || (strcmp(t_start, "BBSVC") == 0))
			fprintf(output, "%s", hostenv_svc);
		else if ((strcmp(t_start, "XYMWEBHOST") == 0) || (strcmp(t_start, "BBHOST") == 0))
			fprintf(output, "%s", hostenv_host);
		else if ((strcmp(t_start, "XYMWEBHIKEY") == 0) || (strcmp(t_start, "BBHIKEY") == 0))
			fprintf(output, "%s", (hostenv_hikey ? hostenv_hikey : hostenv_host));
		else if ((strcmp(t_start, "XYMWEBIP") == 0) || (strcmp(t_start, "BBIP") == 0))
			fprintf(output, "%s", hostenv_ip);
		else if ((strcmp(t_start, "XYMWEBIPNAME") == 0) || (strcmp(t_start, "BBIPNAME") == 0)) {
			if (strcmp(hostenv_ip, "0.0.0.0") == 0)  fprintf(output, "%s", hostenv_host);
			else fprintf(output, "%s", hostenv_ip);
		}
		else if ((strcmp(t_start, "XYMONREPWARN") == 0) || (strcmp(t_start, "BBREPWARN") == 0))
			fprintf(output, "%s", hostenv_repwarn);
		else if ((strcmp(t_start, "XYMONREPPANIC") == 0) || (strcmp(t_start, "BBREPPANIC") == 0))
			fprintf(output, "%s", hostenv_reppanic);
		else if (strcmp(t_start, "LOGTIME") == 0) 	 fprintf(output, "%s", (hostenv_logtime ? hostenv_logtime : ""));
		else if ((strcmp(t_start, "XYMWEBREFRESH") == 0) || (strcmp(t_start, "BBREFRESH") == 0))
			fprintf(output, "%d", hostenv_refresh);
		else if ((strcmp(t_start, "XYMWEBPAGEPATH") == 0) || (strcmp(t_start, "BBPAGEPATH") == 0))
			fprintf(output, "%s", (hostenv_pagepath ? hostenv_pagepath : ""));

		else if (strcmp(t_start, "REPMONLIST") == 0) {
			int i;
			struct tm monthtm;
			char mname[20];
			char *selstr;

			MEMDEFINE(mname);

			nowtm = localtime(&selectedtime);
			for (i=1; (i <= 12); i++) {
				if (i == (nowtm->tm_mon + 1)) selstr = "SELECTED"; else selstr = "";
				monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm->tm_year;
				monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
				strftime(mname, sizeof(mname)-1, "%B", &monthtm);
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%s\n", i, selstr, mname);
			}

			MEMUNDEFINE(mname);
		}
		else if (strcmp(t_start, "MONLIST") == 0) {
			int i;
			struct tm monthtm;
			char mname[20];

			MEMDEFINE(mname);

			nowtm = localtime(&selectedtime);
			for (i=1; (i <= 12); i++) {
				monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm->tm_year;
				monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
				strftime(mname, sizeof(mname)-1, "%B", &monthtm);
				fprintf(output, "<OPTION VALUE=\"%d\">%s\n", i, mname);
			}

			MEMUNDEFINE(mname);
		}
		else if (strcmp(t_start, "REPWEEKLIST") == 0) {
			int i;
			char weekstr[5];
			int weeknum;
			char *selstr;

			nowtm = localtime(&selectedtime);
			strftime(weekstr, sizeof(weekstr)-1, "%V", nowtm); weeknum = atoi(weekstr);
			for (i=1; (i <= 53); i++) {
				if (i == weeknum) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "REPDAYLIST") == 0) {
			int i;
			char *selstr;

			nowtm = localtime(&selectedtime);
			for (i=1; (i <= 31); i++) {
				if (i == nowtm->tm_mday) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "DAYLIST") == 0) {
			int i;

			nowtm = localtime(&selectedtime);
			for (i=1; (i <= 31); i++) {
				fprintf(output, "<OPTION VALUE=\"%d\">%d\n", i, i);
			}
		}
		else if (strcmp(t_start, "REPYEARLIST") == 0) {
			int i;
			char *selstr;
			int beginyear, endyear;

			nowtm = localtime(&selectedtime);
			beginyear = nowtm->tm_year + 1900 - 5;
			endyear = nowtm->tm_year + 1900;

			for (i=beginyear; (i <= endyear); i++) {
				if (i == (nowtm->tm_year + 1900)) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "FUTUREYEARLIST") == 0) {
			int i;
			char *selstr;
			int beginyear, endyear;

			nowtm = localtime(&selectedtime);
			beginyear = nowtm->tm_year + 1900;
			endyear = nowtm->tm_year + 1900 + 5;

			for (i=beginyear; (i <= endyear); i++) {
				if (i == (nowtm->tm_year + 1900)) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "YEARLIST") == 0) {
			int i;
			int beginyear, endyear;

			nowtm = localtime(&selectedtime);
			beginyear = nowtm->tm_year + 1900;
			endyear = nowtm->tm_year + 1900 + 5;

			for (i=beginyear; (i <= endyear); i++) {
				fprintf(output, "<OPTION VALUE=\"%d\">%d\n", i, i);
			}
		}
		else if (strcmp(t_start, "REPHOURLIST") == 0) { 
			int i; 
			struct tm *nowtm = localtime(&yesterday); 
			char *selstr;

			for (i=0; (i <= 24); i++) {
				if (i == nowtm->tm_hour) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "HOURLIST") == 0) { 
			int i; 

			for (i=0; (i <= 24); i++) {
				fprintf(output, "<OPTION VALUE=\"%d\">%d\n", i, i);
			}
		}
		else if (strcmp(t_start, "REPMINLIST") == 0) {
			int i;
			struct tm *nowtm = localtime(&yesterday);
			char *selstr;

			for (i=0; (i <= 59); i++) {
				if (i == nowtm->tm_min) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%02d\" %s>%02d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "MINLIST") == 0) {
			int i;

			for (i=0; (i <= 59); i++) {
				fprintf(output, "<OPTION VALUE=\"%02d\">%02d\n", i, i);
			}
		}
		else if (strcmp(t_start, "REPSECLIST") == 0) {
			int i;
			char *selstr;

			for (i=0; (i <= 59); i++) {
				if (i == 0) selstr = "SELECTED"; else selstr = "";
				fprintf(output, "<OPTION VALUE=\"%02d\" %s>%02d\n", i, selstr, i);
			}
		}
		else if (strcmp(t_start, "HOSTFILTER") == 0) {
			if (hostpattern_text) fprintf(output, "%s", hostpattern_text);
		}
		else if (strcmp(t_start, "PAGEFILTER") == 0) {
			if (pagepattern_text) fprintf(output, "%s", pagepattern_text);
		}
		else if (strcmp(t_start, "IPFILTER") == 0) {
			if (ippattern_text) fprintf(output, "%s", ippattern_text);
		}
		else if (strcmp(t_start, "HOSTLIST") == 0) {
			xtreePos_t handle;
			treerec_t *rec;

			fetch_board();

			for (handle = xtreeFirst(hostnames); (handle != xtreeEnd(hostnames)); handle = xtreeNext(hostnames, handle)) {
				rec = (treerec_t *)xtreeData(hostnames, handle);

				if (wanted_host(rec->name)) {
					fprintf(output, "<OPTION VALUE=\"%s\">%s</OPTION>\n", rec->name, rec->name);
				}
			}
		}
		else if (strcmp(t_start, "JSHOSTLIST") == 0) {
			xtreePos_t handle;

			fetch_board();
			clearflags(testnames);

			fprintf(output, "var hosts = new Array();\n");
			fprintf(output, "hosts[\"ALL\"] = [ \"ALL\"");
			for (handle = xtreeFirst(testnames); (handle != xtreeEnd(testnames)); handle = xtreeNext(testnames, handle)) {
				treerec_t *rec = xtreeData(testnames, handle);
				fprintf(output, ", \"%s\"", rec->name);
			}
			fprintf(output, " ];\n");

			for (handle = xtreeFirst(hostnames); (handle != xtreeEnd(hostnames)); handle = xtreeNext(hostnames, handle)) {
				treerec_t *hrec = xtreeData(hostnames, handle);
				if (wanted_host(hrec->name)) {
					xtreePos_t thandle;
					treerec_t *trec;
					char *bwalk, *tname, *p;
					char *key = (char *)malloc(strlen(hrec->name) + 3);

					/* Setup the search key and find the first occurrence. */
					sprintf(key, "\n%s|", hrec->name);
					if (strncmp(statusboard, (key+1), strlen(key+1)) == 0)
						bwalk = statusboard;
					else {
						bwalk = strstr(statusboard, key);
						if (bwalk) bwalk++;
					}

					while (bwalk) {
						tname = bwalk + strlen(key+1);
						p = strchr(tname, '|'); if (p) *p = '\0';
						if ( (strcmp(tname, xgetenv("INFOCOLUMN")) != 0) &&
						     (strcmp(tname, xgetenv("TRENDSCOLUMN")) != 0) ) {
							thandle = xtreeFind(testnames, tname);
							if (thandle != xtreeEnd(testnames)) {
								trec = (treerec_t *)xtreeData(testnames, thandle);
								trec->flag = 1;
							}
						}
						if (p) *p = '|';

						bwalk = strstr(tname, key); if (bwalk) bwalk++;
					}

					fprintf(output, "hosts[\"%s\"] = [ \"ALL\"", hrec->name);
					for (thandle = xtreeFirst(testnames); (thandle != xtreeEnd(testnames)); thandle = xtreeNext(testnames, thandle)) {
						trec = (treerec_t *)xtreeData(testnames, thandle);
						if (trec->flag == 0) continue;

						trec->flag = 0;
						fprintf(output, ", \"%s\"", trec->name);
					}
					fprintf(output, " ];\n");
				}
			}
		}
		else if (strcmp(t_start, "TESTLIST") == 0) {
			xtreePos_t handle;
			treerec_t *rec;

			fetch_board();

			for (handle = xtreeFirst(testnames); (handle != xtreeEnd(testnames)); handle = xtreeNext(testnames, handle)) {
				rec = (treerec_t *)xtreeData(testnames, handle);
				fprintf(output, "<OPTION VALUE=\"%s\">%s</OPTION>\n", rec->name, rec->name);
			}
		}
		else if (strcmp(t_start, "DISABLELIST") == 0) {
			char *walk, *eoln;
			dishost_t *dhosts = NULL, *hwalk, *hprev;
			distest_t *twalk;

			fetch_board();
			clearflags(testnames);

			walk = statusboard;
			while (walk) {
				eoln = strchr(walk, '\n'); if (eoln) *eoln = '\0';
				if (*walk) {
					char *buf, *hname, *tname, *dismsg, *p;
					time_t distime;
					xtreePos_t thandle;
					treerec_t *rec;

					buf = strdup(walk);
					hname = tname = dismsg = NULL; distime = 0;

					hname = gettok(buf, "|");
					if (hname) tname = gettok(NULL, "|");
					if (tname) { p = gettok(NULL, "|"); if (p) distime = atol(p); }
					if (distime) dismsg = gettok(NULL, "|\n");

					if (hname && tname && (distime != 0) && dismsg && wanted_host(hname)) {
						nldecode(dismsg);
						hwalk = dhosts; hprev = NULL;
						while (hwalk && (strcasecmp(hname, hwalk->name) > 0)) {
							hprev = hwalk;
							hwalk = hwalk->next;
						}
						if (!hwalk || (strcasecmp(hname, hwalk->name) != 0)) {
							dishost_t *newitem = (dishost_t *) malloc(sizeof(dishost_t));
							newitem->name = strdup(hname);
							newitem->tests = NULL;
							newitem->next = hwalk;
							if (!hprev)
								dhosts = newitem;
							else 
								hprev->next = newitem;
							hwalk = newitem;
						}
						twalk = (distest_t *) malloc(sizeof(distest_t));
						twalk->name = strdup(tname);
						twalk->cause = strdup(dismsg);
						twalk->until = distime;
						twalk->next = hwalk->tests;
						hwalk->tests = twalk;

						thandle = xtreeFind(testnames, tname);
						if (thandle != xtreeEnd(testnames)) {
							rec = xtreeData(testnames, thandle);
							rec->flag = 1;
						}
					}

					xfree(buf);
				}

				if (eoln) {
					*eoln = '\n';
					walk = eoln+1;
				}
				else {
					walk = NULL;
				}
			}

			if (dhosts) {
				/* Insert the "All hosts" record first. */
				hwalk = (dishost_t *)calloc(1, sizeof(dishost_t));
				hwalk->next = dhosts;
				dhosts = hwalk;

				for (hwalk = dhosts; (hwalk); hwalk = hwalk->next) {
					fprintf(output, "<TR>");
					fprintf(output, "<TD>");
					fprintf(output,"<form method=\"post\" action=\"%s/enadis.sh\">\n",
						xgetenv("SECURECGIBINURL"));

					fprintf(output, "<table summary=\"%s disabled tests\" width=\"100%%\">\n", 
						(hwalk->name ? hwalk->name : ""));

					fprintf(output, "<tr>\n");
					fprintf(output, "<TH COLSPAN=3><I>%s</I></TH>", 
							(hwalk->name ? hwalk->name : "All hosts"));
					fprintf(output, "</tr>\n");


					fprintf(output, "<tr>\n");

					fprintf(output, "<td>\n");
					if (hwalk->name) {
						fprintf(output, "<input name=\"hostname\" type=hidden value=\"%s\">\n", 
							hwalk->name);

						fprintf(output, "<textarea name=\"%s causes\" rows=\"8\" cols=\"50\" readonly style=\"font-size: 10pt\">\n", hwalk->name);
						for (twalk = hwalk->tests; (twalk); twalk = twalk->next) {
							char *msg = twalk->cause;
							msg += strspn(msg, "0123456789 ");
							fprintf(output, "%s\n%s\nUntil: %s\n---------------------\n", 
								twalk->name, msg, 
								(twalk->until == -1) ? "OK" : ctime(&twalk->until));
						}
						fprintf(output, "</textarea>\n");
					}
					else {
						dishost_t *hw2;
						fprintf(output, "<select multiple size=8 name=\"hostname\">\n");
						for (hw2 = hwalk->next; (hw2); hw2 = hw2->next)
							fprintf(output, "<option value=\"%s\">%s</option>\n", 
								hw2->name, hw2->name);
						fprintf(output, "</select>\n");
					}
					fprintf(output, "</td>\n");

					fprintf(output, "<td align=center>\n");
					fprintf(output, "<select multiple size=8 name=\"enabletest\">\n");
					fprintf(output, "<option value=\"*\" selected>ALL</option>\n");
					if (hwalk->tests) {
						for (twalk = hwalk->tests; (twalk); twalk = twalk->next) {
							fprintf(output, "<option value=\"%s\">%s</option>\n",
								twalk->name, twalk->name);
						}
					}
					else {
						xtreePos_t tidx;
						treerec_t *rec;

						for (tidx = xtreeFirst(testnames); (tidx != xtreeEnd(testnames)); tidx = xtreeNext(testnames, tidx)) {
							rec = xtreeData(testnames, tidx);
							if (rec->flag == 0) continue;

							fprintf(output, "<option value=\"%s\">%s</option>\n",
								rec->name, rec->name);
						}
					}
					fprintf(output, "</select>\n");
					fprintf(output, "</td>\n");

					fprintf(output, "<td align=center>\n");
					fprintf(output, "<input name=\"go\" type=submit value=\"Enable\">\n");
					fprintf(output, "</td>\n");

					fprintf(output, "</tr>\n");

					fprintf(output, "</table>\n");
					fprintf(output, "</form>\n");
					fprintf(output, "</td>\n");
					fprintf(output, "</TR>\n");
				}
			}
			else {
				fprintf(output, "<tr><th align=center colspan=3><i>No tests disabled</i></th></tr>\n");
			}
		}
		else if (strcmp(t_start, "SCHEDULELIST") == 0) {
			char *walk, *eoln;
			int gotany = 0;

			fetch_board();

			walk = scheduleboard;
			while (walk) {
				eoln = strchr(walk, '\n'); if (eoln) *eoln = '\0';
				if (*walk) {
					int id = 0;
					time_t executiontime = 0;
					char *sender = NULL, *cmd = NULL, *buf, *p, *eoln;

					buf = strdup(walk);
					p = gettok(buf, "|");
					if (p) { id = atoi(p); p = gettok(NULL, "|"); }
					if (p) { executiontime = atoi(p); p = gettok(NULL, "|"); }
					if (p) { sender = p; p = gettok(NULL, "|"); }
					if (p) { cmd = p; }

					if (id && executiontime && sender && cmd) {
						gotany = 1;
						nldecode(cmd);
						fprintf(output, "<TR>\n");

						fprintf(output, "<TD>%s</TD>\n", ctime(&executiontime));

						fprintf(output, "<TD>");
						p = cmd;
						while ((eoln = strchr(p, '\n')) != NULL) {
							*eoln = '\0';
							fprintf(output, "%s<BR>", p);
							p = (eoln + 1);
						}
						fprintf(output, "</TD>\n");

						fprintf(output, "<td>\n");
						fprintf(output, "<form method=\"post\" action=\"%s/enadis.sh\">\n",
							xgetenv("SECURECGIBINURL"));
						fprintf(output, "<input name=canceljob type=hidden value=\"%d\">\n", 
							id);
						fprintf(output, "<input name=go type=submit value=\"Cancel\">\n");
						fprintf(output, "</form></td>\n");

						fprintf(output, "</TR>\n");
					}
					xfree(buf);
				}

				if (eoln) {
					*eoln = '\n';
					walk = eoln+1;
				}
				else {
					walk = NULL;
				}
			}

			if (!gotany) {
				fprintf(output, "<tr><th align=center colspan=3><i>No tasks scheduled</i></th></tr>\n");
			}
		}

		else if (strncmp(t_start, "GENERICLIST", strlen("GENERICLIST")) == 0) {
			listpool_t *pool = find_listpool(t_start + strlen("GENERICLIST"));
			listrec_t *walk;

			for (walk = pool->listhead; (walk); walk = walk->next)
				fprintf(output, "<OPTION VALUE=\"%s\" %s %s>%s</OPTION>\n", 
					walk->val, (walk->selected ? "SELECTED" : ""), (walk->extra ? walk->extra : ""),
					walk->name);
		}

		else if (strcmp(t_start, "CRITACKTTPRIO") == 0) fprintf(output, "%d", critackttprio);
		else if (strcmp(t_start, "CRITACKTTGROUP") == 0) fprintf(output, "%s", critackttgroup);
		else if (strcmp(t_start, "CRITACKTTEXTRA") == 0) fprintf(output, "%s", critackttextra);
		else if (strcmp(t_start, "CRITACKINFOURL") == 0) fprintf(output, "%s", ackinfourl);
		else if (strcmp(t_start, "CRITACKDOCURL") == 0) fprintf(output, "%s", critackdocurl);

		else if (strcmp(t_start, "CRITEDITUPDINFO") == 0) {
			fprintf(output, "%s", criteditupdinfo);
		}

		else if (strcmp(t_start, "CRITEDITPRIOLIST") == 0) {
			int i;
			char *selstr;

			for (i=1; (i <= 3); i++) {
				selstr = ((i == criteditprio) ? "SELECTED" : "");
				fprintf(output, "<option value=\"%d\" %s>%d</option>\n", i, selstr, i);
			}
		}

		else if (strcmp(t_start, "CRITEDITCLONELIST") == 0) {
			int i;
			for (i=0; (criteditclonelist[i]); i++) 
				fprintf(output, "<option value=\"%s\">%s</option>\n", 
					criteditclonelist[i], criteditclonelist[i]);
		}

		else if (strcmp(t_start, "CRITEDITGROUP") == 0) {
			fprintf(output, "%s", criteditgroup);
		}

		else if (strcmp(t_start, "CRITEDITEXTRA") == 0) {
			fprintf(output, "%s", criteditextra);
		}

		else if (strcmp(t_start, "CRITEDITWKDAYS") == 0) {
			fprintf(output, "%s", wkdayselect('*', "All days", 1));
			fprintf(output, "%s", wkdayselect('W', "Mon-Fri", 0));
			fprintf(output, "%s", wkdayselect('1', "Monday", 0));
			fprintf(output, "%s", wkdayselect('2', "Tuesday", 0));
			fprintf(output, "%s", wkdayselect('3', "Wednesday", 0));
			fprintf(output, "%s", wkdayselect('4', "Thursday", 0));
			fprintf(output, "%s", wkdayselect('5', "Friday", 0));
			fprintf(output, "%s", wkdayselect('6', "Saturday", 0));
			fprintf(output, "%s", wkdayselect('0', "Sunday", 0));
		}

		else if (strcmp(t_start, "CRITEDITSTART") == 0) {
			int i, curr;
			char *selstr;

			curr = (criteditslastart ? (atoi(criteditslastart) / 100) : 0);
			for (i=0; (i <= 23); i++) {
				selstr = ((i == curr) ? "SELECTED" : "");
				fprintf(output, "<option value=\"%02i00\" %s>%02i:00</option>\n", i, selstr, i);
			}
		}

		else if (strcmp(t_start, "CRITEDITEND") == 0) {
			int i, curr;
			char *selstr;

			curr = (criteditslaend ? (atoi(criteditslaend) / 100) : 24);
			for (i=1; (i <= 24); i++) {
				selstr = ((i == curr) ? "SELECTED" : "");
				fprintf(output, "<option value=\"%02i00\" %s>%02i:00</option>\n", i, selstr, i);
			}
		}

		else if (strncmp(t_start, "CRITEDITDAYLIST", 13) == 0) {
			time_t t = ((*(t_start+13) == '1') ? criteditstarttime : criteditendtime);
			char *defstr = ((*(t_start+13) == '1') ? "Now" : "Never");
			int i;
			char *selstr;
			struct tm *tm;

			tm = localtime(&t);

			selstr = ((t == 0) ? "SELECTED" : "");
			fprintf(output, "<option value=\"0\" %s>%s</option>\n", selstr, defstr);

			for (i=1; (i <= 31); i++) {
				selstr = ( (t && (tm->tm_mday == i)) ? "SELECTED" : "");
				fprintf(output, "<option value=\"%d\" %s>%d</option>\n", i, selstr, i);
			}
		}

		else if (strncmp(t_start, "CRITEDITMONLIST", 13) == 0) {
			time_t t = ((*(t_start+13) == '1') ? criteditstarttime : criteditendtime);
			char *defstr = ((*(t_start+13) == '1') ? "Now" : "Never");
			int i;
			char *selstr;
			struct tm tm;
			time_t now;
			struct tm nowtm;
			struct tm monthtm;
			char mname[20];

			memcpy(&tm, localtime(&t), sizeof(tm));

			now = getcurrenttime(NULL);
			memcpy(&nowtm, localtime(&now), sizeof(tm));

			selstr = ((t == 0) ? "SELECTED" : "");
			fprintf(output, "<option value=\"0\" %s>%s</option>\n", selstr, defstr);

			for (i=1; (i <= 12); i++) {
				selstr = ( (t && (tm.tm_mon == (i -1))) ? "SELECTED" : "");
				monthtm.tm_mon = (i-1); monthtm.tm_mday = 1; monthtm.tm_year = nowtm.tm_year;
				monthtm.tm_hour = monthtm.tm_min = monthtm.tm_sec = monthtm.tm_isdst = 0;
				strftime(mname, sizeof(mname)-1, "%B", &monthtm);
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%s</option>\n", i, selstr, mname);
			}
		}

		else if (strncmp(t_start, "CRITEDITYEARLIST", 14) == 0) {
			time_t t = ((*(t_start+14) == '1') ? criteditstarttime : criteditendtime);
			char *defstr = ((*(t_start+14) == '1') ? "Now" : "Never");
			int i;
			char *selstr;
			struct tm tm;
			time_t now;
			struct tm nowtm;
			int beginyear, endyear;

			memcpy(&tm, localtime(&t), sizeof(tm));

			now = getcurrenttime(NULL);
			memcpy(&nowtm, localtime(&now), sizeof(tm));

			beginyear = nowtm.tm_year + 1900;
			endyear = nowtm.tm_year + 1900 + 5;

			selstr = ((t == 0) ? "SELECTED" : "");
			fprintf(output, "<option value=\"0\" %s>%s</option>\n", selstr, defstr);

			for (i=beginyear; (i <= endyear); i++) {
				selstr = ( (t && (tm.tm_year == (i - 1900))) ? "SELECTED" : "");
				fprintf(output, "<OPTION VALUE=\"%d\" %s>%d</option>\n", i, selstr, i);
			}
		}

		else if (hostenv_hikey && ( (strncmp(t_start, "XMH_", 4) == 0) || (strncmp(t_start, "BBH_", 4) == 0) )) {
			void *hinfo = hostinfo(hostenv_hikey);
			if (hinfo) {
				char *s;

				if (strncmp(t_start, "BBH_", 4) == 0) memmove(t_start, "XMH_", 4); /* For compatibility */
				s = xmh_item_byname(hinfo, t_start);

				if (!s) {
					fprintf(output, "&%s", t_start);
				}
				else {
					fprintf(output, "%s", s);
				}
			}
		}

		else if (strncmp(t_start, "BACKDAYS", 8) == 0) {
			fprintf(output, "%d", backdays);
		}

		else if (strncmp(t_start, "BACKHOURS", 9) == 0) {
			fprintf(output, "%d", backhours);
		}

		else if (strncmp(t_start, "BACKMINS", 8) == 0) {
			fprintf(output, "%d", backmins);
		}

		else if (strncmp(t_start, "BACKSECS", 8) == 0) {
			fprintf(output, "%d", backsecs);
		}

		else if (strncmp(t_start, "EVENTLASTMONTHBEGIN", 19) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);

			tm->tm_mon -= 1;
			tm->tm_mday = 1;
			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}
		else if (strncmp(t_start, "EVENTCURRMONTHBEGIN", 19) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);
			tm->tm_mday = 1;
			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}

		else if (strncmp(t_start, "EVENTLASTWEEKBEGIN", 18) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);
			int weekstart = atoi(xgetenv("WEEKSTART"));

			if (tm->tm_wday == weekstart) { /* Do nothing */ }
			else if (tm->tm_wday > weekstart) tm->tm_mday -= (tm->tm_wday - weekstart);
			else tm->tm_mday += (weekstart - tm->tm_wday) - 7;

			tm->tm_mday -= 7;
			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}
		else if (strncmp(t_start, "EVENTCURRWEEKBEGIN", 18) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);
			int weekstart = atoi(xgetenv("WEEKSTART"));

			if (tm->tm_wday == weekstart) { /* Do nothing */ }
			else if (tm->tm_wday > weekstart) tm->tm_mday -= (tm->tm_wday - weekstart);
			else tm->tm_mday += (weekstart - tm->tm_wday) - 7;

			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}

		else if (strncmp(t_start, "EVENTLASTYEARBEGIN", 18) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);

			tm->tm_year -= 1;
			tm->tm_mon = 0;
			tm->tm_mday = 1;
			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}
		else if (strncmp(t_start, "EVENTCURRYEARBEGIN", 18) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);

			tm->tm_mon = 0;
			tm->tm_mday = 1;
			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}
		else if (strncmp(t_start, "EVENTYESTERDAY", 14) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);

			tm->tm_mday -= 1;
			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}
		else if (strncmp(t_start, "EVENTTODAY", 10) == 0) {
			time_t t = getcurrenttime(NULL);
			struct tm *tm = localtime(&t);

			tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
			tm->tm_isdst = -1;
			t = mktime(tm);
			fprintf(output, "%s", eventreport_timestring(t));
		}
		else if (strncmp(t_start, "EVENTNOW", 8) == 0) {
			time_t t = getcurrenttime(NULL);
			fprintf(output, "%s", eventreport_timestring(t));
		}

		else if (strncmp(t_start, "PAGEPATH_DROPDOWN", 17) == 0) {
			build_pagepath_dropdown(output);
		}
		else if (strncmp(t_start, "EVENTSTARTTIME", 8) == 0) {
			fprintf(output, "%s", hostenv_eventtimestart);
		}
		else if (strncmp(t_start, "EVENTENDTIME", 8) == 0) {
			fprintf(output, "%s", hostenv_eventtimeend);
		}

		else if (strncmp(t_start, "XYMONBODY", 9) == 0) {
			char *bodytext = xymonbody(t_start);
			fprintf(output, "%s", bodytext);
		}

		else if (*t_start && (savechar == ';')) {
			/* A "&xxx;" is probably an HTML escape - output unchanged. */
			fprintf(output, "&%s", t_start);
		}

		else if (*t_start && (strncmp(t_start, "SELECT_", 7) == 0)) {
			/*
			 * Special for getting the SELECTED tag into list boxes.
			 * Cannot use xgetenv because it complains for undefined
			 * environment variables.
			 */
			char *val = getenv(t_start);

			fprintf(output, "%s", (val ? val : ""));
		}

		else if (strlen(t_start) && xgetenv(t_start)) {
			fprintf(output, "%s", xgetenv(t_start));
		}

		else fprintf(output, "&%s", t_start);		/* No substitution - copy all unchanged. */
			
		*t_next = savechar; t_start = t_next; t_next = strchr(t_start, '&');
	}

	/* Remainder of file */
	fprintf(output, "%s", t_start);
}


void headfoot(FILE *output, char *template, char *pagepath, char *head_or_foot, int bgcolor)
{
	int	fd;
	char 	filename[PATH_MAX];
	char    *bulletinfile;
	struct  stat st;
	char	*templatedata;
	char	*hfpath;
	int	have_pagepath = (hostenv_pagepath != NULL);

	MEMDEFINE(filename);

	if (xgetenv("XYMONDREL") == NULL) {
		char *xymondrel = (char *)malloc(12+strlen(VERSION));
		sprintf(xymondrel, "XYMONDREL=%s", VERSION);
		putenv(xymondrel);
	}

	/*
	 * "pagepath" is the relative path for this page, e.g. 
	 * - for the top-level page it is ""
	 * - for a page, it is "pagename/"
	 * - for a subpage, it is "pagename/subpagename/"
	 *
	 * We allow header/footer files named template_PAGE_header or template_PAGE_SUBPAGE_header
	 * so we need to scan for an existing file - starting with the
	 * most detailed one, and working up towards the standard "web/template_TYPE" file.
	 */

	hfpath = strdup(pagepath); 
	/* Trim off excess trailing slashes */
	if (*hfpath) {
		while (*(hfpath + strlen(hfpath) - 1) == '/') *(hfpath + strlen(hfpath) - 1) = '\0';
	}
	fd = -1;

	if (!have_pagepath) hostenv_pagepath = strdup(hfpath);

	while ((fd == -1) && strlen(hfpath)) {
		char *p;
		char *elemstart;

		if (hostenv_templatedir) {
			sprintf(filename, "%s/", hostenv_templatedir);
		}
		else {
			sprintf(filename, "%s/web/", xgetenv("XYMONHOME"));
		}

		p = strchr(hfpath, '/'); elemstart = hfpath;
		while (p) {
			*p = '\0';
			strcat(filename, elemstart);
			strcat(filename, "_");
			*p = '/';
			p++;
			elemstart = p; p = strchr(elemstart, '/');
		}
		strcat(filename, elemstart);
		strcat(filename, "_");
		strcat(filename, head_or_foot);

		dbgprintf("Trying header/footer file '%s'\n", filename);
		fd = open(filename, O_RDONLY);

		if (fd == -1) {
			p = strrchr(hfpath, '/');
			if (p == NULL) p = hfpath;
			*p = '\0';
		}
	}
	xfree(hfpath);

	if (fd == -1) {
		/* Fall back to default head/foot file. */
		if (hostenv_templatedir) {
			sprintf(filename, "%s/%s_%s", hostenv_templatedir, template, head_or_foot);
		}
		else {
			sprintf(filename, "%s/web/%s_%s", xgetenv("XYMONHOME"), template, head_or_foot);
		}

		dbgprintf("Trying header/footer file '%s'\n", filename);
		fd = open(filename, O_RDONLY);
	}

	if (fd != -1) {
		fstat(fd, &st);
		templatedata = (char *) malloc(st.st_size + 1);
		read(fd, templatedata, st.st_size);
		templatedata[st.st_size] = '\0';
		close(fd);

		output_parsed(output, templatedata, bgcolor, getcurrenttime(NULL));

		xfree(templatedata);
	}
	else {
		fprintf(output, "<HTML><BODY> \n <HR size=4> \n <BR>%s is either missing or invalid, please create this file with your custom header<BR> \n<HR size=4>", htmlquoted(filename));
	}

	/* Check for bulletin files */
	bulletinfile = (char *)malloc(strlen(xgetenv("XYMONHOME")) + strlen("/web/bulletin_") + strlen(head_or_foot)+1);
	sprintf(bulletinfile, "%s/web/bulletin_%s", xgetenv("XYMONHOME"), head_or_foot);
	fd = open(bulletinfile, O_RDONLY);
	if (fd != -1) {
		fstat(fd, &st);
		templatedata = (char *) malloc(st.st_size + 1);
		read(fd, templatedata, st.st_size);
		templatedata[st.st_size] = '\0';
		close(fd);
		output_parsed(output, templatedata, bgcolor, getcurrenttime(NULL));
		xfree(templatedata);
	}

	if (!have_pagepath) {
		xfree(hostenv_pagepath); hostenv_pagepath = NULL;
	}

	xfree(bulletinfile);

	MEMUNDEFINE(filename);
}

void showform(FILE *output, char *headertemplate, char *formtemplate, int color, time_t seltime, 
	      char *pretext, char *posttext)
{
	/* Present the query form */
	int formfile;
	char formfn[PATH_MAX];

	sprintf(formfn, "%s/web/%s", xgetenv("XYMONHOME"), formtemplate);
	formfile = open(formfn, O_RDONLY);

	if (formfile >= 0) {
		char *inbuf;
		struct stat st;

		fstat(formfile, &st);
		inbuf = (char *) malloc(st.st_size + 1);
		read(formfile, inbuf, st.st_size);
		inbuf[st.st_size] = '\0';
		close(formfile);

		if (headertemplate) headfoot(output, headertemplate, (hostenv_pagepath ? hostenv_pagepath : ""), "header", color);
		if (pretext) fprintf(output, "%s", pretext);
		output_parsed(output, inbuf, color, seltime);
		if (posttext) fprintf(output, "%s", posttext);
		if (headertemplate) headfoot(output, headertemplate, (hostenv_pagepath ? hostenv_pagepath : ""), "footer", color);

		xfree(inbuf);
	}
}

