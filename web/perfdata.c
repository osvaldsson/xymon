/*----------------------------------------------------------------------------*/
/* Xymon CGI for reporting performance statisticsc from the RRD data          */
/*                                                                            */
/* Copyright (C) 2008-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: perfdata.c 6712 2011-07-31 21:01:52Z storner $";

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <dirent.h>

#include <rrd.h>
#include <pcre.h>

#include "libxymon.h"

enum { O_NONE, O_XML, O_CSV } outform = O_NONE;
char csvdelim = ',';
char *hostpattern = NULL;
char *exhostpattern = NULL;
char *pagepattern = NULL;
char *expagepattern = NULL;
char *starttime = NULL;
char *starttimedate = NULL, *starttimehm = NULL;
char *endtime = NULL;
char *endtimedate = NULL, *endtimehm = NULL;
char *customrrd = NULL;
char *customds = NULL;

static void parse_query(void)
{
	cgidata_t *cgidata = cgi_request();
	cgidata_t *cwalk;

	cwalk = cgidata;
	while (cwalk) {
		if (strcasecmp(cwalk->name, "HOST") == 0) {
			if (*(cwalk->value)) hostpattern = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "EXHOST") == 0) {
			if (*(cwalk->value)) hostpattern = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "PAGEMATCH") == 0) {
			if (*(cwalk->value)) pagepattern = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "EXPAGEMATCH") == 0) {
			if (*(cwalk->value)) expagepattern = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "STARTTIME") == 0) {
			if (*(cwalk->value)) starttime = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "ENDTIME") == 0) {
			if (*(cwalk->value)) endtime = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "CUSTOMRRD") == 0) {
			if (*(cwalk->value)) customrrd = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "CUSTOMDS") == 0) {
			if (*(cwalk->value)) customds = strdup(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "CSV") == 0) {
			outform = O_CSV;
			if (*(cwalk->value)) csvdelim = *(cwalk->value);
		}
		else if (strcasecmp(cwalk->name, "FORMAT") == 0) {
			if (strcmp(cwalk->value, "XML") == 0)
				outform = O_XML;
			else {
				outform = O_CSV;
				csvdelim = *(cwalk->value);
			}
		}

		cwalk = cwalk->next;
	}
}


int oneset(char *hostname, char *rrdname, char *starttime, char *endtime, char *colname, double subfrom, char *dsdescr)
{
	static int firsttime = 1;
	time_t start, end, t;
	unsigned long step;
	unsigned long dscount;
	char **dsnames;
	rrd_value_t *data;
	int columnindex;
	char tstamp[30];
	int dataindex, rowcount, havemin, havemax, missingdata;
	double sum, min, max, val;

	char *rrdargs[10];
	int result;

	rrdargs[0] = "rrdfetch";
	rrdargs[1] = rrdname;
	rrdargs[2] = "AVERAGE";
	rrdargs[3] = "-s"; rrdargs[4] = starttimedate; rrdargs[5] = starttimehm;
	rrdargs[6] = "-e"; rrdargs[7] = endtimedate; rrdargs[8] = endtimehm;
	rrdargs[9] = NULL;

	optind = opterr = 0; rrd_clear_error();
	result = rrd_fetch(9, rrdargs,
			   &start, &end, &step, &dscount, &dsnames, &data);

	if (result != 0) {
		errprintf("RRD error: %s\n", rrd_get_error());
		return 1;
	}

	for (columnindex=0; ((columnindex < dscount) && strcmp(dsnames[columnindex], colname)); columnindex++) ;
	if (columnindex == dscount) {
		errprintf("RRD error: Cannot find column %s\n", colname);
		return 1;
	}

	sum = 0.0;
	havemin = havemax = 0;
	rowcount = 0;

	switch (outform) {
	  case O_XML:
		printf("  <dataset>\n");
		printf("     <hostname>%s</hostname>\n", hostname);
		printf("     <datasource>%s</datasource>\n", rrdname);
		printf("     <rrdcolumn>%s</rrdcolumn>\n", colname);
		printf("     <measurement>%s</measurement>\n", (dsdescr ? dsdescr : colname));
		printf("     <datapoints>\n");
		break;

	  case O_CSV:
		if (firsttime) {
			printf("\"hostname\"%c\"datasource\"%c\"rrdcolumn\"%c\"measurement\"%c\"time\"%c\"value\"\n",
				csvdelim, csvdelim, csvdelim, csvdelim, csvdelim);
			firsttime = 0;
		}
		break;

	  default:
		break;
	}

	for (t=start+step, dataindex=columnindex, missingdata=0; (t <= end); t += step, dataindex += dscount) {
		if (isnan(data[dataindex]) || isnan(-data[dataindex])) {
			missingdata++;
			continue;
		}

		val = (subfrom != 0) ?  subfrom - data[dataindex] : data[dataindex];

		strftime(tstamp, sizeof(tstamp), "%Y%m%d%H%M%S", localtime(&t));

		switch (outform) {
		  case O_XML:
			printf("        <dataelement>\n");
			printf("           <time>%s</time>\n", tstamp);
			printf("           <value>%f</value>\n", val);
			printf("        </dataelement>\n");
			break;
		  case O_CSV:
			printf("\"%s\"%c\"%s\"%c\"%s\"%c\"%s\"%c\"%s\"%c%f\n",
				hostname, csvdelim, rrdname, csvdelim, colname, csvdelim, (dsdescr ? dsdescr : colname), csvdelim, tstamp, csvdelim, val);
			break;

		  default:
			break;
		}

		if (!havemax || (val > max)) {
			max = val;
			havemax = 1;
		}
		if (!havemin || (val < min)) {
			min = val;
			havemin = 1;
		}
		sum += val;
		rowcount++;
	}

	if (outform == O_XML) {
		printf("     </datapoints>\n");
		printf("     <summary>\n");
		if (havemin) printf("          <minimum>%f</minimum>\n", min);
		if (havemax) printf("          <maximum>%f</maximum>\n", max);
		if (rowcount) printf("          <average>%f</average>\n", (sum / rowcount));
		printf("          <missingdatapoints>%d</missingdatapoints>\n", missingdata);
		printf("     </summary>\n");
		printf("  </dataset>\n");
	}

	return 0;
}


int onehost(char *hostname, char *starttime, char *endtime)
{
	struct stat st;
	DIR *d;
	struct dirent *de;

	if ((chdir(xgetenv("XYMONRRDS")) == -1) || (chdir(hostname) == -1)) {
		errprintf("Cannot cd to %s/%s\n", xgetenv("XYMONRRDS"), hostname);
		return 1;
	}

	if (customrrd && customds) {
		if (stat(customrrd, &st) != 0) return 1;

		oneset(hostname, customrrd, starttime, endtime, customds, 0, customds);
		return 0;
	}

	/* 
	 * CPU busy data - use vmstat.rrd if it is there, 
	 * if not then assume it's a Windows box and report the la.rrd data.
	 */
	if (stat("vmstat.rrd", &st) == 0) {
		oneset(hostname, "vmstat.rrd", starttime, endtime, "cpu_idl", 100, "pctbusy");
	}
	else {
		/* No vmstat data, so use the la.rrd file */
		oneset(hostname, "la.rrd", starttime, endtime, "la", 0, "pctbusy");
	}

	/*
	 * Report all memory data - it depends on the OS of the host which one
	 * really is interesting (memory.actual.rrd for Linux, memory.real.rrd for
	 * most of the other systems).
	 */
	if (stat("memory.actual.rrd", &st) == 0) {
		oneset(hostname, "memory.actual.rrd", starttime, endtime, "realmempct", 0, "Virtual");
	}
	if (stat("memory.real.rrd", &st) == 0) {
		oneset(hostname, "memory.real.rrd", starttime, endtime, "realmempct", 0, "RAM");
	}
	if (stat("memory.swap.rrd", &st) == 0) {
		oneset(hostname, "memory.swap.rrd", starttime, endtime, "realmempct", 0, "Swap");
	}

	/*
	 * Report data for all filesystems.
	 */
	d = opendir(".");
	while ((de = readdir(d)) != NULL) {
		if (strncmp(de->d_name, "disk,", 5) != 0) continue;

		stat(de->d_name, &st);
		if (!S_ISREG(st.st_mode)) continue;

		if (strcmp(de->d_name, "disk,root.rrd") == 0) {
			oneset(hostname, de->d_name, starttime, endtime, "pct", 0, "/");
		}
		else {
			char *fsnam = strdup(de->d_name+4);
			char *p;

			while ((p = strchr(fsnam, ',')) != NULL) *p = '/';
			p = fsnam + strlen(fsnam) - 4; *p = '\0';
			dbgprintf("Processing set %s for host %s from %s\n", de->d_name, hostname, fsnam);
			oneset(hostname, de->d_name, starttime, endtime, "pct", 0, fsnam);
			xfree(fsnam);
		}
	}
	closedir(d);
	return 0;
}

