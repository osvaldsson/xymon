/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for handling holidays.                                */
/*                                                                            */
/* Copyright (C) 2006-2008 Michael Nagel                                      */
/* Modifications for Xymon (C) 2007-2011 Henrik Storner <henrik@hswn.dk>      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: holidays.c 6745 2011-09-04 06:01:06Z storner $";

#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "libxymon.h"


static int holidays_like_weekday = -1;

typedef struct holidayset_t {
	char *key;
	holiday_t *head;
	holiday_t *tail;
} holidayset_t;

static void * holidays;
static int current_year = 0;

static time_t mkday(int year, int month, int day)
{
	struct tm t;

	memset(&t, 0, sizeof(t));
	t.tm_year  = year;
	t.tm_mon   = month - 1;
	t.tm_mday  = day;
	t.tm_isdst = -1;

	return mktime(&t);
}

/*
 * Algorithm for calculating the date of Easter Sunday
 * (Meeus/Jones/Butcher Gregorian algorithm)
 * For reference, see http://en.wikipedia.org/wiki/Computus
 */
static time_t getEasterDate(int year) 
{
	time_t day;
	int Y = year+1900;
	int a = Y % 19;
	int b = Y / 100;
	int c = Y % 100;
	int d = b / 4;
	int e = b % 4;
	int f = (b + 8) / 25;
	int g = (b - f + 1) / 3;
	int h = (19 * a + b - d - g + 15) % 30;
	int i = c / 4;
	int k = c % 4;
	int L = (32 + 2 * e + 2 * i - h - k) % 7;
	int m = (a + 11 * h + 22 * L) / 451;

	day = mkday(year, (h + L - 7 * m + 114) / 31, ((h + L - 7 * m + 114) % 31) + 1);

	return day;
}


/* Algorithm to compute the 4th Sunday in Advent (ie the last Sunday before Christmas Day) */
static time_t get4AdventDate(int year)
{
	time_t day;
	struct tm *t;

	day = mkday(year, 12, 24);
	t = localtime(&day);
	day -= t->tm_wday * 86400;

	return day;
}


static int getnumberedweekday(int wkday, int daynum, int month, int year)
{
	struct tm tm;
	time_t t;

	/* First see what weekday the 1st of this month is */
	memset(&tm, 0, sizeof(tm));
	tm.tm_mon = (month - 1);
	tm.tm_year = year;
	tm.tm_mday = 1;
	tm.tm_isdst = -1;
	t = mktime(&tm);
	if (tm.tm_wday != wkday) {
		/* Skip forward so we reach the first of the wanted weekdays */
		tm.tm_mday += (wkday - tm.tm_wday);
		if (tm.tm_mday < 1) tm.tm_mday += 7;
		tm.tm_isdst = -1;
		t = mktime(&tm);
	}

	/* t and tm now has the 1st wkday in this month. So skip to the one we want */
	tm.tm_mday += 7*(daynum - 1);
	/* Check if we overflowed into next month (if daynum == 5) */
	t = mktime(&tm);
	tm.tm_isdst = -1;
	if ((daynum == 5) && (tm.tm_mon != (month - 1))) {
		/* We drifted into the next month. Go back one week to return the last wkday of the month */
		tm.tm_mday -= 7;
		tm.tm_isdst = -1;
		t = mktime(&tm);
	}

	return tm.tm_yday;
}

static int getweekdayafter(int wkday, int daynum, int month, int year)
{
	struct tm tm;
	time_t t;

	/* First see what weekday this date is */
	memset(&tm, 0, sizeof(tm));
	tm.tm_mon = (month - 1);
	tm.tm_year = year;
	tm.tm_mday = daynum;
	tm.tm_isdst = -1;
	t = mktime(&tm);
	if (tm.tm_wday != wkday) {
		/* Skip forward so we reach the wanted weekday */
		tm.tm_mday += (wkday - tm.tm_wday);
		if (tm.tm_mday < daynum) tm.tm_mday += 7;
		tm.tm_isdst = -1;
		t = mktime(&tm);
	}

	return tm.tm_yday;
}


