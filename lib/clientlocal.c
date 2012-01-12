/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, responsible for loading the            */
/* client-local.cfg file into memory and finding the proper host entry.       */
/*                                                                            */
/* Copyright (C) 2006-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: clientlocal.c 6745 2011-09-04 06:01:06Z storner $";

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

static strbuffer_t *clientconfigs = NULL;
static void * rbconfigs;

void load_clientconfig(void)
{
	static char *configfn = NULL;
	static void *clientconflist = NULL;
	FILE *fd;
	strbuffer_t *buf;
	char *sectstart;

	if (!configfn) {
		configfn = (char *)malloc(strlen(xgetenv("XYMONHOME"))+ strlen("/etc/client-local.cfg") + 1);
		sprintf(configfn, "%s/etc/client-local.cfg", xgetenv("XYMONHOME"));
	}

	/* First check if there were no modifications at all */
	if (clientconflist) {
		if (!stackfmodified(clientconflist)){
			dbgprintf("No files modified, skipping reload of %s\n", configfn);
			return;
		}
		else {
			stackfclist(&clientconflist);
			clientconflist = NULL;
		}
	}

	if (!clientconfigs) {
		clientconfigs = newstrbuffer(0);
	}
	else {
		xtreeDestroy(rbconfigs);
		clearstrbuffer(clientconfigs);
	}

	rbconfigs = xtreeNew(strcasecmp);
	addtobuffer(clientconfigs, "\n");
	buf = newstrbuffer(0);

	fd = stackfopen(configfn, "r", &clientconflist); if (!fd) return;
	while (stackfgets(buf, NULL)) addtostrbuffer(clientconfigs, buf);
	stackfclose(fd);

	sectstart = strstr(STRBUF(clientconfigs), "\n[");
	while (sectstart) {
		char *key, *nextsect;

		sectstart += 2;
		key = sectstart;

		sectstart += strcspn(sectstart, "]\n");
		if (*sectstart == ']') {
			*sectstart = '\0'; sectstart++;
			sectstart += strcspn(sectstart, "\n");
		}

		nextsect = strstr(sectstart, "\n[");
		if (nextsect) *(nextsect+1) = '\0';

		xtreeAdd(rbconfigs, key, sectstart+1);
		sectstart = nextsect;
	}

	freestrbuffer(buf);
}

char *get_clientconfig(char *hostname, char *hostclass, char *hostos)
{
	xtreePos_t handle;
	char *result = NULL;

	if (!clientconfigs) return NULL;

	/*
	 * Find the client config.  Search for a HOSTNAME entry first, 
	 * then the CLIENTCLASS, then CLIENTOS.
	 */
	handle = xtreeFind(rbconfigs, hostname);
	if ((handle == xtreeEnd(rbconfigs)) && hostclass && *hostclass)
		handle = xtreeFind(rbconfigs, hostclass);
	if ((handle == xtreeEnd(rbconfigs)) && hostos && *hostos)
		handle = xtreeFind(rbconfigs, hostos);

	if (handle != xtreeEnd(rbconfigs)) result = (char *)xtreeData(rbconfigs, handle);

	return result;
}

