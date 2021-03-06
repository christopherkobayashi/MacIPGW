ABOUT MACIPGW
=============

This is the first release of macipgw, a MacIP gateway for FreeBSD.

MacIP is a protocol that allows the encapsulation of IP packets in AppleTalk
packets, thus allowing Macs connected through an AppleTalk-only network
(such as LocalTalk or Apple Remote Access) to use TCP/IP-based services.

To use macipgw, you need FreeBSD 2.1.5 or newer with AppleTalk kernel
patches (see http://macipgw.sourceforge.net/) or FreeBSD 2.2.1 or
newer. Also, you'll need to install netatalk 1.4b2 or later.
You might want to use the netatalk port; see
http://www.freebsd.org/ports/net.html for details.

Also, you'll need one tunnel device for macipgw, see tun(4) for details.

Please direct questions, comments, diffs to <Stefan.Bethke@Hanse.DE>.

The latest version of macipgw can be found at
http://macipgw.sourceforge.net/
http://sourceforge.net/projects/macipgw/

October 28, 2001, Stefan Bethke


WHATS NEW
=========

New in 1.0:
- No bug reports for b4; so I'm releasing 1.0

New in 1.0b4:
- The routes for the tunnel are correct now.

New in 1.0b3:
- The routing/ICMP stuff has been removed. Everything should be fine.
- The example in the man page has been corrected.

New in 1.0b2:
- Fixed a compiler warning in macip.c
- -z zone now does what it is supposed to do
- now works correctly if there are no zones


INSTALLING
==========

A `make depend all install' as root should work.

If you have installed the netatalk include files (atalk) neither in
/usr/include nor in /usr/local/include, adapt the Makefile appropiatly.

Please see the man page macipgw(8) for details on how to start macipgw.



$Id: README.txt,v 1.1 2001/10/28 16:17:17 stefanbethke Exp $
