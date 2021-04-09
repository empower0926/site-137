
Debian
====================
This directory contains files used to package ozeetyd/ozeety-qt
for Debian-based Linux systems. If you compile ozeetyd/ozeety-qt yourself, there are some useful files here.

## ozeety: URI support ##


ozeety-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install ozeety-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your ozeety-qt binary to `/usr/bin`
and the `../../share/pixmaps/ozeety128.png` to `/usr/share/pixmaps`

ozeety-qt.protocol (KDE)

