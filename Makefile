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

HAPP=housecmis
HROOT=/usr/local
SHARE=$(HROOT)/share/house

# Application build. --------------------------------------------

OBJS=housecmis.o
LIBOJS=

all: housecmis

main: housecmis.o

clean:
	rm -f *.o *.a housecmis

rebuild: clean all

%.o: %.c
	gcc -c -Os -o $@ $<

housecmis: $(OBJS)
	gcc -Os -o housecmis $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lm -lrt

# Application files installation --------------------------------

install-app:
	mkdir -p $(HROOT)/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f $(HROOT)/bin/housecmis
	cp housecmis $(HROOT)/bin
	chown root:root $(HROOT)/bin/housecmis
	chmod 755 $(HROOT)/bin/housecmis
	mkdir -p $(SHARE)/public/housecmis
	cp public/* $(SHARE)/public/housecmis
	chmod 644 $(SHARE)/public/housecmis/*
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/housecmis
	touch /etc/default/housecmis

uninstall-app:
	rm -rf $(SHARE)/public/housecmis
	rm -f $(HROOT)/bin/housecmis

purge-app:

purge-config:
	rm -rf /etc/default/housecmis

# System installation. ------------------------------------------

include $(SHARE)/install.mak

# Docker installation -------------------------------------------

docker: all
	rm -rf build
	mkdir -p build
	cp Dockerfile build
	mkdir -p build$(HROOT)/bin
	cp housecmis build$(HROOT)/bin
	chmod 755 build$(HROOT)/bin/housecmis
	mkdir -p build$(HROOT)/share/house/public/housecmis
	cp public/* build$(HROOT)/share/house/public/housecmis
	chmod 644 build$(HROOT)/share/house/public/housecmis/*
	cp $(SHARE)/public/house.css build$(SHARE)/public
	chmod 644 build$(SHARE)/public/house.css
	cd build ; docker build -t housecmis .
	rm -rf build
