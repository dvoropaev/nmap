
/***************************************************************************
 * error.c -- a few simple routines for dealing with errors (quitting,     *
 * printing error messages, etc.                                           *
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

/* $Id: error.c,v 1.2 2003/09/11 02:12:58 fyodor Exp $ */

#include "error.h"

void fatal(char *fmt, ...) {
va_list  ap;va_start(ap, fmt);
fflush(stdout);vfprintf(stderr, fmt, ap);
fprintf(stderr, "\n");va_end(ap);
exit(1);}

void myerror(char *fmt, ...) {
va_list  ap;va_start(ap, fmt);
fflush(stdout);
vfprintf(stderr, fmt, ap);
fprintf(stderr, "\n");
va_end(ap);
return;
}

void pfatal(char *fmt, ...) {
va_list  ap;va_start(ap, fmt);
fflush(stdout);
vfprintf(stderr, fmt, ap);
fprintf(stderr, ": ");
perror("");
fprintf(stderr, "\n");
va_end(ap);
exit(1);
}

void gh_perror(char *err, ...) {
va_list  ap;va_start(ap, err);
fflush(stdout);
vfprintf(stderr, err, ap);
va_end(ap);
perror(" ");
fflush(stderr);
return;
}