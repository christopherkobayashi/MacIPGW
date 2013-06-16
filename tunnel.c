/*
 *
 * $Id: tunnel.c,v 1.1.1.1 2001/10/28 15:01:50 stefanbethke Exp $
 *
 * (c) 1997 Stefan Bethke. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/endian.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_tun.h>
#include <net/route.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "common.h"
#include "util.h"
#include "tunnel.h"


struct tunnel {
	int		dev;
	char	name[32];
	u_long	net;
	u_long	mask;
};

struct rtmsg {
	struct rt_msghdr	h;
	char				d[256];
};

static struct tunnel gTunnel;

static outputfunc_t	gOutput;

static void set_sin (struct sockaddr *s, u_long ip) {
	struct sockaddr_in *sin = (struct sockaddr_in *)s;
	bzero (sin, sizeof (*sin));
	sin->sin_len         = sizeof (*sin);
	sin->sin_family      = AF_INET;
	sin->sin_addr.s_addr = htonl(ip);
}

static int tunnel_ifconfig (void) {
	int					sk;
	struct ifreq		ifrq;
	struct ifaliasreq	ifra;

	sk = socket (PF_INET, SOCK_DGRAM, 0);
	if (sk < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_ifconfig: socket");
		return -1;
	}

	bzero (&ifrq, sizeof (ifrq));
	strncpy (ifrq.ifr_name,  gTunnel.name, IFNAMSIZ);
	strncpy (ifra.ifra_name, gTunnel.name, IFNAMSIZ);

	if (ioctl (sk, SIOCGIFFLAGS, &ifrq) < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_ifconfig: SIOCGIFFLAGS");
		close (sk);
		return -1;
	}
	ifrq.ifr_flags |= IFF_UP;
	if (ioctl (sk, SIOCSIFFLAGS, &ifrq) < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_ifconfig: SIOCSIFFLAGS");
		close (sk);
		return -1;
	}

	set_sin (&ifrq.ifr_addr,      gTunnel.net+1);
	set_sin (&ifrq.ifr_dstaddr,   gTunnel.net);
	if (ioctl (sk, SIOCDIFADDR, &ifrq) < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_ifconfig: SIOCDIFADDR");
	}

	if (gDebug & DEBUG_TUNNEL) {
		printf ("tunnel_ifconfig: setting address %s -> ", iptoa(gTunnel.net+1));
		printf ("%s netmask ", iptoa(gTunnel.net));
		printf ("%s\n", iptoa(gTunnel.mask));
	}
	set_sin (&ifra.ifra_addr,      gTunnel.net+1);
	set_sin (&ifra.ifra_broadaddr, gTunnel.net);
	set_sin (&ifra.ifra_mask,      gTunnel.mask);
	if (ioctl (sk, SIOCAIFADDR, &ifra) < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_ifconfig: SIOCAIFADDR");
		close (sk);
		return -1;
	}

	close (sk);
	return 0;
}


int tunnel_route (int op, u_long net, u_long mask, u_long gw) {
	int					sk;
	struct rtmsg		rm;
	static int			rtmseq = 2783;
	struct sockaddr_in	sin;
	char				*p;

#define NEXTADDR(f,a) if (rm.h.rtm_addrs & (f)) {	\
			sin.sin_addr.s_addr = htonl (a);		\
			bcopy (&sin, p, sin.sin_len);			\
			p += sin.sin_len;						\
		}
	
	sk = socket (PF_ROUTE, SOCK_RAW, 0);
	if (sk < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_route: socket");
		return -1;
	}
	
	bzero (&rm, sizeof (rm));
	rm.h.rtm_version = RTM_VERSION;
	rm.h.rtm_type    = op;
	rm.h.rtm_addrs   = RTA_DST | RTA_NETMASK | (op==RTM_ADD ? RTA_GATEWAY : 0);
	rm.h.rtm_seq     = rtmseq++;
	rm.h.rtm_pid     = getpid();
	rm.h.rtm_flags   = RTF_UP | RTF_GATEWAY | RTF_STATIC;
	
	bzero (&sin, sizeof (sin));

	p = rm.d;
	sin.sin_family = AF_INET;
	sin.sin_len    = sizeof(sin);
	sin.sin_port   = 0;

	NEXTADDR(RTA_DST,     net);
	NEXTADDR(RTA_GATEWAY, gw);	
	*((u_short*)p)++ = 8;
	*((u_short*)p)++ = 0;
	*((u_long*)p)++ = htonl(mask);

	if (gDebug & DEBUG_TUNNEL) {
		printf ("tunnel_route: %s dst=%s ", op == RTM_ADD ? "adding" : "deleting", iptoa (net));
		printf ("mask=%s ", iptoa (mask));
		printf ("gw=%s\n", iptoa (gw));
	}
	rm.h.rtm_msglen = p - (char *)&rm;
	if (write (sk, &rm, rm.h.rtm_msglen) < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_route: write");
		return -1;
	}
	
	close (sk);
	return 0;
}


int 
tunnel_open (u_long net, u_long mask, outputfunc_t o) {
	int					i;
	char				s[32], *q;
	struct tuninfo		ti;
	
	gTunnel.dev = 0;
	for (i=0; i<=9; i++) {
		sprintf (s, "/dev/tun%d", i);
		gTunnel.dev = open (s, O_RDWR);
		if (gTunnel.dev)
			break;
	}
	if (gTunnel.dev < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_open: open");
		return -1;
	}

	q = rindex (s, '/');
	if (!q)
		return -1;
	q++;
	strcpy (gTunnel.name, q);
	if (gDebug & DEBUG_TUNNEL) {
		printf ("tunnel_open: %s:%s opened.\n", s, gTunnel.name);
	}
	
	ti.type = 23;
	ti.mtu  = 586;		/*	DDP_MAXSZ - 1	*/
	ti.baudrate = 38400;
	if (ioctl (gTunnel.dev, TUNSIFINFO, &ti) < 0)
	{	if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_open: TUNSIFINFO");
		return -1;
	}

