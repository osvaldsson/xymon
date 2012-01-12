/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* This is a xymond worker module for the "stachg" channel.                   */
/* This module implements the file-based history logging, and keeps the       */
/* historical logfiles in $XYMONVAR/hist/ and $XYMONVAR/histlogs/ updated     */
/* to keep track of the status changes.                                       */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: xymond_history.c 6748 2011-09-04 17:24:36Z storner $";

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <sys/wait.h>
#include <time.h>
#include <limits.h>

#include "libxymon.h"

#include "xymond_worker.h"

int rotatefiles = 0;
time_t nextfscheck = 0;

void sig_handler(int signum)
{
	/*
	 * Why this? Because we must have our own signal handler installed to call wait()
	 */
	switch (signum) {
	  case SIGCHLD:
		  break;

	  case SIGHUP:
		  rotatefiles = 1;
		  nextfscheck = 0;
		  break;
	}
}

typedef struct columndef_t {
	char *name;
	int saveit;
} columndef_t;
void * columndefs;

int main(int argc, char *argv[])
{
	time_t starttime = gettimer();
	char *histdir = NULL;
	char *histlogdir = NULL;
	char *msg;
	int argi, seq;
	int save_allevents = 1;
	int save_hostevents = 1;
	int save_statusevents = 1;
	int save_histlogs = 1, defaultsaveop = 1;
	FILE *alleventsfd = NULL;
	int running = 1;
	struct sigaction sa;
	char newcol2[3];
	char oldcol2[3];
	char alleventsfn[PATH_MAX];
	char pidfn[PATH_MAX];
	int logdirfull = 0;
	int minlogspace = 5;

	MEMDEFINE(pidfn);
	MEMDEFINE(alleventsfn);
	MEMDEFINE(newcol2);
	MEMDEFINE(oldcol2);

	/* Dont save the error buffer */
	save_errbuf = 0;

	sprintf(pidfn, "%s/xymond_history.pid", xgetenv("XYMONSERVERLOGS"));
	if (xgetenv("XYMONALLHISTLOG")) save_allevents = (strcmp(xgetenv("XYMONALLHISTLOG"), "TRUE") == 0);
	if (xgetenv("XYMONHOSTHISTLOG")) save_hostevents = (strcmp(xgetenv("XYMONHOSTHISTLOG"), "TRUE") == 0);
	if (xgetenv("SAVESTATUSLOG")) save_histlogs = (strncmp(xgetenv("SAVESTATUSLOG"), "FALSE", 5) != 0);

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--histdir=")) {
			histdir = strchr(argv[argi], '=')+1;
		}
		else if (argnmatch(argv[argi], "--histlogdir=")) {
			histlogdir = strchr(argv[argi], '=')+1;
		}
		else if (argnmatch(argv[argi], "--pidfile=")) {
			strcpy(pidfn, strchr(argv[argi], '=')+1);
		}
		else if (argnmatch(argv[argi], "--minimum-free=")) {
			minlogspace = atoi(strchr(argv[argi], '=')+1);
		}
		else if (argnmatch(argv[argi], "--debug")) {
			debug = 1;
		}
	}

	if (xgetenv("XYMONHISTDIR") && (histdir == NULL)) {
		histdir = strdup(xgetenv("XYMONHISTDIR"));
	}
	if (histdir == NULL) {
		errprintf("No history directory given, aborting\n");
		return 1;
	}

	if (save_histlogs && (histlogdir == NULL) && xgetenv("XYMONHISTLOGS")) {
		histlogdir = strdup(xgetenv("XYMONHISTLOGS"));
	}
	if (save_histlogs && (histlogdir == NULL)) {
		errprintf("No history-log directory given, aborting\n");
		return 1;
	}

	columndefs = xtreeNew(strcmp);
	{
		char *defaultsave, *tok;
		char *savelist;
		columndef_t *newrec;

		savelist = strdup(xgetenv("SAVESTATUSLOG"));
		defaultsave = strtok(savelist, ","); 
		/*
		 * TRUE: Save everything by default; may list some that are not saved.
		 * ONLY: Save nothing by default; may list some that are saved.
		 * FALSE: Save nothing.
		 */
		defaultsaveop = (strcasecmp(defaultsave, "TRUE") == 0);
		tok = strtok(NULL, ",");
		while (tok) {
			newrec = (columndef_t *)malloc(sizeof(columndef_t));
			if (*tok == '!') {
				newrec->saveit = 0;
				newrec->name = strdup(tok+1);
			}
			else {
				newrec->saveit = 1;
				newrec->name = strdup(tok);
			}
			xtreeAdd(columndefs, newrec->name, newrec);

			tok = strtok(NULL, ",");
		}
		xfree(savelist);
	}

	{
		FILE *pidfd = fopen(pidfn, "w");
		if (pidfd) {
			fprintf(pidfd, "%d\n", getpid());
			fclose(pidfd);
		}
	}

	sprintf(alleventsfn, "%s/allevents", histdir);
	if (save_allevents) {
		alleventsfd = fopen(alleventsfn, "a");
		if (alleventsfd == NULL) {
			errprintf("Cannot open the all-events file '%s'\n", alleventsfn);
		}
		setvbuf(alleventsfd, (char *)NULL, _IOLBF, 0);
	}

	/* For picking up lost children */
	setup_signalhandler("xymond_history");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	signal(SIGPIPE, SIG_DFL);

	while (running) {
		char *metadata[20] = { NULL, };
		int metacount;
		char *p;
		char *statusdata = "";
		char *hostname, *hostnamecommas, *testname, *dismsg, *modifiers;
		time_t tstamp, lastchg, disabletime, clienttstamp;
		int tstamp_i, lastchg_i;
		int newcolor, oldcolor;
		int downtimeactive;
		struct tm tstamptm;
		int trend;
		int childstat;

		/* Pickup any finished child processes to avoid zombies */
		while (wait3(&childstat, WNOHANG, NULL) > 0) ;

		if (rotatefiles && alleventsfd) {
			fclose(alleventsfd);
			alleventsfd = fopen(alleventsfn, "a");
			if (alleventsfd == NULL) {
				errprintf("Cannot re-open the all-events file '%s'\n", alleventsfn);
			}
			else {
				setvbuf(alleventsfd, (char *)NULL, _IOLBF, 0);
			}
		}

		msg = get_xymond_message(C_STACHG, "xymond_history", &seq, NULL);
		if (msg == NULL) {
			running = 0;
			continue;
		}

		if (nextfscheck < gettimer()) {
			logdirfull = (chkfreespace(histlogdir, minlogspace, minlogspace) != 0);
			if (logdirfull) errprintf("Historylog directory %s has less than %d%% free space - disabling save of data for 5 minutes\n", histlogdir, minlogspace);
			nextfscheck = gettimer() + 300;
		}

		p = strchr(msg, '\n'); 
		if (p) {
			*p = '\0'; 
			statusdata = msg_data(p+1);
		}
		metacount = 0;
		memset(&metadata, 0, sizeof(metadata));
		p = gettok(msg, "|");
		while (p && (metacount < 20)) {
			metadata[metacount++] = p;
			p = gettok(NULL, "|");
		}

		if ((metacount > 9) && (strncmp(metadata[0], "@@stachg", 8) == 0)) {
			xtreePos_t handle;
			columndef_t *saveit = NULL;

			/* @@stachg#seq|timestamp|sender|origin|hostname|testname|expiretime|color|prevcolor|changetime|disabletime|disablemsg|downtimeactive|clienttstamp|modifiers */
			sscanf(metadata[1], "%d.%*d", &tstamp_i); tstamp = tstamp_i;
			hostname = metadata[4];
			testname = metadata[5];
			newcolor = parse_color(metadata[7]);
			oldcolor = parse_color(metadata[8]);
			lastchg  = atoi(metadata[9]);
			disabletime = atoi(metadata[10]);
			dismsg   = metadata[11];
			downtimeactive = (atoi(metadata[12]) > 0);
			clienttstamp = atoi(metadata[13]);
			modifiers = metadata[14];

			if (newcolor == -1) {
				errprintf("Bad message: newcolor is unknown '%s'\n", metadata[7]);
				continue;
			}

			p = hostnamecommas = strdup(hostname); while ((p = strchr(p, '.')) != NULL) *p = ',';

			handle = xtreeFind(columndefs, testname);
			if (handle == xtreeEnd(columndefs)) {
				saveit = (columndef_t *)malloc(sizeof(columndef_t));
				saveit->name = strdup(testname);
				saveit->saveit = defaultsaveop;
				xtreeAdd(columndefs, saveit->name, saveit);
			}
			else {
				saveit = (columndef_t *) xtreeData(columndefs, handle);
			}

			if (save_statusevents) {
				char statuslogfn[PATH_MAX];
				int logexists;
				FILE *statuslogfd;
				char oldcol[100];
				char timestamp[40];
				struct stat st;

				MEMDEFINE(statuslogfn);
				MEMDEFINE(oldcol);
				MEMDEFINE(timestamp);

				sprintf(statuslogfn, "%s/%s.%s", histdir, hostnamecommas, testname);
				stat(statuslogfn, &st);
				statuslogfd = fopen(statuslogfn, "r+");
				logexists = (statuslogfd != NULL);
				*oldcol = '\0';

				if (logexists) {
					/*
					 * There is a fair chance xymond has not been
					 * running all the time while this system was monitored.
					 * So get the time of the latest status change from the file,
					 * instead of relying on the "lastchange" value we get
					 * from xymond. This is also needed when migrating from 
					 * standard bbd to xymond.
					 */
					off_t pos = -1;
					char l[1024];
					int gotit;

					MEMDEFINE(l);

					fseeko(statuslogfd, 0, SEEK_END);
					if (ftello(statuslogfd) > 512) {
						/* Go back 512 from EOF, and skip to start of a line */
						fseeko(statuslogfd, -512, SEEK_END);
						gotit = (fgets(l, sizeof(l)-1, statuslogfd) == NULL);
					}
					else {
						/* Read from beginning of file */
						fseeko(statuslogfd, 0, SEEK_SET);
						gotit = 0;
					}


					while (!gotit) {
						off_t tmppos = ftello(statuslogfd);
						time_t dur;
						int dur_i;

						if (fgets(l, sizeof(l)-1, statuslogfd)) {
							/* Sun Oct 10 06:49:42 2004 red   1097383782 602 */

							if ((strlen(l) > 24) && 
							    (sscanf(l+24, " %s %d %d", oldcol, &lastchg_i, &dur_i) == 2) &&
							    (parse_color(oldcol) != -1)) {
								/* 
								 * Record the start location of the line
								 */
								pos = tmppos;
								lastchg = lastchg_i;
								dur = dur_i;
							}
						}
						else {
							gotit = 1;
						}
					}

					if (pos == -1) {
						/* 
						 * Couldnt find anything in the log.
						 * Take lastchg from the timestamp of the logfile,
						 * and just append the data.
						 */
						lastchg = st.st_mtime;
						fseeko(statuslogfd, 0, SEEK_END);
					}
					else {
						/*
						 * lastchg was updated above.
						 * Seek to where the last line starts.
						 */
						fseeko(statuslogfd, pos, SEEK_SET);
					}

					MEMUNDEFINE(l);
				}
				else {
					/*
					 * Logfile does not exist.
					 */
					lastchg = tstamp;
					statuslogfd = fopen(statuslogfn, "a");
					if (statuslogfd == NULL) {
						errprintf("Cannot open status historyfile '%s' : %s\n", 
							statuslogfn, strerror(errno));
					}
				}

				if (strcmp(oldcol, colorname(newcolor)) == 0) {
					/* We wont update history unless the color did change. */
					if ((gettimer() - starttime) > 300) {
						errprintf("Will not update %s - color unchanged (%s)\n", 
							  statuslogfn, oldcol);
					}

					if (hostnamecommas) xfree(hostnamecommas);
					if (statuslogfd) fclose(statuslogfd);

					MEMUNDEFINE(statuslogfn);
					MEMUNDEFINE(oldcol);
					MEMUNDEFINE(timestamp);

					continue;
				}

				if (statuslogfd) {
					if (logexists) {
						struct tm oldtm;

						/* Re-print the old record, now with the final duration */
						memcpy(&oldtm, localtime(&lastchg), sizeof(oldtm));
						strftime(timestamp, sizeof(timestamp), "%a %b %e %H:%M:%S %Y", &oldtm);
						fprintf(statuslogfd, "%s %s %d %d\n", 
							timestamp, oldcol, (int)lastchg, (int)(tstamp - lastchg));
					}

					/* And the new record. */
					memcpy(&tstamptm, localtime(&tstamp), sizeof(tstamptm));
					strftime(timestamp, sizeof(timestamp), "%a %b %e %H:%M:%S %Y", &tstamptm);
					fprintf(statuslogfd, "%s %s %d", timestamp, colorname(newcolor), (int)tstamp);

					fclose(statuslogfd);
				}

				MEMUNDEFINE(statuslogfn);
				MEMUNDEFINE(oldcol);
				MEMUNDEFINE(timestamp);
			}

			if (save_histlogs && saveit->saveit && !logdirfull) {
				char *hostdash;
				char fname[PATH_MAX];
				FILE *histlogfd;

				MEMDEFINE(fname);

				p = hostdash = strdup(hostname); while ((p = strchr(p, '.')) != NULL) *p = '_';
				sprintf(fname, "%s/%s", histlogdir, hostdash);
				mkdir(fname, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
				p = fname + sprintf(fname, "%s/%s/%s", histlogdir, hostdash, testname);
				mkdir(fname, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
				p += sprintf(p, "/%s", histlogtime(tstamp));

				histlogfd = fopen(fname, "w");
				if (histlogfd) {
					/*
					 * When a host gets disabled or goes purple, the status
					 * message data is not changed - so it will include a
					 * wrong color as the first word of the message.
					 * Therefore we need to fixup this so it matches the
					 * newcolor value.
					 */
					int txtcolor = parse_color(statusdata);
					char *origstatus = statusdata;
					char *eoln, *restofdata;
					int written, closestatus, ok = 1;

					if (txtcolor != -1) {
						fprintf(histlogfd, "%s", colorname(newcolor));
						statusdata += strlen(colorname(txtcolor));
					}

					if (dismsg && *dismsg) nldecode(dismsg);
					if (disabletime > 0) {
						fprintf(histlogfd, " Disabled until %s\n%s\n\n", 
							ctime(&disabletime), (dismsg ? dismsg : ""));
						fprintf(histlogfd, "Status message when disabled follows:\n\n");
						statusdata = origstatus;
					}
					else if (dismsg && *dismsg) {
						fprintf(histlogfd, " Planned downtime: %s\n\n", dismsg);
						fprintf(histlogfd, "Original status message follows:\n\n");
						statusdata = origstatus;
					}

					restofdata = statusdata;
					if (modifiers && *modifiers) {
						char *modtxt;

						/* We must finish writing the first line before putting in the modifiers */
						eoln = strchr(restofdata, '\n');
						if (eoln) {
							restofdata = eoln+1;
							*eoln = '\0';
							fprintf(histlogfd, "%s\n", statusdata);
						}

						nldecode(modifiers);
						modtxt = strtok(modifiers, "\n");
						while (modtxt) {
							fprintf(histlogfd, "%s\n", modtxt);
							modtxt = strtok(NULL, "\n");
						}
					}

					written = fwrite(restofdata, 1, strlen(restofdata), histlogfd);
					if (written != strlen(restofdata)) {
						ok = 0;
						errprintf("Error writing to file %s: %s\n", fname, strerror(errno));
						closestatus = fclose(histlogfd); /* Ignore any errors on close */
					}
					else {
						fprintf(histlogfd, "Status unchanged in 0.00 minutes\n");
						fprintf(histlogfd, "Message received from %s\n", metadata[2]);
						if (clienttstamp) fprintf(histlogfd, "Client data ID %d\n", (int) clienttstamp);
						closestatus = fclose(histlogfd);
						if (closestatus != 0) {
							ok = 0;
							errprintf("Error writing to file %s: %s\n", fname, strerror(errno));
						}
					}

					if (!ok) remove(fname);
				}
				else {
					errprintf("Cannot create histlog file '%s' : %s\n", fname, strerror(errno));
				}
				xfree(hostdash);

				MEMUNDEFINE(fname);
			}

			strncpy(oldcol2, ((oldcolor >= 0) ? colorname(oldcolor) : "-"), 2);
			strncpy(newcol2, colorname(newcolor), 2);
			newcol2[2] = oldcol2[2] = '\0';

			if (oldcolor == -1)           trend = -1;	/* we dont know how bad it was */
			else if (newcolor > oldcolor) trend = 2;	/* It's getting worse */
			else if (newcolor < oldcolor) trend = 1;	/* It's getting better */
			else                          trend = 0;	/* Shouldn't happen ... */

			if (save_hostevents) {
				char hostlogfn[PATH_MAX];
				FILE *hostlogfd;

				MEMDEFINE(hostlogfn);

				sprintf(hostlogfn, "%s/%s", histdir, hostname);
				hostlogfd = fopen(hostlogfn, "a");
				if (hostlogfd) {
					fprintf(hostlogfd, "%s %d %d %d %s %s %d\n",
						testname, (int)tstamp, (int)lastchg, (int)(tstamp - lastchg),
						newcol2, oldcol2, trend);
					fclose(hostlogfd);
				}
				else {
					errprintf("Cannot open host logfile '%s' : %s\n", hostlogfn, strerror(errno));
				}

				MEMUNDEFINE(hostlogfn);
			}

			if (save_allevents) {
				fprintf(alleventsfd, "%s %s %d %d %d %s %s %d\n",
					hostname, testname, (int)tstamp, (int)lastchg, (int)(tstamp - lastchg),
					newcol2, oldcol2, trend);
				fflush(alleventsfd);
			}

			xfree(hostnamecommas);
		}
		else if ((metacount > 3) && ((strncmp(metadata[0], "@@drophost", 10) == 0))) {
			/* @@drophost|timestamp|sender|hostname */

			hostname = metadata[3];

			if (save_histlogs) {
				char *hostdash;
				char testdir[PATH_MAX];

				MEMDEFINE(testdir);

				/* Remove all directories below the host-specific histlog dir */
				p = hostdash = strdup(hostname); while ((p = strchr(p, '.')) != NULL) *p = '_';
				sprintf(testdir, "%s/%s", histlogdir, hostdash);
				dropdirectory(testdir, 1);
				xfree(hostdash);

				MEMUNDEFINE(testdir);
			}

			if (save_hostevents) {
				char hostlogfn[PATH_MAX];
				struct stat st;

				MEMDEFINE(hostlogfn);

				sprintf(hostlogfn, "%s/%s", histdir, hostname);
				if ((stat(hostlogfn, &st) == 0) && S_ISREG(st.st_mode)) {
					unlink(hostlogfn);
				}

				MEMUNDEFINE(hostlogfn);
			}

			if (save_statusevents) {
				DIR *dirfd;
				struct dirent *de;
				char *hostlead;
				char statuslogfn[PATH_MAX];
				struct stat st;

				MEMDEFINE(statuslogfn);

				/* Remove $XYMONVAR/hist/host,name.* */
				p = hostnamecommas = strdup(hostname); while ((p = strchr(p, '.')) != NULL) *p = ',';
				hostlead = malloc(strlen(hostname) + 2);
				strcpy(hostlead, hostnamecommas); strcat(hostlead, ".");

				dirfd = opendir(histdir);
				if (dirfd) {
					while ((de = readdir(dirfd)) != NULL) {
						if (strncmp(de->d_name, hostlead, strlen(hostlead)) == 0) {
							sprintf(statuslogfn, "%s/%s", histdir, de->d_name);
							if ((stat(statuslogfn, &st) == 0) && S_ISREG(st.st_mode)) {
								unlink(statuslogfn);
							}
						}
					}
					closedir(dirfd);
				}

				xfree(hostlead);
				xfree(hostnamecommas);

				MEMUNDEFINE(statuslogfn);
			}
		}
		else if ((metacount > 4) && ((strncmp(metadata[0], "@@droptest", 10) == 0))) {
			/* @@droptest|timestamp|sender|hostname|testname */

			hostname = metadata[3];
			testname = metadata[4];

			if (save_histlogs) {
				char *hostdash;
				char testdir[PATH_MAX];

				MEMDEFINE(testdir);

				p = hostdash = strdup(hostname); while ((p = strchr(p, '.')) != NULL) *p = '_';
				sprintf(testdir, "%s/%s/%s", histlogdir, hostdash, testname);
				dropdirectory(testdir, 1);
				xfree(hostdash);

				MEMUNDEFINE(testdir);
			}

			if (save_statusevents) {
				char *hostnamecommas;
				char statuslogfn[PATH_MAX];
				struct stat st;

				MEMDEFINE(statuslogfn);

				p = hostnamecommas = strdup(hostname); while ((p = strchr(p, '.')) != NULL) *p = ',';
				sprintf(statuslogfn, "%s/%s.%s", histdir, hostnamecommas, testname);
				if ((stat(statuslogfn, &st) == 0) && S_ISREG(st.st_mode)) unlink(statuslogfn);
				xfree(hostnamecommas);

				MEMUNDEFINE(statuslogfn);
			}
		}
		else if ((metacount > 4) && ((strncmp(metadata[0], "@@renamehost", 12) == 0))) {
			/* @@renamehost|timestamp|sender|hostname|newhostname */
			char *newhostname;

			hostname = metadata[3];
			newhostname = metadata[4];

			if (save_histlogs) {
				char *hostdash;
				char *newhostdash;
				char olddir[PATH_MAX];
				char newdir[PATH_MAX];

				MEMDEFINE(olddir); MEMDEFINE(newdir);

				p = hostdash = strdup(hostname); while ((p = strchr(p, '.')) != NULL) *p = '_';
				p = newhostdash = strdup(newhostname); while ((p = strchr(p, '.')) != NULL) *p = '_';
				sprintf(olddir, "%s/%s", histlogdir, hostdash);
				sprintf(newdir, "%s/%s", histlogdir, newhostdash);
				rename(olddir, newdir);
				xfree(newhostdash);
				xfree(hostdash);

				MEMUNDEFINE(newdir); MEMUNDEFINE(olddir);
			}

			if (save_hostevents) {
				char hostlogfn[PATH_MAX];
				char newhostlogfn[PATH_MAX];

				MEMDEFINE(hostlogfn); MEMDEFINE(newhostlogfn);

				sprintf(hostlogfn, "%s/%s", histdir, hostname);
				sprintf(newhostlogfn, "%s/%s", histdir, newhostname);
				rename(hostlogfn, newhostlogfn);

				MEMUNDEFINE(hostlogfn); MEMUNDEFINE(newhostlogfn);
			}

			if (save_statusevents) {
				DIR *dirfd;
				struct dirent *de;
				char *hostlead;
				char *newhostnamecommas;
				char statuslogfn[PATH_MAX];
				char newlogfn[PATH_MAX];

				MEMDEFINE(statuslogfn); MEMDEFINE(newlogfn);

				p = hostnamecommas = strdup(hostname); while ((p = strchr(p, '.')) != NULL) *p = ',';
				hostlead = malloc(strlen(hostname) + 2);
				strcpy(hostlead, hostnamecommas); strcat(hostlead, ".");

				p = newhostnamecommas = strdup(newhostname); while ((p = strchr(p, '.')) != NULL) *p = ',';


				dirfd = opendir(histdir);
				if (dirfd) {
					while ((de = readdir(dirfd)) != NULL) {
						if (strncmp(de->d_name, hostlead, strlen(hostlead)) == 0) {
							char *testname = strchr(de->d_name, '.');
							sprintf(statuslogfn, "%s/%s", histdir, de->d_name);
							sprintf(newlogfn, "%s/%s%s", histdir, newhostnamecommas, testname);
							rename(statuslogfn, newlogfn);
						}
					}
					closedir(dirfd);
				}

				xfree(newhostnamecommas);
				xfree(hostlead);
				xfree(hostnamecommas);

				MEMUNDEFINE(statuslogfn); MEMUNDEFINE(newlogfn);
			}
		}
		else if ((metacount > 5) && (strncmp(metadata[0], "@@renametest", 12) == 0)) {
			/* @@renametest|timestamp|sender|hostname|oldtestname|newtestname */
			char *newtestname;

			hostname = metadata[3];
			testname = metadata[4];
			newtestname = metadata[5];

			if (save_histlogs) {
				char *hostdash;
				char olddir[PATH_MAX];
				char newdir[PATH_MAX];

				MEMDEFINE(olddir); MEMDEFINE(newdir);

				p = hostdash = strdup(hostname); while ((p = strchr(p, '.')) != NULL) *p = '_';
				sprintf(olddir, "%s/%s/%s", histlogdir, hostdash, testname);
				sprintf(newdir, "%s/%s/%s", histlogdir, hostdash, newtestname);
				rename(olddir, newdir);
				xfree(hostdash);

				MEMUNDEFINE(newdir); MEMUNDEFINE(olddir);
			}

			if (save_statusevents) {
				char *hostnamecommas;
				char statuslogfn[PATH_MAX];
				char newstatuslogfn[PATH_MAX];

				MEMDEFINE(statuslogfn); MEMDEFINE(newstatuslogfn);

				p = hostnamecommas = strdup(hostname); while ((p = strchr(p, '.')) != NULL) *p = ',';
				sprintf(statuslogfn, "%s/%s.%s", histdir, hostnamecommas, testname);
				sprintf(newstatuslogfn, "%s/%s.%s", histdir, hostnamecommas, newtestname);
				rename(statuslogfn, newstatuslogfn);
				xfree(hostnamecommas);

				MEMUNDEFINE(newstatuslogfn); MEMUNDEFINE(statuslogfn);
			}
		}
		else if (strncmp(metadata[0], "@@idle", 6) == 0) {
			/* Nothing */
		}
		else if (strncmp(metadata[0], "@@shutdown", 10) == 0) {
			running = 0;
		}
		else if (strncmp(metadata[0], "@@logrotate", 11) == 0) {
			char *fn = xgetenv("XYMONCHANNEL_LOGFILENAME");
			if (fn && strlen(fn)) {
				freopen(fn, "a", stdout);
				freopen(fn, "a", stderr);
			}
			continue;
		}
		else if (strncmp(metadata[0], "@@reload", 8) == 0) {
			/* Do nothing */
		}
	}

	MEMUNDEFINE(newcol2);
	MEMUNDEFINE(oldcol2);
	MEMUNDEFINE(alleventsfn);
	MEMUNDEFINE(pidfn);

	fclose(alleventsfd);
	unlink(pidfn);

	return 0;
}

