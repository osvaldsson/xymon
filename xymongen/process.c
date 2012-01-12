/*----------------------------------------------------------------------------*/
/* Xymon overview webpage generator tool.                                     */
/*                                                                            */
/* This file contains to to calculate the "color" of hosts and pages, and     */
/* handle summary transmission.                                               */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: process.c 6712 2011-07-31 21:01:52Z storner $";

#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>

#include "xymongen.h"
#include "process.h"
#include "util.h"

void calc_hostcolors(char *nongreenignores)
{
	int		color, nongreencolor, criticalcolor, oldage;
	hostlist_t 	*h, *cwalk;
	entry_t		*e;

	for (h = hostlistBegin(); (h); h = hostlistNext()) {
		color = nongreencolor = criticalcolor = 0; oldage = 1;

		for (e = h->hostentry->entries; (e); e = e->next) {
			if (e->propagate && (e->color > color)) color = e->color;
			oldage &= e->oldage;

			if (e->propagate && (e->color > nongreencolor) && (strstr(nongreenignores, e->column->listname) == NULL)) {
				nongreencolor = e->color;
			}

			if (e->propagate && e->alert && (e->color > criticalcolor)) {
				criticalcolor = e->color;
			}
		}

		/* Blue and clear is not propagated upwards */
		if ((color == COL_CLEAR) || (color == COL_BLUE)) color = COL_GREEN;

		h->hostentry->color = color;
		h->hostentry->nongreencolor = nongreencolor;
		h->hostentry->criticalcolor = criticalcolor;
		h->hostentry->oldage = oldage;

		/* Need to update the clones also */
		for (cwalk = h->clones; (cwalk); cwalk = cwalk->clones) {
			cwalk->hostentry->color = color;
			cwalk->hostentry->nongreencolor = nongreencolor;
			cwalk->hostentry->criticalcolor = criticalcolor;
			cwalk->hostentry->oldage = oldage;
		}
	}
}


void calc_pagecolors(xymongen_page_t *phead)
{
	xymongen_page_t 	*p, *toppage;
	group_t *g;
	host_t  *h;
	int	color, oldage;

	for (toppage=phead; (toppage); toppage = toppage->next) {

		/* Start with the color of immediate hosts */
		color = -1; oldage = 1;
		for (h = toppage->hosts; (h); h = h->next) {
			if (h->color > color) color = h->color;
			oldage &= h->oldage;
		}

		/* Then adjust with the color of hosts in immediate groups */
		for (g = toppage->groups; (g); g = g->next) {
			for (h = g->hosts; (h); h = h->next) {
				if ((g->onlycols == NULL) && (g->exceptcols == NULL)) {
					/* No group-only or group-except directives - use host color */
					if (h->color > color) color = h->color;
					oldage &= h->oldage;
				}
				else if (g->onlycols) {
					/* This is a group-only directive. Color must be
					 * based on the tests included in the group-only
					 * directive, NOT all tests present for the host.
					 * So we need to re-calculate host color from only
					 * the selected tests.
					 */
					entry_t *e;

					for (e = h->entries; (e); e = e->next) {
						if ( e->propagate && 
						     (e->color > color) &&
						     wantedcolumn(e->column->name, g->onlycols) )
							color = e->color;
							oldage &= e->oldage;
					}

					/* Blue and clear is not propagated upwards */
					if ((color == COL_CLEAR) || (color == COL_BLUE)) color = COL_GREEN;
				}
				else if (g->exceptcols) {
					/* This is a group-except directive. Color must be
					 * based on the tests NOT included in the group-except
					 * directive, NOT all tests present for the host.
					 * So we need to re-calculate host color from only
					 * the selected tests.
					 */
					entry_t *e;

					for (e = h->entries; (e); e = e->next) {
						if ( e->propagate && 
						     (e->color > color) &&
						     !wantedcolumn(e->column->name, g->exceptcols) )
							color = e->color;
							oldage &= e->oldage;
					}

					/* Blue and clear is not propagated upwards */
					if ((color == COL_CLEAR) || (color == COL_BLUE)) color = COL_GREEN;
				}
			}
		}

		/* Then adjust with the color of subpages, if any.  */
		/* These must be calculated first!                  */
		if (toppage->subpages) {
			calc_pagecolors(toppage->subpages);
		}

		for (p = toppage->subpages; (p); p = p->next) {
			if (p->color > color) color = p->color;
			oldage &= p->oldage;
		}

		if (color == -1) {
			/*
			 * If no hosts or subpages, all goes green.
			 */
			color = COL_GREEN;
			oldage = 1;
		}

		toppage->color = color;
		toppage->oldage = oldage;
	}
}