void format_rrdtime(char *t, char **tday, char **thm)
{
	int year, month, day, hour, min,sec;
	int n, parseerror;
	time_t now = getcurrenttime(NULL);
	struct tm *nowtm = localtime(&now);

	if (t == NULL) return;

	/* Input is YYYY/MM/DD@HH:MM:SS or YYYYMMDD or MMDD */
	parseerror = 0;
	n = sscanf(t, "%d/%d/%d@%d:%d:%d", &year, &month, &day, &hour, &min, &sec);
	switch (n) {
	  case 6: break; /* Got all */
	  case 5: sec = 0; break;
	  case 4: min = sec = 0; break;
	  case 3: hour = min = sec = 0; break;
	  default: parseerror = 1; break;
	}

	parseerror = 0;
	hour = min = sec = 0;
	n = sscanf(t, "%d/%d/%d", &year, &month, &day);
	switch (n) {
	  case 3: break; /* Got all */
	  case 2: day = month; month = year; year = nowtm->tm_year + 1900;
	  default: parseerror = 1; break;
	}

	if (year < 100) year += 2000;

	*tday = (char *)malloc(10);
	sprintf(*tday, "%4d%02d%02d", year, month, day);
	*thm = (char *)malloc(20);
	sprintf(*thm, "%02d:%02d:%02d", hour, min, sec);
}

