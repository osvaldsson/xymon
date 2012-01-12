/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* This module implements the setup/teardown of the xymond communications     */
/* channel, using standard System V IPC mechanisms: Shared memory and         */
/* semaphores.                                                                */
/*                                                                            */
/* The concept is to use a shared memory segment for each "channel" that      */
/* xymond supports. This memory segment is used to pass a single xymond       */
/* message between the xymond master daemon, and the xymond_channel workers.  */
/* Two semaphores are used to synchronize between the master daemon and the   */
/* workers, i.e. the workers wait for a semaphore to go up indicating that a  */
/* new message has arrived, and the master daemon then waits for the other    */
/* semaphore to go 0 indicating that the workers have read the message. A     */
/* third semaphore is used as a simple counter to tell how many workers have  */
/* attached to a channel.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: xymond_ipc.c 6712 2011-07-31 21:01:52Z storner $";

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "libxymon.h"

#include "xymond_ipc.h"

char *channelnames[C_LAST+1] = {
	"",		/* First one is index 0 - not used */
	"status", 
	"stachg",
	"page",
	"data",
	"notes",
	"enadis",
	"client",
	"clichg",
	"user",
	NULL
};

xymond_channel_t *setup_channel(enum msgchannels_t chnid, int role)
{
	key_t key;
	struct stat st;
	struct sembuf s;
	xymond_channel_t *newch;
	unsigned int bufsz;
	int flags = ((role == CHAN_MASTER) ? (IPC_CREAT | 0600) : 0);
	char *xymonhome = xgetenv("XYMONHOME");

	if ( (xymonhome == NULL) || (stat(xymonhome, &st) == -1) ) {
		errprintf("XYMONHOME not defined, or points to invalid directory - cannot continue.\n");
		return NULL;
	}

	bufsz = 1024*shbufsz(chnid);
	dbgprintf("Setting up %s channel (id=%d)\n", channelnames[chnid], chnid);

	dbgprintf("calling ftok('%s',%d)\n", xymonhome, chnid);
	key = ftok(xymonhome, chnid);
	if (key == -1) {
		errprintf("Could not generate shmem key based on %s: %s\n", xymonhome, strerror(errno));
		return NULL;
	}
	dbgprintf("ftok() returns: 0x%X\n", key);

	newch = (xymond_channel_t *)malloc(sizeof(xymond_channel_t));
	newch->seq = 0;
	newch->channelid = chnid;
	newch->msgcount = 0;
	newch->shmid = shmget(key, bufsz, flags);
	if (newch->shmid == -1) {
		errprintf("Could not get shm of size %d: %s\n", bufsz, strerror(errno));
		xfree(newch);
		return NULL;
	}
	dbgprintf("shmget() returns: 0x%X\n", newch->shmid);

	newch->channelbuf = (char *) shmat(newch->shmid, NULL, 0);
	if (newch->channelbuf == (char *)-1) {
		errprintf("Could not attach shm %s\n", strerror(errno));
		if (role == CHAN_MASTER) shmctl(newch->shmid, IPC_RMID, NULL);
		xfree(newch);
		return NULL;
	}

	newch->semid = semget(key, 3, flags);
	if (newch->semid == -1) {
		errprintf("Could not get sem: %s\n", strerror(errno));
		shmdt(newch->channelbuf);
		if (role == CHAN_MASTER) shmctl(newch->shmid, IPC_RMID, NULL);
		xfree(newch);
		return NULL;
	}

	if (role == CHAN_CLIENT) {
		/*
		 * Clients must register their presence.
		 * We use SEM_UNDO; so if the client crashes, it wont leave a stale count.
		 */
		s.sem_num = CLIENTCOUNT; s.sem_op = +1; s.sem_flg = SEM_UNDO;
		if (semop(newch->semid, &s, 1) == -1) {
			errprintf("Could not register presence: %s\n", strerror(errno));
			shmdt(newch->channelbuf);
			xfree(newch);
			return NULL;
		}
	}
	else if (role == CHAN_MASTER) {
		int n;

		n = semctl(newch->semid, CLIENTCOUNT, GETVAL);
		if (n > 0) {
			errprintf("FATAL: xymond sees clientcount %d, should be 0\nCheck for hanging xymond_channel processes or stale semaphores\n", n);
			shmdt(newch->channelbuf);
			shmctl(newch->shmid, IPC_RMID, NULL);
			semctl(newch->semid, 0, IPC_RMID);
			xfree(newch);
			return NULL;
		}
	}

#ifdef MEMORY_DEBUG
	add_to_memlist(newch->channelbuf, bufsz);
#endif
	return newch;
}

void close_channel(xymond_channel_t *chn, int role)
{
	if (chn == NULL) return;

	/* No need to de-register, this happens automatically because we registered with SEM_UNDO */

	if (role == CHAN_MASTER) semctl(chn->semid, 0, IPC_RMID);

	MEMUNDEFINE(chn->channelbuf);
	shmdt(chn->channelbuf);
	if (role == CHAN_MASTER) shmctl(chn->shmid, IPC_RMID, NULL);
}