static void reset_holidays(void)
{
	static int firsttime = 1;
	xtreePos_t handle;
	holidayset_t *hset;
	holiday_t *walk, *zombie;

	if (!firsttime) {
		for (handle = xtreeFirst(holidays); (handle != xtreeEnd(holidays)); handle = xtreeNext(holidays, handle)) {
			hset = (holidayset_t *)xtreeData(holidays, handle);
			xfree(hset->key);

			walk = hset->head;
			while (walk) {
				zombie = walk;
				walk = walk->next;
				xfree(zombie->desc);
				xfree(zombie);
			}
		}

		xtreeDestroy(holidays);
	}

	holidays_like_weekday = -1;
	firsttime = 0;
	holidays = xtreeNew(strcasecmp);
}


static void add_holiday(char *key, int year, holiday_t *newhol)
{
	int isOK = 0;
	struct tm *t;
	time_t day;
	holiday_t *newitem;
	xtreePos_t handle;
	holidayset_t *hset;

	switch (newhol->holtype) {
	  case HOL_ABSOLUTE:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <=31) && (!newhol->year || (newhol->year == year)) );
		if (!isOK) break;
		day = mkday(year, newhol->month, newhol->day);
		t = localtime(&day);
		newhol->yday = t->tm_yday;
		break;

	  case HOL_EASTER:
		isOK = (newhol->month == 0); if (!isOK) break;
		day = getEasterDate(year);
		t = localtime(&day);
		newhol->yday = t->tm_yday + newhol->day;
		break;

	  case HOL_ADVENT:
		isOK = (newhol->month == 0); if (!isOK) break;
		day = get4AdventDate(year);
		t = localtime(&day);
		newhol->yday = t->tm_yday + newhol->day;
		break;

	  case HOL_MON:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 5) );
		if (!isOK) break;
		newhol->yday = getnumberedweekday(1, newhol->day, newhol->month, year);
		break;

	  case HOL_TUE:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 5) );
		if (!isOK) break;
		newhol->yday = getnumberedweekday(2, newhol->day, newhol->month, year);
		break;

	  case HOL_WED:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 5) );
		if (!isOK) break;
		newhol->yday = getnumberedweekday(3, newhol->day, newhol->month, year);
		break;

	  case HOL_THU:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 5) );
		if (!isOK) break;
		newhol->yday = getnumberedweekday(4, newhol->day, newhol->month, year);
		break;

	  case HOL_FRI:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 5) );
		if (!isOK) break;
		newhol->yday = getnumberedweekday(5, newhol->day, newhol->month, year);
		break;

	  case HOL_SAT:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 5) );
		if (!isOK) break;
		newhol->yday = getnumberedweekday(6, newhol->day, newhol->month, year);
		break;

	  case HOL_SUN:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 5) );
		if (!isOK) break;
		newhol->yday = getnumberedweekday(0, newhol->day, newhol->month, year);
		break;

	  case HOL_MON_AFTER:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 31) );
		if (!isOK) break;
		newhol->yday = getweekdayafter(1, newhol->day, newhol->month, year);
		break;

	  case HOL_TUE_AFTER:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 31) );
		if (!isOK) break;
		newhol->yday = getweekdayafter(2, newhol->day, newhol->month, year);
		break;

	  case HOL_WED_AFTER:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 31) );
		if (!isOK) break;
		newhol->yday = getweekdayafter(3, newhol->day, newhol->month, year);
		break;

	  case HOL_THU_AFTER:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 31) );
		if (!isOK) break;
		newhol->yday = getweekdayafter(4, newhol->day, newhol->month, year);
		break;

	  case HOL_FRI_AFTER:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 31) );
		if (!isOK) break;
		newhol->yday = getweekdayafter(5, newhol->day, newhol->month, year);
		break;

	  case HOL_SAT_AFTER:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 31) );
		if (!isOK) break;
		newhol->yday = getweekdayafter(6, newhol->day, newhol->month, year);
		break;

	  case HOL_SUN_AFTER:
		isOK = ( (newhol->month >= 1 && newhol->month <=12) && (newhol->day >=1 && newhol->day <= 31) );
		if (!isOK) break;
		newhol->yday = getweekdayafter(0, newhol->day, newhol->month, year);
		break;

	}

	if (!isOK) {
		errprintf("Error in holiday definition %s\n", newhol->desc);
		return;
	}

	newitem = (holiday_t *)calloc(1, sizeof(holiday_t));
	newitem->holtype = newhol->holtype;
	newitem->day = newhol->day;
	newitem->month = newhol->month;
	newitem->desc = strdup(newhol->desc);
	newitem->yday = newhol->yday;

	handle = xtreeFind(holidays, key);
	if (handle == xtreeEnd(holidays)) {
		hset = (holidayset_t *)calloc(1, sizeof(holidayset_t));
		hset->key = strdup(key);
		xtreeAdd(holidays, hset->key, hset);
	}
	else {
		hset = (holidayset_t *)xtreeData(holidays, handle);
	}

	if (hset->head == NULL) {
		hset->head = hset->tail = newitem;
	}
	else {
		hset->tail->next = newitem; hset->tail = hset->tail->next;
	}
}