void delete_old_acks(void)
{
	DIR             *xymonacks;
	struct dirent   *d;
	struct stat     st;
	time_t		now = getcurrenttime(NULL);
	char		fn[PATH_MAX];

	xymonacks = opendir(xgetenv("XYMONACKDIR"));
	if (!xymonacks) {
		errprintf("No XYMONACKDIR! Cannot cd to directory %s\n", xgetenv("XYMONACKDIR"));
		return;
        }

	chdir(xgetenv("XYMONACKDIR"));
	while ((d = readdir(xymonacks))) {
		strcpy(fn, d->d_name);
		if (strncmp(fn, "ack.", 4) == 0) {
			stat(fn, &st);
			if (S_ISREG(st.st_mode) && (st.st_mtime < now)) {
				unlink(fn);
			}
		}
	}
	closedir(xymonacks);
}

void send_summaries(summary_t *sumhead)
{
	summary_t *s;

	for (s = sumhead; (s); s = s->next) {
		char *suburl;
		int summarycolor = -1;
		char *summsg;

		/* Decide which page to pick the color from for this summary. */
		suburl = s->url;
		if (strncmp(suburl, "http://", 7) == 0) {
			char *p;

			/* Skip hostname part */
			suburl += 7;			/* Skip "http://" */
			p = strchr(suburl, '/');	/* Find next '/' */
			if (p) suburl = p;
		}
		if (strncmp(suburl, xgetenv("XYMONWEB"), strlen(xgetenv("XYMONWEB"))) == 0) 
			suburl += strlen(xgetenv("XYMONWEB"));
		if (*suburl == '/') suburl++;

		dbgprintf("summ1: s->url=%s, suburl=%s\n", s->url, suburl);

		if      (strcmp(suburl, "xymon.html") == 0) summarycolor = xymon_color;
		else if (strcmp(suburl, "index.html") == 0) summarycolor = xymon_color;
		else if (strcmp(suburl, "") == 0) summarycolor = xymon_color;
		else if (strcmp(suburl, "nongreen.html") == 0) summarycolor = nongreen_color;
		else if (strcmp(suburl, "critical.html") == 0) summarycolor = critical_color;
		else {
			/* 
			 * Specific page - find it in the page tree.
			 */
			char *p, *pg;
			xymongen_page_t *pgwalk;
			xymongen_page_t *sourcepg = NULL;
			char *urlcopy = strdup(suburl);

			/*
			 * Walk the page tree
			 */
			pg = urlcopy; sourcepg = pagehead;
			do {
				p = strchr(pg, '/');
				if (p) *p = '\0';

				dbgprintf("Searching for page %s\n", pg);
				for (pgwalk = sourcepg->subpages; (pgwalk && (strcmp(pgwalk->name, pg) != 0)); pgwalk = pgwalk->next);
				if (pgwalk != NULL) {
					sourcepg = pgwalk;

					if (p) { 
						*p = '/'; pg = p+1; 
					}
					else pg = NULL;
				}
				else pg = NULL;
			} while (pg);

			dbgprintf("Summary search for %s found page %s (title:%s), color %d\n",
				suburl, sourcepg->name, sourcepg->title, sourcepg->color);
			summarycolor = sourcepg->color;
			xfree(urlcopy);
		}

		if (summarycolor == -1) {
			errprintf("Could not determine sourcepage for summary %s\n", s->url);
			summarycolor = pagehead->color;
		}

		/* Send the summary message */
		summsg = (char *)malloc(1024 + strlen(s->name) + strlen(s->url) + strlen(timestamp));
		sprintf(summsg, "summary summary.%s %s %s %s",
			s->name, colorname(summarycolor), s->url, timestamp);
		sendmessage(summsg, s->receiver, XYMON_TIMEOUT, NULL);
		xfree(summsg);
	}
}