#if 0
	i = gDebug & DEBUG_TUNNEL;
	if (ioctl (gTunnel.dev, TUNSDEBUG, &i) < 0)
	{	if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_open: TUNSIFINFO");
		return -1;
	}
#endif
	
	gTunnel.net   = net;
	gTunnel.mask  = mask;

	if (tunnel_ifconfig () < 0) {
		return -1;
	}

	tunnel_route (RTM_DELETE, gTunnel.net, gTunnel.mask, gTunnel.net+1);
	tunnel_route (RTM_ADD, gTunnel.net+1, (long)0xFFFFFFFF, (long)0x7F000001);
	if (tunnel_route (RTM_ADD, gTunnel.net, gTunnel.mask, gTunnel.net+1) < 0) {
		return -1;
	}

	gOutput = o;

	return gTunnel.dev;	
}


void tunnel_close (void) {
	int		i, sk;
	struct ifreq		ifrq;
	struct ifaliasreq	ifra;
	struct sockaddr_in	*sin;
	
	i = 0;
	if (ioctl (gTunnel.dev, TUNSDEBUG, &i) < 0)
	{	if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_close: TUNSIFINFO");
	}

	sk = socket (PF_INET, SOCK_DGRAM, 0);
	if (sk < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_close: socket");
		return;
	}
	bzero (&ifrq, sizeof (ifrq));
	strncpy (ifrq.ifr_name,  gTunnel.name, IFNAMSIZ);
	strncpy (ifra.ifra_name, gTunnel.name, IFNAMSIZ);

	if (ioctl (sk, SIOCGIFFLAGS, &ifrq) < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_close: SIOCGIFFLAGS");
		close (sk);
		return;
	}
	ifrq.ifr_flags &= ~IFF_UP;
	if (ioctl (sk, SIOCSIFFLAGS, &ifrq) < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_close: SIOCSIFFLAGS");
		close (sk);
		return;
	}

	bzero (&ifra.ifra_addr,      sizeof (*sin));
	bzero (&ifra.ifra_broadaddr, sizeof (*sin));
	bzero (&ifra.ifra_mask,      sizeof (*sin));
	if (ioctl (sk, SIOCDIFADDR, &ifra) < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_close: SIOCDIFADDR");
		close (sk);
		return;
	}
	tunnel_route (RTM_DELETE, gTunnel.net+1, (long)0xFFFFFFFF, (long)0x7F000001);
	tunnel_route (RTM_DELETE, gTunnel.net, gTunnel.mask, gTunnel.net+1);
}


void tunnel_input (void) {
	char	buffer[600];
	int		i;
	
	i = read (gTunnel.dev, buffer, sizeof (buffer));
	if (i < 0) {
		if (gDebug & DEBUG_TUNNEL)
			perror ("tunnel_input: read");
		return;
	}
	if (gDebug & DEBUG_TUNNEL)
		printf ("read packet from tunnel.\n");
	gOutput (buffer, i);
}


void tunnel_output (char *buffer, int len) {
	write (gTunnel.dev, buffer, len);
	if (gDebug & DEBUG_TUNNEL)
		printf ("sent packet into tunnel.\n");
}
