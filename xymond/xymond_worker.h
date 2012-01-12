/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __XYMOND_WORKER_H__
#define __XYMOND_WORKER_H__

#include <sys/time.h>

#include "xymond_ipc.h"
typedef void (update_fn_t)(char *);

extern int net_worker_option(char *arg);
extern int net_worker_locatorbased(void);
extern void net_worker_run(enum locator_servicetype_t svc, enum locator_sticky_t sticky, update_fn_t *updfunc);
extern unsigned char *get_xymond_message(enum msgchannels_t chnid, char *id, int *seq, struct timespec *timeout);

#endif

