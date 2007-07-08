#!/bin/sh
#
#    natpmp - an implementation of NAT-PMP
#    Copyright (C) 2007  Adrian Friedli
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program; if not, write to the Free Software Foundation, Inc.,
#   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#


TARGET = natpmp
OBJECTS = natpmp.o die.o leases.o linux_iptables.o
MANPAGES = natpmp.1
DEPFILE = Makefile.depend
CCFLAGS += -W -Wall

all: $(TARGET)

clean:
	rm -rf $(TARGET) $(OBJECTS) $(DEPFILE)

man: $(MANPAGES)

$(TARGET): $(OBJECTS)
	$(CC) $(CCFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CCFLAGS) -c -o $@ $<

%: %.xml
	docbook2x-man $<

$(DEPFILE): *.[ch]
	$(CC) -MM $^ > $@

-include $(DEPFILE)

.PHONY: all install clean man
