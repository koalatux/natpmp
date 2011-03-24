/*
 *    natpmp - an implementation of NAT-PMP
 *    Copyright (C) 2007  Adrian Friedli
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <syslog.h>

extern int debuglevel;
extern int do_fork;
#define debug_printf(...) { if (debuglevel >= 2) { if (do_fork) syslog(LOG_DEBUG, __VA_ARGS__); else fprintf(stderr, __VA_ARGS__); } }

void die(const char * e) __attribute__ ((noreturn)) ;
void p_die(const char * p) __attribute__ ((noreturn)) ;