static int record_compare(void **a, void **b)
{
	holiday_t **reca = (holiday_t **)a;
	holiday_t **recb = (holiday_t **)b;

	return ((*reca)->yday < (*recb)->yday);
}

static void * record_getnext(void *a)
{
	return ((holiday_t *)a)->next;
}

static void record_setnext(void *a, void *newval)
{
	((holiday_t *)a)->next = (holiday_t *)newval;
}



int load_holidays(int year)
{
	static void *configholidays = NULL;
	char fn[PATH_MAX];
	FILE *fd;
	strbuffer_t *inbuf;
	holiday_t newholiday;
	xtreePos_t handle, commonhandle;
	char *setname = NULL;
	holidayset_t *commonhols;

	MEMDEFINE(fn);

	if (year == 0) {
		time_t tnow;
		struct tm *now;
		tnow = getcurrenttime(NULL);
		now = localtime(&tnow);
		year = now->tm_year;
	}
	else if (year > 1000) {
		year -= 1900;
	}

	sprintf(fn, "%s/etc/holidays.cfg", xgetenv("XYMONHOME"));

	/* First check if there were no modifications at all */
	if (configholidays) {
		/* if the new year begins, the holidays have to be recalculated */
		if (!stackfmodified(configholidays) && (year == current_year)){
			dbgprintf("No files modified, skipping reload of %s\n", fn);
			MEMUNDEFINE(fn);
			return 0;
		}
		else {
			stackfclist(&configholidays);
			configholidays = NULL;
		}
	}

	reset_holidays();

	fd = stackfopen(fn, "r", &configholidays);
	if (!fd) {
		errprintf("Cannot open configuration file %s\n", fn);
		MEMUNDEFINE(fn);
		return 0;
	}

	memset(&newholiday,0,sizeof(holiday_t));
	inbuf = newstrbuffer(0);

	while (stackfgets(inbuf, NULL)) {
		char *p, *delim, *arg1, *arg2;

		sanitize_input(inbuf, 1, 0);
		if (STRBUFLEN(inbuf) == 0) continue;

		p = STRBUF(inbuf);
		if (strncasecmp(p, "HOLIDAYLIKEWEEKDAY=", 19) == 0)  {
			p+=19;
			holidays_like_weekday = atoi(p);
			if (holidays_like_weekday < -1 || holidays_like_weekday > 6) {
				holidays_like_weekday = -1;
				errprintf("Invalid HOLIDAYLIKEWEEKDAY in %s\n", fn);
			}

			continue;
		}

		if (*p == '[') {
			/* New set of holidays */
			if (setname) xfree(setname);
			delim = strchr(p, ']'); if (delim) *delim = '\0';
			setname = strdup(p+1);
			continue;
		}

		delim = strchr(p, ':');
		if (delim) {
			memset(&newholiday,0,sizeof(holiday_t));
			if (delim == p) {
				newholiday.desc = "untitled";
			}
			else {
				*delim = '\0';
				newholiday.desc = p;
				p=delim+1;
			}
		}

		arg1 = strtok(p, "=");
		while (arg1) {
			arg2=strtok(NULL," ,;\t\n\r");
			if (!arg2) break;
			if (strncasecmp(arg1, "TYPE", 4) == 0) {
				if      (strncasecmp(arg2, "STATIC", 6) == 0) newholiday.holtype = HOL_ABSOLUTE;
				else if (strncasecmp(arg2, "EASTER", 6) == 0) newholiday.holtype = HOL_EASTER;
				else if (strncasecmp(arg2, "4ADVENT", 7) == 0) newholiday.holtype = HOL_ADVENT;
				else if (strncasecmp(arg2, "MON", 3) == 0) newholiday.holtype = HOL_MON;
				else if (strncasecmp(arg2, "TUE", 3) == 0) newholiday.holtype = HOL_TUE;
				else if (strncasecmp(arg2, "WED", 3) == 0) newholiday.holtype = HOL_WED;
				else if (strncasecmp(arg2, "THU", 3) == 0) newholiday.holtype = HOL_THU;
				else if (strncasecmp(arg2, "FRI", 3) == 0) newholiday.holtype = HOL_FRI;
				else if (strncasecmp(arg2, "SAT", 3) == 0) newholiday.holtype = HOL_SAT;
				else if (strncasecmp(arg2, "SUN", 3) == 0) newholiday.holtype = HOL_SUN;
				else if (strncasecmp(arg2, "+MON", 4) == 0) newholiday.holtype = HOL_MON_AFTER;
				else if (strncasecmp(arg2, "+TUE", 4) == 0) newholiday.holtype = HOL_TUE_AFTER;
				else if (strncasecmp(arg2, "+WED", 4) == 0) newholiday.holtype = HOL_WED_AFTER;
				else if (strncasecmp(arg2, "+THU", 4) == 0) newholiday.holtype = HOL_THU_AFTER;
				else if (strncasecmp(arg2, "+FRI", 4) == 0) newholiday.holtype = HOL_FRI_AFTER;
				else if (strncasecmp(arg2, "+SAT", 4) == 0) newholiday.holtype = HOL_SAT_AFTER;
				else if (strncasecmp(arg2, "+SUN", 4) == 0) newholiday.holtype = HOL_SUN_AFTER;
			}
			else if (strncasecmp(arg1, "MONTH", 5) == 0) {
				newholiday.month=atoi(arg2);
			}
			else if (strncasecmp(arg1, "DAY", 3) == 0) {
				newholiday.day=atoi(arg2);
			}
			else if (strncasecmp(arg1, "OFFSET", 6) == 0) {
				newholiday.day=atoi(arg2);
			}
			else if (strncasecmp(arg1, "YEAR", 4) == 0) {
				newholiday.year=atoi(arg2);
				if (newholiday.year > 1000) {
			                newholiday.year -= 1900;
			        }
			}

			arg1 = strtok(NULL,"=");
		}

		add_holiday((setname ? setname : ""), year, &newholiday);
	}

	stackfclose(fd);
	freestrbuffer(inbuf);

	commonhandle = xtreeFind(holidays, "");
	commonhols = (commonhandle != xtreeEnd(holidays)) ? (holidayset_t *)xtreeData(holidays, commonhandle) : NULL;

	for (handle = xtreeFirst(holidays); (handle != xtreeEnd(holidays)); handle = xtreeNext(holidays, handle)) {
		holidayset_t *oneset = (holidayset_t *)xtreeData(holidays, handle);
		if (commonhols && (oneset != commonhols)) {
			/* Add the common holidays to this set */
			holiday_t *walk;

			for (walk = commonhols->head; (walk); walk = walk->next) add_holiday(oneset->key, year, walk);
		}

		oneset->head = msort(oneset->head, record_compare, record_getnext, record_setnext);
	}

	MEMUNDEFINE(fn);
	current_year = year;

	return 0;
}


