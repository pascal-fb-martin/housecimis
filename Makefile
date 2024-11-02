# HousecMIs - A simple home web service to calculate an index from CMIS
#
# Copyright 2023, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.

HAPP=housecimis
HROOT=/usr/local
SHARE=$(HROOT)/share/house

# Application build. --------------------------------------------

OBJS=housecimis.o
LIBOJS=

all: housecimis

main: housecimis.o

clean:
	rm -f *.o *.a housecimis

rebuild: clean all

%.o: %.c
	gcc -c -Os -o $@ $<

housecimis: $(OBJS)
	gcc -Os -o housecimis $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lm -lrt

# Application files installation --------------------------------

install-app:
	mkdir -p $(HROOT)/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f $(HROOT)/bin/housecimis
	cp housecimis $(HROOT)/bin
	chown root:root $(HROOT)/bin/housecimis
	chmod 755 $(HROOT)/bin/housecimis
	mkdir -p $(SHARE)/public/cimis
	cp public/* $(SHARE)/public/cimis
	chmod 644 $(SHARE)/public/cimis/*
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/cimis
	touch /etc/default/housecimis

uninstall-app:
	rm -rf $(SHARE)/public/cimis
	rm -f $(HROOT)/bin/housecimis

purge-app:

purge-config:
	rm -rf /etc/default/housecimis

# System installation. ------------------------------------------

include $(SHARE)/install.mak

# Docker installation -------------------------------------------

docker: all
	rm -rf build
	mkdir -p build
	cp Dockerfile build
	mkdir -p build$(HROOT)/bin
	cp housecimis build$(HROOT)/bin
	chmod 755 build$(HROOT)/bin/housecimis
	mkdir -p build$(HROOT)/share/house/public/cimis
	cp public/* build$(HROOT)/share/house/public/cimis
	chmod 644 build$(HROOT)/share/house/public/cimis/*
	cp $(SHARE)/public/house.css build$(HROOT)/share/house/public
	chmod 644 build$(HROOT)/share/house/public/house.css
	cd build ; docker build -t housecimis .
	rm -rf build
