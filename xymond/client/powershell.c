/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Client backend module for the Windows Powershell client                    */
/*                                                                            */
/* Copyright (C) 2010-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char powershell_rcsid[] = "$Id: powershell.c 6712 2011-07-31 21:01:52Z storner $";

void handle_powershell_client(char *hostname, char *clienttype, enum ostype_t os, 
			 void *hinfo, char *sender, time_t timestamp,
			 char *clientdata)
{
	/* At the moment, the Powershell client just uses the same handling as the BBWin client */

	handle_win32_bbwin_client(hostname, clienttype, os, hinfo, sender, timestamp, clientdata);
}