static holiday_t *findholiday(char *key, int dayinyear)
{
	xtreePos_t handle;
	holidayset_t *hset;
	holiday_t *p;

	if (key && *key) {
		handle = xtreeFind(holidays, key);
		if (handle == xtreeEnd(holidays)) {
			key = NULL;
			handle = xtreeFind(holidays, "");
		}
	}
	else {
		key = NULL;
		handle = xtreeFind(holidays, "");
	}

	if (handle != xtreeEnd(holidays)) 
		hset = (holidayset_t *)xtreeData(holidays, handle);
	else
		return NULL;

	p = hset->head;
	while (p) {
		if (dayinyear == p->yday) {
			return p;
		}

		p = p->next;
	}

	return NULL;
}

int getweekdayorholiday(char *key, struct tm *t)
{
	holiday_t *rec;

	if (holidays_like_weekday == -1) return t->tm_wday;

	rec = findholiday(key, t->tm_yday);
	if (rec) return holidays_like_weekday;

	return t->tm_wday;
}

char *isholiday(char *key, int dayinyear)
{
	holiday_t *rec;

	rec = findholiday(key, dayinyear);
	if (rec) return rec->desc;

	return NULL;
}


void printholidays(char *key, strbuffer_t *buf, int mfirst, int mlast)
{
	int day;
	char *fmt;
	char oneh[1024];
	char dstr[1024];

	fmt = xgetenv("HOLIDAYFORMAT");

	for (day = 0; (day < 366); day++) {
		char *desc = isholiday(key, day);

		if (desc) {
			struct tm tm;
			time_t t;

			/*
			 * mktime() ignores the tm_yday parameter, so to get the 
			 * actual date for the "yday'th" day of the year we just set
			 * tm_mday to the yday value, and month to January. mktime() 
			 * will figure out that the 56th of January is really Feb 25.
			 *
			 * Note: tm_yday is zero-based, but tm_mday is 1-based!
			 */
			tm.tm_mon = 0; tm.tm_mday = day+1; tm.tm_year = current_year;
			tm.tm_hour = 12; tm.tm_min = 0; tm.tm_sec = 0;
			tm.tm_isdst = -1;
			t = mktime(&tm);
			if ((tm.tm_mon >= mfirst) && (tm.tm_mon <= mlast)) {
				strftime(dstr, sizeof(dstr), fmt, localtime(&t));
				sprintf(oneh, "<tr><td>%s</td><td>%s</td>\n", desc, dstr);
				addtobuffer(buf, oneh);
			}
		}
	}
}