int main(int argc, char **argv)
{
	pcre *hostptn, *exhostptn, *pageptn, *expageptn;
	void *hwalk;
	char *hostname, *pagename;

	hostptn = exhostptn = pageptn = expageptn = NULL;

	if (getenv("QUERY_STRING") == NULL) {
		/* Not invoked through the CGI */
		if (argc < 4) {
			errprintf("Usage:\n%s HOSTNAME-PATTERN STARTTIME ENDTIME", argv[0]);
			return 1;
		}

		hostpattern = argv[1];
		if (strncmp(hostpattern, "--page=", 7) == 0) {
			pagepattern = strchr(argv[1], '=') + 1;
			hostpattern = NULL;
		}
		starttimedate = argv[2]; starttimehm = "00:00:00";
		endtimedate = argv[3]; endtimehm = "00:00:00";
		if (argc > 4) {
			if (strncmp(argv[4], "--csv", 5) == 0) {
				char *p;

				outform = O_CSV;
				if ((p = strchr(argv[4], '=')) != NULL) csvdelim = *(p+1);
			}
		}
	}
	else {
		char *envarea;
		int argi;

		for (argi = 1; (argi < argc); argi++) {
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

		/* Parse CGI parameters */
		parse_query();
		format_rrdtime(starttime, &starttimedate, &starttimehm);
		format_rrdtime(endtime, &endtimedate, &endtimehm);

		switch (outform) {
		  case O_XML:
			printf("Content-type: application/xml\n\n");
			break;

		  case O_CSV:
			printf("Content-type: text/csv\n\n");
			break;

		  case O_NONE:
			load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
			printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
			showform(stdout, "perfdata", "perfdata_form", COL_BLUE, getcurrenttime(NULL), NULL, NULL);
			return 0;
		}
	}

	load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());

	if (hostpattern) hostptn = compileregex(hostpattern);
	if (exhostpattern) exhostptn = compileregex(exhostpattern);
	if (pagepattern) pageptn = compileregex(pagepattern);
	if (expagepattern) expageptn = compileregex(expagepattern);

	switch (outform) {
	  case O_XML:
		printf("<?xml version='1.0' encoding='ISO-8859-1'?>\n");
		printf("<datasets>\n");
		break;
	  default:
		break;
	}

	dbgprintf("Got hosts, it is %s\n", (first_host() == NULL) ? "empty" : "not empty");

	for (hwalk = first_host(); (hwalk); hwalk = next_host(hwalk, 0)) {
		hostname = xmh_item(hwalk, XMH_HOSTNAME);
		pagename = xmh_item(hwalk, XMH_PAGEPATH);

		dbgprintf("Processing host %s\n", hostname);

		if (hostpattern && !matchregex(hostname, hostptn)) continue;
		if (exhostpattern && matchregex(hostname, exhostptn)) continue;
		if (pagepattern && !matchregex(pagename, pageptn)) continue;
		if (expagepattern && matchregex(pagename, expageptn)) continue;

		onehost(hostname, starttime, endtime);
	}

	switch (outform) {
	  case O_XML:
		printf("</datasets>\n");
		break;
	  default:
		break;
	}

	return 0;
}

