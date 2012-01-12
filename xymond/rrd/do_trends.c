/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* This module handles custom "trends" data.                                  */
/*                                                                            */
/* Copyright (C) 2007-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char trends_rcsid[] = "$Id: do_trends.c 6712 2011-07-31 21:01:52Z storner $";

/* 
 * This module was inspired by a mail from Stef Coene:
 *
 * ---------------------------------------------------------------------------
 * Date: Wed, 17 Jan 2007 14:04:29 +0100
 * From: Stef Coene
 * Subject: Re: [xymon] xymon monitoring
 * 
 * Just wondering, how hard would it be to create an extra channel for trending?
 * So you can use the xymon client to send "numbers" to the xymon server together
 * with some extra control information.
 * 
 * xymon <xymon server> trends <server name>
 * <rrd file name> <ds name> <number> <options>
 * -----------------------------------------------------------------------------
 *
 * Instead of a dedicated Xymon channel for this, I decided to use the 
 * existing "data" message type. To use this, send a "data" message to 
 * xymon formatted like this:
 *
 *    data $MACHINE.trends
 *    [filename.rrd]
 *    DS-definition1 VALUE2
 *    DS-definition2 VALUE2
 *
 * E.g. to create/update a custom RRD file "weather.rrd" with two 
 * GAUGE datasets "temp" and "wind", with current values "21" and 
 * "8" respectively, send this message:
 *
 *    [weather.rrd]
 *    DS:temp:GAUGE:600:0:U 21
 *    DS:wind:GAUGE:600:0:U 8
 */


static int do_trends_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp) 
{ 
	char *boln, *eoln, *p;
	int dscount;
	char **creparams;

	creparams = (char **)calloc(1, sizeof(char *));
	dscount = 0;

	boln = strchr(msg, '\n'); if (boln) boln++;
	while (boln && *boln) {
		eoln = strchr(boln, '\n'); if (eoln) *eoln = '\0';

		if (*boln == '[') {
			/* Flush the current RRD file */
			if (creparams[0]) create_and_update_rrd(hostname, testname, classname, pagepaths, creparams, NULL);

			creparams = (char **)realloc(creparams, 1*sizeof(char *));
			creparams[0] = NULL;
			dscount = 0;

			/* Get the RRD filename */
			p = strchr(boln+1, ']'); if (p) *p = '\0';
			setupfn("%s", boln+1);

			/* And setup the initial rrdvalues string */
			sprintf(rrdvalues, "%d", (int)tstamp);
		}
		else if (strncmp(boln, "DS:", 3) == 0) {
			char *valptr = boln + strcspn(boln, " \t");

			if ((*valptr == ' ') || (*valptr == '\t')) {
				*valptr = '\0'; valptr += 1 + strspn(valptr+1, " \t");
				creparams[dscount] = boln;
				dscount++;
				creparams = (char **)realloc(creparams, (1+dscount)*sizeof(char **));
				creparams[dscount] = NULL;
				sprintf(rrdvalues+strlen(rrdvalues), ":%s", valptr);
			}
		}

		boln = (eoln ? eoln+1 : NULL);
	}

	/* Do the last RRD set */
	if (creparams[0]) create_and_update_rrd(hostname, testname, classname, pagepaths, creparams, NULL);
	xfree(creparams);

	return 0;
}

