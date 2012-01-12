/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __CLIENT_CONFIG_H__
#define __CLIENT_CONFIG_H__

#include "libxymon.h"

extern int load_client_config(char *configfn);
extern void dump_client_config(void);

extern void clearalertgroups(void);
extern char *getalertgroups(void);
extern void addalertgroup(char *group);

extern int get_cpu_thresholds(void *hinfo, char *classname, 
			      float *loadyellow, float *loadred, 
			      int *recentlimit, int *ancientlimit, int *uptimecolor,
			      int *maxclockdiff, int *clockdiffcolor);

extern int get_disk_thresholds(void *hinfo, char *classname,
				char *fsname,
				long *warnlevel, long *paniclevel,
				int *abswarn, int *abspanic,
				int *ignored, char **group);

extern int get_inode_thresholds(void *hinfo, char *classname,
				char *fsname,
				long *warnlevel, long *paniclevel,
				int *abswarn, int *abspanic,
				int *ignored, char **group);

extern void get_memory_thresholds(void *hhinfo, char *classname,
				  int *physyellow, int *physred, 
				  int *swapyellow, int *swapred, 
				  int *actyellow, int *actred);

extern void get_zos_memory_thresholds(void *hinfo, char *classname,
                               int *csayellow, int *csared, int *ecsayellow, int *ecsared,
                               int *sqayellow, int *sqared, int *esqayellow, int *esqared);

extern void get_cics_thresholds(void *hinfo, char *classname, char *appid,
                        int *dsayel, int *dsared, int *edsayel, int *edsared);

extern void get_zvsevsize_thresholds(void *hinfo, char *classname,
                        int *availyel, int *availred);

extern void get_zvsegetvis_thresholds(void *hinfo, char *classname, char *pid,
                        int *gv24yel, int *gv24red, int *gvanyyel, int *gvanyred);

extern void get_asid_thresholds(void *hinfo, char *classname,
                                 int *maxyellow, int *maxred);

extern int get_paging_thresholds(void *hinfo, char *classname, 
				 int *pagingyellow, int *pagingred);

extern int check_mibvals(void *hinfo, char *classname, 
			 char *mibname, char *keyname, char *mibdata,
		  	 strbuffer_t *summarybuf, int *anyrules);

extern char *check_rrdds_thresholds(char *hostname, char *classname, char *pagepaths, char *rrdkey, void *valnames, char *vals);


extern int scan_log(void *hinfo, char *classname, 
		    char *logname, char *logdata, char *section, strbuffer_t *summarybuf);
extern int check_file(void *hinfo, char *classname, 
		      char *filename, char *filedata, char *section, strbuffer_t *summarybuf, off_t *sz, 
		      char **id, int *trackit, int *anyrules);
extern int check_dir(void *hinfo, char *classname, 
		     char *filename, char *filedata, char *section, strbuffer_t *summarybuf, unsigned long *sz, 
		     char **id, int *trackit);

extern int clear_process_counts(void *hinfo, char *classname);
extern void add_process_count(char *pname);
extern char *check_process_count(int *pcount, int *lowlim, int *uplim, int *pcolor, char **id, int *trackit, char **group);

extern int clear_disk_counts(void *hinfo, char *classname);
extern void add_disk_count(char *dname);
extern char *check_disk_count(int *dcount, int *lowlim, int *uplim, int *dcolor, char **group);

extern int clear_inode_counts(void *hinfo, char *classname);
extern void add_inode_count(char *iname);
extern char *check_inode_count(int *icount, int *lowlim, int *uplim, int *icolor, char **group);

extern int clear_port_counts(void *hinfo, char *classname);
extern void add_port_count(char *spname, char *tpname, char *stname);
extern char *check_port_count(int *pcount, int *lowlim, int *uplim, int *pcolor, char **id, int *trackit, char **group);

extern int clear_svc_counts(void *hinfo, char *classname); 
extern void add_svc_count(char *spname, char *tpname, char *stname);
extern char *check_svc_count(int *pcount, int *pcolor, char **group);

extern void get_mqqueue_thresholds(void *hinfo, char *classname, char *qmgrname, char *qname, int *warnlen, int *critlen, int *warnage, int *critage, char **trackit);
extern int get_mqchannel_params(void *hinfo, char *classname, char *qmgrname, char *chnname, char *chnstatus, int *color);

#endif

