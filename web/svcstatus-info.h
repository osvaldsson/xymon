/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __SVCSTATUS_INFO_H__
#define __SVCSTATUS_INFO_H__

extern int showenadis;
extern int usejsvalidation;
extern int newcritconfig;

extern char *generate_info(char *hostname, char *critconfigfn);

#endif