#ifdef STANDALONE
int main(int argc, char *argv[])
{
	char l[1024];
	char *hset = NULL;
	char *p;
	strbuffer_t *sbuf = newstrbuffer(0);
	char *dayname[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

	load_holidays(0);
	do {
		printf("$E year, $4 year, $W daynum wkday month year, Setname\n? "); fflush(stdout);
		if (!fgets(l, sizeof(l), stdin)) return 0;
		p = strchr(l, '\n'); if (p) *p = '\0';
		if (hset) xfree(hset);
		hset = strdup(l);

		if (*hset == '$') {
			time_t t;
			struct tm *tm;
			int i;
			char *tok, *arg[5];

			i = 0; tok = strtok(hset, " ");
			while (tok) {
				arg[i] = tok;
				i++;
				tok = strtok(NULL, " ");
			}

			if (arg[0][1] == 'E') {
				t = getEasterDate(atoi(arg[1]) - 1900);
				tm = localtime(&t);
				printf("Easter Sunday %04d is %02d/%02d/%04d\n", atoi(arg[1]), 
					tm->tm_mday, tm->tm_mon+1, tm->tm_year+1900);
			}
			else if (arg[0][1] == '4') {
				t = get4AdventDate(atoi(arg[1]) - 1900);
				tm = localtime(&t);
				printf("4Advent %04d is %02d/%02d/%04d\n", atoi(arg[1]), 
					tm->tm_mday, tm->tm_mon+1, tm->tm_year+1900);
			}
			else if (arg[0][1] == 'W') {
				struct tm wtm;

				memset(&wtm, 0, sizeof(wtm));
				wtm.tm_mday = getnumberedweekday(atoi(arg[2]), atoi(arg[1]), atoi(arg[3]), atoi(arg[4])-1900) + 1;
				wtm.tm_mon = 0;
				wtm.tm_year = atoi(arg[4]) - 1900;
				wtm.tm_isdst = -1;
				mktime(&wtm);
				printf("The %d. %s in %02d/%04d is %02d/%02d/%04d\n", 
					atoi(arg[1]), dayname[atoi(arg[2])], atoi(arg[3]), atoi(arg[4]),
					wtm.tm_mday, wtm.tm_mon+1, wtm.tm_year+1900);
			}
			else if (arg[0][1] == 'A') {
				struct tm wtm;

				memset(&wtm, 0, sizeof(wtm));
				wtm.tm_mday = getweekdayafter(atoi(arg[2]), atoi(arg[1]), atoi(arg[3]), atoi(arg[4])-1900) + 1;
				wtm.tm_mon = 0;
				wtm.tm_year = atoi(arg[4]) - 1900;
				wtm.tm_isdst = -1;
				mktime(&wtm);
				printf("The %d. %s on or after %02d/%04d is %02d/%02d/%04d\n", 
					atoi(arg[1]), dayname[atoi(arg[2])], atoi(arg[3]), atoi(arg[4]),
					wtm.tm_mday, wtm.tm_mon+1, wtm.tm_year+1900);
			}
		}
		else {
			int year = atoi(hset);
			load_holidays(year);
			printholidays(hset, sbuf);
			printf("Holidays in set: %s\n", STRBUF(sbuf));
			clearstrbuffer(sbuf);
		}
	} while (1);

	return 0;
}

#endif

