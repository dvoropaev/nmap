
/***************************************************************************
 * netutils.c -- This contains some useful little network/socket related   *
 * utility functions.                                                      *
 *                                                                         *
 ***********************IMPORTANT NSOCK LICENSE TERMS***********************
 *                                                                         *
 * The nsock parallel socket event library is (C) 1999-2003 Insecure.Com   *
 * LLC This library is free software; you may redistribute and/or          *
 * modify it under the terms of the GNU General Public License as          *
 * published by the Free Software Foundation; Version 2.  This guarantees  *
 * your right to use, modify, and redistribute this software under certain *
 * conditions.  If this license is unacceptable to you, Insecure.Com LLC   *
 * may be willing to sell alternative licenses (contact                    *
 * sales@insecure.com ).                                                   *
 *                                                                         *
 * If you received these files with a written license agreement stating    *
 * terms other than the (GPL) terms above, then that alternative license   *
 * agreement takes precedence over this comment.                          *
 *                                                                         *
 * Source is provided to this software because we believe users have a     *
 * right to know exactly what a program is going to do before they run it. *
 * This also allows you to audit the software for security holes (none     *
 * have been found so far).                                                *
 *                                                                         *
 * Source code also allows you to port Nmap to new platforms, fix bugs,    *
 * and add new features.  You are highly encouraged to send your changes   *
 * to fyodor@insecure.org for possible incorporation into the main         *
 * distribution.  By sending these changes to Fyodor or one the            *
 * insecure.org development mailing lists, it is assumed that you are      *
 * offering Fyodor and Insecure.Com LLC the unlimited, non-exclusive right *
 * to reuse, modify, and relicense the code.  Nmap will always be          *
 * available Open Source, but this is important because the inability to   *
 * relicense code has caused devastating problems for other Free Software  *
 * projects (such as KDE and NASM).  We also occasionally relicense the    *
 * code to third parties as discussed above.  If you wish to specify       *
 * special license conditions of your contributions, just say so when you  *
 * send them.                                                              *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       *
 * General Public License for more details (                               *
 * http://www.gnu.org/copyleft/gpl.html ).                                 *
 *                                                                         *
 ***************************************************************************/

/* $Id: netutils.c,v 1.4 2003/09/11 02:12:58 fyodor Exp $ */

#include "netutils.h"
#include "error.h"

#if WIN32
#include "Winsock2.h"
#endif

#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#if HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif

static int netutils_debugging = 0;

/* maximize the number of file descriptors (including sockets) allowed
   for this process and return that maximum value (note -- you better
   not actually open this many -- stdin, stdout, other files opened by
   libraries you use, etc. all count toward this limit.  Leave a
   little slack */

int maximize_fdlimit() { 
#ifndef WIN32

struct rlimit r; 
static int maxfds = -1;

  if (maxfds > 0)
    return maxfds;

#if(defined(RLIMIT_NOFILE))
  if (!getrlimit(RLIMIT_NOFILE, &r)) {
    r.rlim_cur = r.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &r))
      if (netutils_debugging) perror("setrlimit RLIMIT_NOFILE failed");
    if (!getrlimit(RLIMIT_NOFILE, &r)) {
      maxfds = r.rlim_cur;
      return maxfds;
    } else return 0;
  }
#endif
#if(defined(RLIMIT_OFILE) && !defined(RLIMIT_NOFILE))
  if (!getrlimit(RLIMIT_OFILE, &r)) {
    r.rlim_cur = r.rlim_max;
    if (setrlimit(RLIMIT_OFILE, &r))
      if (netutils_debugging) perror("setrlimit RLIMIT_OFILE failed");
    if (!getrlimit(RLIMIT_OFILE, &r)) {
      maxfds = r.rlim_cur;
      return maxfds;
    } else return 0;
  }
#endif
#endif // !WIN32
  return 0;
}



int nsock_unblock_socket(int sd) {
#ifdef WIN32
unsigned long one = 1;
ioctlsocket (sd, FIONBIO, &one);
#else
int options;
/*Unblock our socket to prevent recvfrom from blocking forever
  on certain target ports. */
options = O_NONBLOCK | fcntl(sd, F_GETFL);
fcntl(sd, F_SETFL, options);
#endif
return 1;
}
