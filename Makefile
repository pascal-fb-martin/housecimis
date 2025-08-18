# HousecMIs - A simple home web service to calculate an index from CMIS
#
# Copyright 2025, Pascal Martin
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
#
# WARNING
#
# This Makefile depends on echttp and houseportal (dev) being installed.

prefix=/usr/local
SHARE=$(prefix)/share/house

INSTALL=/usr/bin/install

HAPP=housecimis
HCAT=providers

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
	gcc -Os -o housecimis $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lmagic -lm -lrt

# Application files installation --------------------------------

install-ui: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public/cimis
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public/cimis

install-runtime: install-preamble
	$(INSTALL) -m 0755 -s housecimis $(DESTDIR)$(prefix)/bin
	touch $(DESTDIR)/etc/default/housecimis

install-app: install-ui install-runtime

uninstall-app:
	rm -rf $(DESTDIR)$(SHARE)/public/cimis
	rm -f $(DESTDIR)$(prefix)/bin/housecimis

purge-app:

purge-config:
	rm -rf $(DESTDIR)/etc/default/housecimis

# Build a private Debian package. -------------------------------

install-package: install-ui install-runtime install-systemd

debian-package:
	rm -rf build
	install -m 0755 -d build/$(HAPP)/DEBIAN
	cat debian/control | sed "s/{{arch}}/`dpkg --print-architecture`/" > build/$(HAPP)/DEBIAN/control
	install -m 0644 debian/copyright build/$(HAPP)/DEBIAN
	install -m 0644 debian/changelog build/$(HAPP)/DEBIAN
	install -m 0755 debian/postinst build/$(HAPP)/DEBIAN
	install -m 0755 debian/prerm build/$(HAPP)/DEBIAN
	install -m 0755 debian/postrm build/$(HAPP)/DEBIAN
	make DESTDIR=build/$(HAPP) install-package
	cd build/$(HAPP) ; find etc -type f | sed 's/etc/\/etc/' > DEBIAN/conffiles
	cd build ; fakeroot dpkg-deb -b $(HAPP) .

# System installation. ------------------------------------------

include $(SHARE)/install.mak

# Docker installation -------------------------------------------

docker: all
	rm -rf build
	mkdir -p build
	cp Dockerfile build
	mkdir -p build$(prefix)/bin
	cp housecimis build$(prefix)/bin
	chmod 755 build$(prefix)/bin/housecimis
	mkdir -p build$(prefix)/share/house/public/cimis
	cp public/* build$(prefix)/share/house/public/cimis
	chmod 644 build$(prefix)/share/house/public/cimis/*
	cp $(SHARE)/public/house.css build$(prefix)/share/house/public
	chmod 644 build$(prefix)/share/house/public/house.css
	cd build ; docker build -t housecimis .
	rm -rf build
