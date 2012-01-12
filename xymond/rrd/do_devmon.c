/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module for Devmon                                        */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/* Copyright (C) 2008 Buchan Milne                                            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char devmon_rcsid[] = "$Id $";

int do_devmon_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
#define MAXCOLS 20
	char *devmon_params[MAXCOLS+7] = { NULL, };

	char *eoln, *curline;
	static int ptnsetup = 0;
	static pcre *inclpattern = NULL;
	static pcre *exclpattern = NULL;
	int in_devmon = 1;
	int numds = 0;
	char *rrdbasename;
	int lineno = 0;

	rrdbasename = NULL;
	curline = msg;
	while (curline)  {
		char *fsline = NULL;
		char *p;
		char *columns[MAXCOLS];
		int columncount;
		char *ifname = NULL;
		int pused = -1;
		int wanteddisk = 1;
		long long aused = 0;
		char *dsval;
		int i;

		eoln = strchr(curline, '\n'); if (eoln) *eoln = '\0';
		lineno++;

		if(!strncmp(curline, "<!--DEVMON RRD: ",16)) {
			in_devmon = 0;
			/*if(rrdbasename) {xfree(rrdbasename);rrdbasename = NULL;}*/
			rrdbasename = strtok(curline+16," ");
			if (rrdbasename == NULL) rrdbasename = xstrdup(testname);
			dbgprintf("DEVMON: changing testname from %s to %s\n",testname,rrdbasename);
			numds = 0;
			goto nextline;
		}
		if(in_devmon == 0 && !strncmp(curline, "-->",3)) {
			in_devmon = 1;
			goto nextline;
		}
		if (in_devmon != 0 ) goto nextline;

		for (columncount=0; (columncount<MAXCOLS); columncount++) columns[columncount] = "";
		fsline = xstrdup(curline); columncount = 0; p = strtok(fsline, " ");
		while (p && (columncount < MAXCOLS)) { columns[columncount++] = p; p = strtok(NULL, " "); }

		/* DS:ds0:COUNTER:600:0:U DS:ds1:COUNTER:600:0:U */
		if (!strncmp(curline, "DS:",3)) {
			dbgprintf("Looking for DS defintions in %s\n",curline);
			while ( numds < MAXCOLS) {
				dbgprintf("Seeing if column %d that has %s is a DS\n",numds,columns[numds]);
				if (strncmp(columns[numds],"DS:",3)) break;
				devmon_params[numds] = xstrdup(columns[numds]);
				numds++;
			}
			dbgprintf("Found %d DS definitions\n",numds);
			devmon_params[numds] = NULL;

			goto nextline;
		}

		dbgprintf("Found %d columns in devmon rrd data\n",columncount);
		if (columncount > 2) {
			dbgprintf("Skipping line %d, found %d (max 2) columns in devmon rrd data, space in repeater name?\n",lineno,columncount);
			goto nextline;
		}

		/* Now we should be on to values:
		 * eth0.0 4678222:9966777
		 */
		ifname = xstrdup(columns[0]);
		dsval = strtok(columns[1],":");
		if (dsval == NULL) {
			dbgprintf("Skipping line %d, line is malformed\n",lineno);
			goto nextline;
		}
		sprintf(rrdvalues, "%d:", (int)tstamp);
		strcat(rrdvalues,dsval);
		for (i=1;i < numds;i++) {
			dsval = strtok(NULL,":");
			if (dsval == NULL) {
				dbgprintf("Skipping line %d, %d tokens present, expecting %d\n",lineno,i,numds);
				goto nextline;
			}
			strcat(rrdvalues,":");
			strcat(rrdvalues,dsval);
		}
		/* File names in the format if_load.eth0.0.rrd */
		setupfn2("%s.%s.rrd", rrdbasename, ifname);
		dbgprintf("Sending from devmon to RRD for %s %s: %s\n",rrdbasename,ifname,rrdvalues);
		create_and_update_rrd(hostname, testname, classname, pagepaths, devmon_params, NULL);
		if (ifname) { xfree(ifname); ifname = NULL; }

		if (eoln) *eoln = '\n';

nextline:
		if (fsline) { xfree(fsline); fsline = NULL; }
		curline = (eoln ? (eoln+1) : NULL);
	}

	return 0;
}
