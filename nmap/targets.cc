
/***************************************************************************
 * targets.cc -- Functions relating to "ping scanning" as well as          *
 * determining the exact IPs to hit based on CIDR and other input          *
 * formats.                                                                *
 *                                                                         *
 ***********************IMPORTANT NMAP LICENSE TERMS************************
 *                                                                         *
 * The Nmap Security Scanner is (C) 1995-2003 Insecure.Com LLC. This       *
 * program is free software; you may redistribute and/or modify it under   *
 * the terms of the GNU General Public License as published by the Free    *
 * Software Foundation; Version 2.  This guarantees your right to use,     *
 * modify, and redistribute this software under certain conditions.  If    *
 * you wish to embed Nmap technology into proprietary software, we may be  *
 * willing to sell alternative licenses (contact sales@insecure.com).      *
 * Many security scanner vendors already license Nmap technology such as   *
 * our remote OS fingerprinting database and code.                         *
 *                                                                         *
 * Note that the GPL places important restrictions on "derived works", yet *
 * it does not provide a detailed definition of that term.  To avoid       *
 * misunderstandings, we consider an application to constitute a           *
 * "derivative work" for the purpose of this license if it does any of the *
 * following:                                                              *
 * o Integrates source code from Nmap                                      *
 * o Reads or includes Nmap copyrighted data files, such as                *
 *   nmap-os-fingerprints or nmap-service-probes.                          *
 * o Executes Nmap                                                         *
 * o Integrates/includes/aggregates Nmap into an executable installer      *
 * o Links to a library or executes a program that does any of the above   *
 *                                                                         *
 * The term "Nmap" should be taken to also include any portions or derived *
 * works of Nmap.  This list is not exclusive, but is just meant to        *
 * clarify our interpretation of derived works with some common examples.  *
 * These restrictions only apply when you actually redistribute Nmap.  For *
 * example, nothing stops you from writing and selling a proprietary       *
 * front-end to Nmap.  Just distribute it by itself, and point people to   *
 * http://www.insecure.org/nmap/ to download Nmap.                         *
 *                                                                         *
 * We don't consider these to be added restrictions on top of the GPL, but *
 * just a clarification of how we interpret "derived works" as it applies  *
 * to our GPL-licensed Nmap product.  This is similar to the way Linus     *
 * Torvalds has announced his interpretation of how "derived works"        *
 * applies to Linux kernel modules.  Our interpretation refers only to     *
 * Nmap - we don't speak for any other GPL products.                       *
 *                                                                         *
 * If you have any questions about the GPL licensing restrictions on using *
 * Nmap in non-GPL works, we would be happy to help.  As mentioned above,  *
 * we also offer alternative license to integrate Nmap into proprietary    *
 * applications and appliances.  These contracts have been sold to many    *
 * security vendors, and generally include a perpetual license as well as  *
 * providing for priority support and updates as well as helping to fund   *
 * the continued development of Nmap technology.  Please email             *
 * sales@insecure.com for further information.                             *
 *                                                                         *
 * If you received these files with a written license agreement or         *
 * contract stating terms other than the (GPL) terms above, then that      *
 * alternative license agreement takes precedence over these comments.     *
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
 * Insecure.Org development mailing lists, it is assumed that you are      *
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
 * General Public License for more details at                              *
 * http://www.gnu.org/copyleft/gpl.html .                                  *
 *                                                                         *
 ***************************************************************************/

/* $Id: targets.cc,v 1.29 2003/09/16 06:04:55 fyodor Exp $ */


#include "targets.h"
#include "timing.h"
#include "osscan.h"
#include "NmapOps.h"
#include "TargetGroup.h"
#include "Target.h"

extern NmapOps o;
enum pingstyle { pingstyle_unknown, pingstyle_rawtcp, pingstyle_rawudp, pingstyle_connecttcp, 
		 pingstyle_icmp };

/*  predefined filters -- I need to kill these globals at some pont. */
extern unsigned long flt_dsthost, flt_srchost;
extern unsigned short flt_baseport;

/* Gets the host number (index) of target in the hostbatch array of
 pointers.  Note that the target MUST EXIST in the array or all
 heck will break loose. */
static inline int gethostnum(Target *hostbatch[], Target *target) {
  int i = 0;
  do {
    if (hostbatch[i] == target)
      return i;
  } while(++i);

  fatal("fluxx0red");
  return 0; // Unreached
}

/* Internal function to update the state of machine (up/down/etc) based on
   ping results */
static int hostupdate(Target *hostbatch[], Target *target, 
		      int newstate, int dotimeout, int trynum, 
		      struct timeout_info *to, struct timeval *sent, 
		      struct timeval *rcvd,
		      struct pingtune *pt, struct tcpqueryinfo *tqi, 
		      enum pingstyle style)
{

  int hostnum = gethostnum(hostbatch, target);
  int i;
  int p;
  int seq;
  int tmpsd;
  struct timeval tv;

  if (o.debugging)  {
    gettimeofday(&tv, NULL);
    log_write(LOG_STDOUT, "Hostupdate called for machine %s state %s -> %s (trynum %d, dotimeadj: %s time: %ld)\n", target->targetipstr(), readhoststate(target->flags), readhoststate(newstate), trynum, (dotimeout)? "yes" : "no", (long) TIMEVAL_SUBTRACT(tv, *sent));
  }

  assert(hostnum <= pt->group_end);
  
  if (dotimeout) {
    if (!rcvd) {
      rcvd = &tv;
      gettimeofday(&tv, NULL);
    }
    adjust_timeouts2(sent, rcvd, to);
  }
  
  /* If this is a tcp connect() pingscan, close all sockets */
  
  if (style == pingstyle_connecttcp) {
    seq = hostnum * pt->max_tries + trynum;
    for(p=0; p < o.num_ping_synprobes; p++) {
      for(i=0; i <= pt->block_tries; i++) {  
        seq = hostnum * pt->max_tries + i;
        tmpsd = tqi->sockets[p][seq];
        if (tmpsd >= 0) {
	  assert(tqi->sockets_out > 0);
	  tqi->sockets_out--;
	  close(tmpsd);
	  if (tmpsd == tqi->maxsd) tqi->maxsd--;
	  FD_CLR(tmpsd, &(tqi->fds_r));
	  FD_CLR(tmpsd, &(tqi->fds_w));
	  FD_CLR(tmpsd, &(tqi->fds_x));
	  tqi->sockets[p][seq] = -1;
        }
      }
    }
  }
  
  target->to = *to;
  
  if (target->flags & HOST_UP) {
    /* The target is already up and that takes precedence over HOST_DOWN
       or HOST_FIREWALLED, so we just return. */
    return 0;
  }
  
  if (trynum > 0 && !(pt->dropthistry)) {
    pt->dropthistry = 1;
    if (o.debugging) 
      log_write(LOG_STDOUT, "Decreasing massping group size from %f to ", pt->group_size);
    pt->group_size = MAX(pt->group_size * 0.75, pt->min_group_size);
    if (o.debugging) 
      log_write(LOG_STDOUT, "%f\n", pt->group_size);
  }
  
  if (newstate == HOST_DOWN && (target->flags & HOST_DOWN)) {
    /* I see nothing to do here */
  } else if (newstate == HOST_UP && (target->flags & HOST_DOWN)) {
  /* We give host_up precedence */
    target->flags &= ~HOST_DOWN; /* Kill the host_down flag */
    target->flags |= HOST_UP;
    if (hostnum >= pt->group_start) {  
      assert(pt->down_this_block > 0);
      pt->down_this_block--;
      pt->up_this_block++;
    }
  } else if (newstate == HOST_DOWN) {
    target->flags |= HOST_DOWN;
    assert(pt->block_unaccounted > 0);
    if (hostnum >= pt->group_start) { 
      pt->down_this_block++;
      pt->block_unaccounted--;
      pt->num_responses++;
    }
  } else {
    assert(newstate == HOST_UP);
    target->flags |= HOST_UP;
    assert(pt->block_unaccounted > 0);
    if (hostnum >= pt->group_start) { 
      pt->up_this_block++;
      pt->block_unaccounted--;
      pt->num_responses++;
    }
  }
  return 0;
}

void hoststructfry(Target *hostbatch[], int nelem) {
  genfry((unsigned char *)hostbatch, sizeof(Target *), nelem);
  return;
}

Target *nexthost(HostGroupState *hs, 
			    struct scan_lists *ports, int *pingtype) {
int hidx;
char *device;
int i;
struct sockaddr_storage ss;
size_t sslen;

if (hs->next_batch_no < hs->current_batch_sz) {
  /* Woop!  This is easy -- we just pass back the next host struct */
  return hs->hostbatch[hs->next_batch_no++];
}
/* Doh, we need to refresh our array */
/* for(i=0; i < hs->max_batch_sz; i++) hs->hostbatch[i] = new Target(); */

hs->current_batch_sz = hs->next_batch_no = 0;
do {
  /* Grab anything we have in our current_expression */
  while (hs->current_batch_sz < hs->max_batch_sz && 
	 hs->current_expression.get_next_host(&ss, &sslen) == 0)
    {
      hidx = hs->current_batch_sz;
      hs->hostbatch[hidx] = new Target();
      hs->hostbatch[hidx]->setTargetSockAddr(&ss, sslen);

      /* Lets figure out what device this IP uses ... */
      if (o.spoofsource) {
	o.SourceSockAddr(&ss, &sslen);
	hs->hostbatch[hidx]->setSourceSockAddr(&ss, sslen);
	Strncpy(hs->hostbatch[hidx]->device, o.device, 64);
      } else {
	/* We figure out the source IP/device IFF
	   1) We are r00t AND
	   2) We are doing tcp or udp pingscan OR
	   3) We are doing a raw-mode portscan or osscan OR 
	   4) We are on windows and doing ICMP ping */
	if (o.isr00t && o.af() == AF_INET && 
	    ((*pingtype & (PINGTYPE_TCP|PINGTYPE_UDP)) || 
	     o.synscan || o.finscan || o.xmasscan || o.nullscan || 
	     o.ipprotscan || o.maimonscan || o.idlescan || o.ackscan || 
	     o.udpscan || o.osscan || o.windowscan
#ifdef WIN32
         || (*pingtype & (PINGTYPE_ICMP_PING|PINGTYPE_ICMP_MASK|PINGTYPE_ICMP_TS))
#endif // WIN32
		 )) {
	  struct sockaddr_in *sin = (struct sockaddr_in *) &ss;
	  sslen = sizeof(*sin);
	  sin->sin_family = AF_INET;
#if HAVE_SOCKADDR_SA_LEN
	  sin->sin_len = sslen;
#endif
	  device = routethrough(hs->hostbatch[hidx]->v4hostip(), 
				&(sin->sin_addr));
	  hs->hostbatch[hidx]->setSourceSockAddr(&ss, sslen);
      o.decoys[o.decoyturn] = hs->hostbatch[hidx]->v4source();
	  if (!device) {
	    if (*pingtype == PINGTYPE_NONE) {
	      fatal("Could not determine what interface to route packets through, run again with -e <device>");
	    } else {
#if WIN32
          fatal("Unable to determine what interface to route packets through to %s", hs->hostbatch[hidx]->targetipstr());
#endif
	      error("WARNING:  Could not determine what interface to route packets through to %s, changing ping scantype to ICMP ping only", hs->hostbatch[hidx]->targetipstr());
	      *pingtype = PINGTYPE_ICMP_PING;
	    }
	  } else {
	    Strncpy(hs->hostbatch[hidx]->device, device, 64);
	  }
	}
      }

      /* In some cases, we can only allow hosts that use the same device
	 in a group. */
      if (o.af() == AF_INET && o.isr00t && hidx > 0 && *hs->hostbatch[hidx]->device && hs->hostbatch[hidx]->v4source().s_addr != hs->hostbatch[0]->v4source().s_addr) {
	/* Cancel everything!  This guy must go in the next group and we are
	   outtof here */
	hs->current_expression.return_last_host();
	delete hs->hostbatch[hidx];
	goto batchfull;
      }
      hs->current_batch_sz++;
    }

  if (hs->current_batch_sz < hs->max_batch_sz &&
      hs->next_expression < hs->num_expressions) {
    /* We are going to have to plop in another expression. */
    while(hs->current_expression.parse_expr(hs->target_expressions[hs->next_expression++], o.af()) != 0) 
      if (hs->next_expression >= hs->num_expressions)
	break;
  } else break;
} while(1);

 batchfull:
 
if (hs->current_batch_sz == 0)
  return NULL;

/* OK, now we have our complete batch of entries.  The next step is to
   randomize them (if requested) */
if (hs->randomize) {
  hoststructfry(hs->hostbatch, hs->current_batch_sz);
}

/* Finally we do the mass ping (if required) */
 if ((*pingtype & 
      (PINGTYPE_ICMP_PING|PINGTYPE_ICMP_MASK|PINGTYPE_ICMP_TS) ) || 
     ((!o.isr00t || o.af() == AF_INET6 || hs->hostbatch[0]->v4host().s_addr) && 
      (*pingtype != PINGTYPE_NONE))) 
   massping(hs->hostbatch, hs->current_batch_sz, ports, *pingtype);
 else for(i=0; i < hs->current_batch_sz; i++)  {
   hs->hostbatch[i]->to.srtt = -1;
   hs->hostbatch[i]->to.rttvar = -1;
   hs->hostbatch[i]->to.timeout = o.initialRttTimeout() * 1000;
   hs->hostbatch[i]->flags |= HOST_UP; /*hostbatch[i].up = 1;*/
 }
 return hs->hostbatch[hs->next_batch_no++];
}


void massping(Target *hostbatch[], int num_hosts, 
              struct scan_lists *ports, int pingtype) {
static struct timeout_info to = {0,0,0};
static double gsize = (double) LOOKAHEAD;
int hostnum;
struct pingtune pt;
struct scanstats ss;
struct timeval begin_select;
struct pingtech ptech;
struct tcpqueryinfo tqi;
int max_block_size = 70;
struct ppkt {
  unsigned char type;
  unsigned char code;
  unsigned short checksum;
  unsigned short id;
  unsigned short seq;
};
int elapsed_time;
int blockinc;
int probes_per_host = 0; /* Number of probes done for each host (eg
                            ping packet plus two TCP ports would be 3) */
int sd_blocking = 1;
struct sockaddr_in sock;
u16 seq = 0;
int sd = -1, rawsd = -1, rawpingsd = -1;
struct timeval *time;
struct timeval start, end;
unsigned short id;
pcap_t *pd = NULL;
char filter[512];
unsigned short sportbase;
int max_sockets = 0;
int p;
int group_start, group_end, direction; /* For going forward or backward through
					       grouplen */
memset((char *)&ptech, 0, sizeof(ptech));

memset((char *) &pt, 0, sizeof(pt)); 

pt.up_this_block = 0;
pt.block_unaccounted = LOOKAHEAD;
pt.down_this_block = 0;
pt.num_responses = 0;
pt.max_tries = 5; /* Maximum number of tries for a block */
pt.group_start = 0;
pt.block_tries = 0; /* How many tries this block has gone through */
pt.seq_offset = get_random_u16(); // For distinguishing between received packets from this execution vs. concurrent nmaps

/* What port should we send from? */
if (o.magic_port_set) sportbase = o.magic_port;
else sportbase = o.magic_port + 20;

/* What kind of scans are we doing? */
 if ((pingtype & (PINGTYPE_ICMP_PING|PINGTYPE_ICMP_MASK|PINGTYPE_ICMP_TS)) && 
     hostbatch[0]->v4source().s_addr) 
  ptech.rawicmpscan = 1;
else if (pingtype & (PINGTYPE_ICMP_PING|PINGTYPE_ICMP_MASK|PINGTYPE_ICMP_TS)) 
  ptech.icmpscan = 1;
if (pingtype & PINGTYPE_UDP) 
  ptech.rawudpscan = 1;
if (pingtype & PINGTYPE_TCP) {
  if (o.isr00t && o.af() == AF_INET)
    ptech.rawtcpscan = 1;
  else ptech.connecttcpscan = 1;
}

 probes_per_host = 0;
 if (pingtype & PINGTYPE_ICMP_PING) probes_per_host++;
 if (pingtype & PINGTYPE_ICMP_MASK) probes_per_host++;
 if (pingtype & PINGTYPE_ICMP_TS) probes_per_host++;
 probes_per_host += o.num_ping_synprobes + o.num_ping_ackprobes + o.num_ping_udpprobes;

pt.min_group_size = MAX(3, MAX(o.min_parallelism, 16) / probes_per_host);

 /* I think max_parallelism mostly relates to # of probes in parallel against
    a single machine.  So I only use it to go down to 15 here */
pt.group_size = (o.max_parallelism)? MIN(MAX(o.max_parallelism,15), gsize) : gsize;
/* Make sure we have at least the minimum parallelism level */
pt.group_size = MAX(pt.group_size, o.min_parallelism);
/* Reduce the group size proportionally to number of probes sent per host */
pt.group_size = MAX(pt.min_group_size, 0.9999 + pt.group_size / probes_per_host);
max_block_size /= probes_per_host;

time = (struct timeval *) safe_zalloc(sizeof(struct timeval) * ((pt.max_tries) * num_hosts));
id = (unsigned short) get_random_uint();

if (ptech.connecttcpscan)  {
  /* I think max_parallelism mostly relates to # of probes in parallel against
      a given machine.  So I won't go below 15 here because of it */
  max_sockets = MAX(1, max_sd() - 4);
  if (o.max_parallelism) {
    max_block_size = MIN(50, MAX(o.max_parallelism, 15) / probes_per_host);
  } else {
    max_block_size = MIN(50, max_sockets / probes_per_host);
  }
  max_block_size = MAX(1, max_block_size);
  pt.group_size = MIN(pt.group_size, max_block_size);
  memset((char *)&tqi, 0, sizeof(tqi));

  for(p=0; p < o.num_ping_synprobes; p++) {
    tqi.sockets[p] = (int *) safe_malloc(sizeof(int) * (pt.max_tries) * num_hosts);
    memset(tqi.sockets[p], 255, sizeof(int) * (pt.max_tries) * num_hosts);
  }
  FD_ZERO(&(tqi.fds_r));
  FD_ZERO(&(tqi.fds_w));
  FD_ZERO(&(tqi.fds_x));
  tqi.sockets_out = 0;
  tqi.maxsd = 0;
}

if (ptech.icmpscan) {
  sd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sd < 0) pfatal("Socket trouble in massping"); 
  unblock_socket(sd);
  sd_blocking = 0;
  if (num_hosts > 10)
    max_rcvbuf(sd);
  broadcast_socket(sd);
} else sd = -1;


/* if to timeout structure hasn't been initialized yet */
if (!to.srtt && !to.rttvar && !to.timeout) {
  /*  to.srtt = 800000;
      to.rttvar = 500000; */ /* we will init these when we get real data */
  to.timeout = o.initialRttTimeout() * 1000;
  to.srtt = -1;
  to.rttvar = -1;
} 

/* Init our raw socket */
if (o.numdecoys > 1 || ptech.rawtcpscan || ptech.rawicmpscan || ptech.rawudpscan) {
  if ((rawsd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0 )
    pfatal("socket trobles in massping");
  broadcast_socket(rawsd);
  
  if ((rawpingsd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0 )
    pfatal("socket trobles in massping");
  broadcast_socket(rawpingsd);
}
 else { rawsd = -1; rawpingsd = -1; }

if (ptech.rawicmpscan || ptech.rawtcpscan || ptech.rawudpscan) {
  /* we need a pcap descript0r! */
  /* MAX snaplen needed = 
     24 bytes max link_layer header
     64 bytes max IPhdr
     16 bytes of the TCP header
     ---
   = 104 byte snaplen */
  pd = my_pcap_open_live(hostbatch[0]->device, 104, o.spoofsource, 20);

  flt_dsthost = hostbatch[0]->v4source().s_addr;
  flt_baseport = sportbase;

  snprintf(filter, sizeof(filter), "(icmp and dst host %s) or ((tcp or udp) and dst host %s and ( dst port %d or dst port %d or dst port %d or dst port %d or dst port %d))", 
	   inet_ntoa(hostbatch[0]->v4source()),
	   inet_ntoa(hostbatch[0]->v4source()),
	   sportbase , sportbase + 1, sportbase + 2, sportbase + 3, 
	   sportbase + 4);

  set_pcap_filter(hostbatch[0], pd, flt_icmptcp_5port, filter); 
}

 blockinc = (int) (0.9999 + 8.0 / probes_per_host);

memset((char *)&sock,0,sizeof(sock));
gettimeofday(&start, NULL);

 pt.group_end = (int) MIN(pt.group_start + pt.group_size -1, num_hosts -1);
 
 while(pt.group_start < num_hosts) { /* while we have hosts left to scan */
   do { /* one block */
     pt.up_this_block = 0;
     pt.down_this_block = 0;
     pt.block_unaccounted = 0;

     /* Sometimes a group gets too big for the network path and a
        router (especially things like cable/DSL modems) will drop our
        packets after a certain number have been sent.  If we just
        retry the exact same group, we are likely to get exactly the
        same behavior (missing later hosts).  So we reverse the order
        for each try */
     direction = (pt.block_tries % 2 == 0)? 1 : -1;
     if (direction == 1) { group_start = pt.group_start; group_end = pt.group_end; }
     else { group_start = pt.group_end; group_end = pt.group_start; }
     for(hostnum = group_start; hostnum != group_end + direction; hostnum += direction) {      
       /* If (we don't know whether the host is up yet) ... */
       if (!(hostbatch[hostnum]->flags & HOST_UP) && !hostbatch[hostnum]->wierd_responses && !(hostbatch[hostnum]->flags & HOST_DOWN)) {  
	 /* Send a ping queries to it */
	 seq = hostnum * pt.max_tries + pt.block_tries + pt.seq_offset;
	 if (ptech.icmpscan && !sd_blocking) { 
	   block_socket(sd); sd_blocking = 1; 
	 }
	 pt.block_unaccounted++;
	 gettimeofday(&time[(seq - pt.seq_offset) & 0xFFFF], NULL);

	 if (ptech.icmpscan || ptech.rawicmpscan)
	   sendpingqueries(sd, rawpingsd, hostbatch[hostnum],  
			   seq, id, &ss, time, pingtype, ptech);
       
	 if (ptech.rawtcpscan || ptech.rawudpscan) 
	   sendrawtcpudppingqueries(rawsd, hostbatch[hostnum], pingtype, seq, time, &pt);

	 else if (ptech.connecttcpscan) {
	   sendconnecttcpqueries(hostbatch, &tqi, hostbatch[hostnum], seq, time, &pt, &to, max_sockets);
	 }
       }
     } /* for() loop */
     /* OK, we have sent our ping packets ... now we wait for responses */
     gettimeofday(&begin_select, NULL);
     do {
       if (ptech.icmpscan && sd_blocking ) { 
	 unblock_socket(sd); sd_blocking = 0; 
       }
       if(ptech.icmpscan || ptech.rawicmpscan || ptech.rawtcpscan || ptech.rawudpscan) {       
	 get_ping_results(sd, pd, hostbatch, pingtype, time, &pt, &to, id, 
			  &ptech, ports);
       }
       if (ptech.connecttcpscan) {
	 get_connecttcpscan_results(&tqi, hostbatch, time, &pt, &to);
       }
       gettimeofday(&end, NULL);
       elapsed_time = TIMEVAL_SUBTRACT(end, begin_select);
     } while( elapsed_time < to.timeout);
     /* try again if a new box was found but some are still unaccounted for and
	we haven't run out of retries.  Always retry at least once.
     */
     pt.dropthistry = 0;
     pt.block_tries++;
   } while ((pt.up_this_block > 0 || pt.block_tries == 1) && pt.block_unaccounted > 0 && pt.block_tries < pt.max_tries);

   if (o.debugging)
     log_write(LOG_STDOUT, "Finished block: srtt: %d rttvar: %d timeout: %d block_tries: %d up_this_block: %d down_this_block: %d group_sz: %d\n", to.srtt, to.rttvar, to.timeout, pt.block_tries, pt.up_this_block, pt.down_this_block, pt.group_end - pt.group_start + 1);

   if (pt.block_tries == 2 && pt.up_this_block == 0 && pt.down_this_block == 0)
     /* Then it did not miss any hosts (that we know of)*/
       pt.group_size = MIN(pt.group_size + blockinc, max_block_size);
   
   /* Move to next block */
   pt.block_tries = 0;
   pt.group_start = pt.group_end +1;
   pt.group_end = (int) MIN(pt.group_start + pt.group_size -1, num_hosts -1);
   /*   pt.block_unaccounted = pt.group_end - pt.group_start + 1;   */
 }

 close(sd);
 if (ptech.connecttcpscan)
   for(p=0; p < o.num_ping_synprobes; p++)
     free(tqi.sockets[p]);
 if (sd >= 0) close(sd);
 if (rawsd >= 0) close(rawsd);
 if (rawpingsd >= 0) close(rawpingsd);
 free(time);
 if (pd) pcap_close(pd);
 if (o.debugging) 
   log_write(LOG_STDOUT, "massping done:  num_hosts: %d  num_responses: %d\n", num_hosts, pt.num_responses);
 gsize = pt.group_size;
 return;
}

int sendconnecttcpqueries(Target *hostbatch[], struct tcpqueryinfo *tqi,
			Target *target, u16 seq, 
			struct timeval *time, struct pingtune *pt, 
			struct timeout_info *to, int max_sockets) {
  int i;

  for( i=0; i<o.num_ping_synprobes; i++ ) {
    if (i > 0 && o.scan_delay) enforce_scan_delay(NULL);
    sendconnecttcpquery(hostbatch, tqi, target, i, seq, time, pt, to, max_sockets);
  }

  return 0;
}

int sendconnecttcpquery(Target *hostbatch[], struct tcpqueryinfo *tqi,
			Target *target, int probe_port_num, u16 seq, 
			struct timeval *time, struct pingtune *pt, 
			struct timeout_info *to, int max_sockets) {

  int res,i;
  int tmpsd;
  int hostnum, trynum;
  struct sockaddr_storage sock;
  struct sockaddr_in *sin = (struct sockaddr_in *) &sock;
  struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) &sock;
  size_t socklen;
  
  seq -= pt->seq_offset; // Because connect() pingscan doesn't send it over the wire
  trynum = seq % pt->max_tries;
  hostnum = seq / pt->max_tries;

  assert(tqi->sockets_out <= max_sockets);
  if (tqi->sockets_out == max_sockets) {
    /* We've got to free one! */
    for(i=0; i < trynum; i++) {
      tmpsd = hostnum * pt->max_tries + i;
      if (tqi->sockets[probe_port_num][tmpsd] >= 0) {
	if (o.debugging) 
	  log_write(LOG_STDOUT, "sendconnecttcpquery: Scavenging a free socket due to serious shortage\n");
	close(tqi->sockets[probe_port_num][tmpsd]);
	tqi->sockets[probe_port_num][tmpsd] = -1;
	tqi->sockets_out--;
	break;
      }
    }
    if (i == trynum)
      fatal("sendconnecttcpquery: Could not scavenge a free socket!");
  }
    
  /* Since we know we now have a free s0cket, lets take it */
  assert(tqi->sockets[probe_port_num][seq] == -1);
  tqi->sockets[probe_port_num][seq] =  socket(o.af(), SOCK_STREAM, IPPROTO_TCP);
  if (tqi->sockets[probe_port_num][seq] == -1) 
    fatal("Socket creation in sendconnecttcpquery");
  tqi->maxsd = MAX(tqi->maxsd, tqi->sockets[probe_port_num][seq]);
  tqi->sockets_out++;
  unblock_socket(tqi->sockets[probe_port_num][seq]);
  init_socket(tqi->sockets[probe_port_num][seq]);

  if (target->TargetSockAddr(&sock, &socklen) != 0)
    fatal("Unable to get target sock in sendconnecttcpquery");

  if (sin->sin_family == AF_INET)
    sin->sin_port = htons(o.ping_synprobes[probe_port_num]);
#if HAVE_IPV6
  else sin6->sin6_port = htons(o.ping_synprobes[probe_port_num]);
#endif //HAVE_IPV6

  res = connect(tqi->sockets[probe_port_num][seq],(struct sockaddr *)&sock, socklen);

  if ((res != -1 || socket_errno() == ECONNREFUSED)) {
    /* This can happen on localhost, successful/failing connection immediately
       in non-blocking mode */
      hostupdate(hostbatch, target, HOST_UP, 1, trynum, to, 
		 &time[seq], NULL, pt, tqi, pingstyle_connecttcp);
    if (tqi->maxsd == tqi->sockets[probe_port_num][seq]) tqi->maxsd--;
  }
  else if (socket_errno() == ENETUNREACH) {
    if (o.debugging) 
      error("Got ENETUNREACH from sendconnecttcpquery connect()");
    hostupdate(hostbatch, target, HOST_DOWN, 1, trynum, to, 
	       &time[seq], NULL, pt, tqi, pingstyle_connecttcp);
  }
  else {
    /* We'll need to select() and wait it out */
    FD_SET(tqi->sockets[probe_port_num][seq], &(tqi->fds_r));
    FD_SET(tqi->sockets[probe_port_num][seq], &(tqi->fds_w));
    FD_SET(tqi->sockets[probe_port_num][seq], &(tqi->fds_x));
  }
return 0;
}

int sendrawtcpudppingqueries(int rawsd, Target *target, int pingtype, u16 seq, 
			  struct timeval *time, struct pingtune *pt) {
  int i;

  if (pingtype & PINGTYPE_UDP) {
    for( i=0; i<o.num_ping_udpprobes; i++ ) {
      if (i > 0 && o.scan_delay) enforce_scan_delay(NULL);
      sendrawudppingquery(rawsd, target, o.ping_udpprobes[i], seq, time, pt);
    }
  }

  if (pingtype & PINGTYPE_TCP_USE_ACK) {
    for( i=0; i<o.num_ping_ackprobes; i++ ) {
      if (i > 0 && o.scan_delay) enforce_scan_delay(NULL);
      sendrawtcppingquery(rawsd, target, PINGTYPE_TCP_USE_ACK, o.ping_ackprobes[i], seq, time, pt);
    }
  }

  if (pingtype & PINGTYPE_TCP_USE_SYN) {
    for( i=0; i<o.num_ping_synprobes; i++ ) {
      if (i > 0 && o.scan_delay) enforce_scan_delay(NULL);
      sendrawtcppingquery(rawsd, target, PINGTYPE_TCP_USE_SYN, o.ping_synprobes[i], seq, time, pt);
    }
  }

  return 0;
}

int sendrawudppingquery(int rawsd, Target *target, u16 probe_port,
			u16 seq, struct timeval *time, struct pingtune *pt) {
int trynum = 0;
unsigned short sportbase;

if (o.magic_port_set) sportbase = o.magic_port;
else { 
  sportbase = o.magic_port + 20;
  trynum = seq % pt->max_tries;
}

 o.decoys[o.decoyturn].s_addr = target->v4source().s_addr;
 
 send_udp_raw_decoys( rawsd, target->v4hostip(), o.ttl, sportbase + trynum, probe_port, seq, o.extra_payload, o.extra_payload_length);


 return 0;
}

int sendrawtcppingquery(int rawsd, Target *target, int pingtype, u16 probe_port,
			u16 seq, struct timeval *time, struct pingtune *pt) {
int trynum = 0;
int myseq;
unsigned short sportbase;
unsigned long myack;

if (o.magic_port_set) sportbase = o.magic_port;
else { 
  sportbase = o.magic_port + 20;
  trynum = seq % pt->max_tries;
}

 myseq = (get_random_uint() << 22) + (seq << 6) + 0x1E; /* (response & 0x3F) better be 0x1E or 0x1F */
 myack = (get_random_uint() << 22) + (seq << 6) + 0x1E; /* (response & 0x3F) better be 0x1E or 0x1F */
 o.decoys[o.decoyturn].s_addr = target->v4source().s_addr;

 if (pingtype & PINGTYPE_TCP_USE_SYN) {   
   send_tcp_raw_decoys( rawsd, target->v4hostip(), o.ttl, sportbase + trynum, probe_port, myseq, myack, TH_SYN, 0, NULL, 0, o.extra_payload, 
			o.extra_payload_length);
 } else {
   send_tcp_raw_decoys( rawsd, target->v4hostip(), o.ttl, sportbase + trynum, probe_port, myseq, myack, TH_ACK, 0, NULL, 0, o.extra_payload, 
			o.extra_payload_length);
 }

 return 0;
}

int sendpingqueries(int sd, int rawsd, Target *target,  
		  u16 seq, unsigned short id, struct scanstats *ss, 
		    struct timeval *time, int pingtype, struct pingtech ptech) {
  if (pingtype & PINGTYPE_ICMP_PING) {
    if (o.scan_delay) enforce_scan_delay(NULL);
    sendpingquery(sd, rawsd, target, seq, id, ss, time, PINGTYPE_ICMP_PING, ptech);
  }
  if (pingtype & PINGTYPE_ICMP_MASK) {
    if (o.scan_delay) enforce_scan_delay(NULL);
    sendpingquery(sd, rawsd, target, seq, id, ss, time, PINGTYPE_ICMP_MASK, ptech);

  }
  if (pingtype & PINGTYPE_ICMP_TS) {
    if (o.scan_delay) enforce_scan_delay(NULL);
    sendpingquery(sd, rawsd, target, seq, id, ss, time, PINGTYPE_ICMP_TS, ptech);
  }

  return 0;
}

int sendpingquery(int sd, int rawsd, Target *target,  
		  u16 seq, unsigned short id, struct scanstats *ss, 
		  struct timeval *time, int pingtype, struct pingtech ptech) {
struct ppkt {
  u8 type;
  u8 code;
  u16 checksum;
  u16 id;
  u16 seq;
  u8 data[1500]; /* Note -- first 4-12 bytes can be used for ICMP header */
} pingpkt;
u32 *datastart = (u32 *) pingpkt.data;
int datalen = sizeof(pingpkt.data); 
int icmplen=0;
int decoy;
int res;
struct sockaddr_in sock;
char *ping = (char *) &pingpkt;

 if (pingtype & PINGTYPE_ICMP_PING) {
   icmplen = 8; 
   pingpkt.type = 8;
 }
 else if (pingtype & PINGTYPE_ICMP_MASK) {
   icmplen = 12;
   *datastart++ = 0;
   datalen -= 4;
   pingpkt.type = 17;
 }
 else if (pingtype & PINGTYPE_ICMP_TS) {   
   icmplen = 20;
   memset(datastart, 0, 12);
   datastart += 12;
   datalen -= 12;
   pingpkt.type = 13;
 }
 else fatal("sendpingquery: unknown pingtype: %d", pingtype);

 if (o.extra_payload_length > 0) {
   icmplen += MIN(datalen, o.extra_payload_length);
   memset(datastart, 0, MIN(datalen, o.extra_payload_length));
 }
/* Fill out the ping packet */

pingpkt.code = 0;
pingpkt.id = id;
pingpkt.seq = seq;
pingpkt.checksum = 0;
pingpkt.checksum = in_cksum((unsigned short *)ping, icmplen);

/* Now for our sock */
if (ptech.icmpscan) {
  memset((char *)&sock, 0, sizeof(sock));
  sock.sin_family= AF_INET;
  sock.sin_addr = target->v4host();
  
  o.decoys[o.decoyturn].s_addr = target->v4source().s_addr;
}

 for (decoy = 0; decoy < o.numdecoys; decoy++) {
   if (ptech.icmpscan && decoy == o.decoyturn) {
     /* FIXME: If EHOSTUNREACH (Windows does that) then we were
	probably unable to obtain an arp response from the machine.
	We should just consider the host down rather than ignoring
	the error */
     // Can't currently do the tracer because 'ping' has no IP header
     //     PacketTrace::trace(PacketTrace::SENT, (u8 *) ping, icmplen); 
     if ((res = sendto(sd,(char *) ping,icmplen,0,(struct sockaddr *)&sock,
		       sizeof(struct sockaddr))) != icmplen && 
		       socket_errno() != EHOSTUNREACH 
#ifdef WIN32
        // Windows (correctly) returns this if we scan an address that is
        // known to be nonsensical (e.g. myip & mysubnetmask)
	&& socket_errno() != WSAEADDRNOTAVAIL
#endif 
		       ) {
       fprintf(stderr, "sendto in sendpingquery returned %d (should be 8)!\n", res);
       perror("sendto");
     }
   } else {
     send_ip_raw( rawsd, &o.decoys[decoy], target->v4hostip(), o.ttl, IPPROTO_ICMP, ping, icmplen);
   }
 }

 return 0;
}

int get_connecttcpscan_results(struct tcpqueryinfo *tqi, 
			       Target *hostbatch[], 
			       struct timeval *time, struct pingtune *pt, 
			       struct timeout_info *to) {

int res, res2;
int tm;
struct timeval myto, start, end;
int hostindex;
int trynum, newstate = HOST_DOWN;
int seq;
int p;
char buf[256];
int foundsomething = 0;
fd_set myfds_r,myfds_w,myfds_x;
gettimeofday(&start, NULL);
 
while(pt->block_unaccounted) {
  /* OK so there is a little fudge factor, SUE ME! */
  myto.tv_sec  = to->timeout / 1000000; 
  myto.tv_usec = to->timeout % 1000000;
  foundsomething = 0;
  myfds_r = tqi->fds_r;
  myfds_w = tqi->fds_w;
  myfds_x = tqi->fds_x;
  res = select(tqi->maxsd + 1, &myfds_r, &myfds_w, &myfds_x, &myto);
  if (res > 0) {
    for(hostindex = pt->group_start; hostindex <= pt->group_end; hostindex++) {
      for(trynum=0; trynum <= pt->block_tries; trynum++) {
	seq = hostindex * pt->max_tries + trynum;
	for(p=0; p < o.num_ping_synprobes; p++) {
	  if (tqi->sockets[p][seq] >= 0) {
	    if (o.debugging > 1) {
	      if (FD_ISSET(tqi->sockets[p][seq], &(myfds_r))) {
	        log_write(LOG_STDOUT, "WRITE selected for machine %s\n", hostbatch[hostindex]->targetipstr());  
	      }
	      if ( FD_ISSET(tqi->sockets[p][seq], &myfds_w)) {
	        log_write(LOG_STDOUT, "READ selected for machine %s\n", hostbatch[hostindex]->targetipstr()); 
	      }
	      if  ( FD_ISSET(tqi->sockets[p][seq], &myfds_x)) {
	        log_write(LOG_STDOUT, "EXC selected for machine %s\n", hostbatch[hostindex]->targetipstr());
	      }
	    }
	    if (FD_ISSET(tqi->sockets[p][seq], &myfds_r) || FD_ISSET(tqi->sockets[p][seq], &myfds_w) ||  FD_ISSET(tqi->sockets[p][seq], &myfds_x)) {
	      foundsomething = 0;
	      res2 = recv(tqi->sockets[p][seq], buf, sizeof(buf) - 1, 0);
	      if (res2 == -1) {
	        switch(socket_errno()) {
	        case ECONNREFUSED:
	        case EAGAIN:
#ifdef WIN32
//		  case WSAENOTCONN:	//	needed?  this fails around here on my system
#endif
		  if (socket_errno() == EAGAIN && o.verbose) {
		    log_write(LOG_STDOUT, "Machine %s MIGHT actually be listening on probe port %d\n", hostbatch[hostindex]->targetipstr(), o.ping_synprobes[p]);
		  }
		  foundsomething = 1;
		  newstate = HOST_UP;	
		  break;
	        case ENETDOWN:
	        case ENETUNREACH:
	        case ENETRESET:
	        case ECONNABORTED:
	        case ETIMEDOUT:
	        case EHOSTDOWN:
	        case EHOSTUNREACH:
		  foundsomething = 1;
		  newstate = HOST_DOWN;
		  break;
	        default:
		  snprintf (buf, sizeof(buf), "Strange read error from %s", hostbatch[hostindex]->targetipstr());
		  perror(buf);
		  break;
	        }
	      } else { 
	        foundsomething = 1;
	        newstate = HOST_UP;
	        if (o.verbose) {	      
		  buf[res2] = '\0';
		  if (res2 == 0)
		    log_write(LOG_STDOUT, "Machine %s is actually LISTENING on probe port %d\n",
			   hostbatch[hostindex]->targetipstr(), 
			   o.ping_synprobes[p]);
		  else 
		    log_write(LOG_STDOUT, "Machine %s is actually LISTENING on probe port %d, banner: %s\n",
			   hostbatch[hostindex]->targetipstr(), 
			   o.ping_synprobes[p], buf);
	        }
	      }
	      if (foundsomething) {
	        hostupdate(hostbatch, hostbatch[hostindex], newstate, 1, trynum,
			 to,  &time[seq], NULL, pt, tqi, pingstyle_connecttcp);
	      /*	      break;*/
	      }
	    }
	  }
        }
      }
    }
  }
  gettimeofday(&end, NULL);
  tm = TIMEVAL_SUBTRACT(end,start);  
  if (tm > (30 * to->timeout)) {
    error("WARNING: getconnecttcpscanresults is taking way too long, skipping");
    break;
  }
  if (res == 0 &&  tm > to->timeout) break; 
}

/* OK, now we have to kill all outstanding queries to make room for
   the next group :( I'll miss these little guys. */
 for(hostindex = pt->group_start; hostindex <= pt->group_end; hostindex++) { 
      for(trynum=0; trynum <= pt->block_tries; trynum++) {
	seq = hostindex * pt->max_tries + trynum;
	for(p=0; p < o.num_ping_synprobes; p++) {
	  if ( tqi->sockets[p][seq] >= 0) {
	    tqi->sockets_out--;
	    close(tqi->sockets[p][seq]);
	    tqi->sockets[p][seq] = -1;
	  }
	}
      }
 }
 tqi->maxsd = 0;
 assert(tqi->sockets_out == 0);
 FD_ZERO(&(tqi->fds_r));
 FD_ZERO(&(tqi->fds_w));
 FD_ZERO(&(tqi->fds_x));
	 
return 0;
}


int get_ping_results(int sd, pcap_t *pd, Target *hostbatch[], int pingtype, 
		     struct timeval *time,  struct pingtune *pt, 
		     struct timeout_info *to, int id, struct pingtech *ptech, 
		     struct scan_lists *ports) {
  fd_set fd_r, fd_x;
  struct timeval myto, tmpto, start, end, rcvdtime;
  unsigned int bytes;
  int res;
  struct ppkt {
    unsigned char type;
    unsigned char code;
    unsigned short checksum;
    unsigned short id;
    unsigned short seq;
  } *ping = NULL, *ping2 = NULL;
  char response[16536]; 
  struct tcphdr *tcp;
  udphdr_bsd *udp;
  struct ip *ip, *ip2;
  u32 hostnum = 0xFFFFFF; /* This ought to crash us if it is used uninitialized */
  int tm;
  int dotimeout = 1;
  int newstate = HOST_DOWN;
  int foundsomething;
  unsigned short newport;
  int newportstate; /* Hack so that in some specific cases we can determine the 
		       state of a port and even skip the real scan */
  u32 trynum = 0xFFFFFF;
  enum pingstyle pingstyle = pingstyle_unknown;
  int timeout = 0;
  u16 sequence = 65534;
  unsigned long tmpl;
  unsigned short sportbase;

  FD_ZERO(&fd_r);
  FD_ZERO(&fd_x);

  /* Decide on the timeout, based on whether we need to also watch for TCP stuff */
  if (ptech->icmpscan && !ptech->rawtcpscan && !ptech->rawudpscan) {
    /* We only need to worry about pings, so we set timeout for the whole she-bang! */
    myto.tv_sec  = to->timeout / 1000000;
    myto.tv_usec = to->timeout % 1000000;
  } else {
    myto.tv_sec = 0;
    myto.tv_usec = 20000;
  }

  if (o.magic_port_set) sportbase = o.magic_port;
  else sportbase = o.magic_port + 20;

  gettimeofday(&start, NULL);
  newport = 0;
  newportstate = PORT_UNKNOWN;

  while(pt->block_unaccounted > 0 && !timeout) {
    tmpto = myto;

    if (pd) {
      ip = (struct ip *) readip_pcap(pd, &bytes, to->timeout, &rcvdtime);
    } else {    
      FD_SET(sd, &fd_r);
      FD_SET(sd, &fd_x);
      res = select(sd+1, &fd_r, NULL, &fd_x, &tmpto);
      if (res == 0) break;
      bytes = recv(sd, response,sizeof(response), 0 );
      ip = (struct ip *) response;
      if (bytes > 0) {
	gettimeofday(&rcvdtime, NULL);
	PacketTrace::trace(PacketTrace::RCVD, (u8 *) response, bytes, &rcvdtime);
      }
    }

    gettimeofday(&end, NULL);
    tm = TIMEVAL_SUBTRACT(end,start);  
    if (tm > (MAX(400000,3 * to->timeout)))
      timeout = 1;
    if (bytes == 0 &&  tm > to->timeout) {  
      timeout = 1;
    }
    if (bytes == 0)
      continue;

    if (bytes > 0 && bytes <= 20) {  
      error("%d byte micro packet received in get_ping_results", bytes);
      continue;
    }  

    foundsomething = 0;
    dotimeout = 0;
  
    /* First check if it is ICMP, TCP, or UDP */
    if (ip->ip_p == IPPROTO_ICMP) {    
      /* if it is our response */
      ping = (struct ppkt *) ((ip->ip_hl * 4) + (char *) ip);
      if (bytes < ip->ip_hl * 4 + 8U) {
	error("Supposed ping packet is only %d bytes long!", bytes);
	continue;
      }
      /* Echo reply, Timestamp reply, or Address Mask Reply */
      if  ( (ping->type == 0 || ping->type == 14 || ping->type == 18)
	    && !ping->code && ping->id == id) {
	sequence = ping->seq - pt->seq_offset;
	hostnum = sequence / pt->max_tries;
	if (hostnum > (u32) pt->group_end) {
	  if (o.debugging) 
	    error("Ping sequence %hu leads to hostnum %d which is beyond the end of this group (%d)", sequence, hostnum, pt->group_end);
	  continue;
	}

	if (o.debugging) 
	  log_write(LOG_STDOUT, "We got a ping packet back from %s: id = %d seq = %d checksum = %d\n", inet_ntoa(ip->ip_src), ping->id, ping->seq, ping->checksum);

	if (hostbatch[hostnum]->v4host().s_addr == ip->ip_src.s_addr) {
	  foundsomething = 1;
	  pingstyle = pingstyle_icmp;
	  newstate = HOST_UP;
	  trynum = sequence % pt->max_tries;
	  dotimeout = 1;

	  if (!hostbatch[hostnum]->v4sourceip()) {      	
	    struct sockaddr_in sin;
	    memset(&sin, 0, sizeof(sin));
	    sin.sin_family = AF_INET;
	    sin.sin_addr.s_addr = ip->ip_dst.s_addr;
#if HAVE_SOCKADDR_SA_LEN
	    sin.sin_len = sizeof(sin);
#endif
	    hostbatch[hostnum]->setSourceSockAddr((struct sockaddr_storage *) &sin,
						  sizeof(sin));
	  }
	}
	else hostbatch[hostnum]->wierd_responses++;
      }

      // Destination unreachable, source quench, or time exceeded 
      else if (ping->type == 3 || ping->type == 4 || ping->type == 11 || o.debugging) {
	if (bytes <  ip->ip_hl * 4 + 28U) {
	  if (o.debugging)
	    error("ICMP type %d code %d packet is only %d bytes\n", ping->type, ping->code, bytes);
	  continue;
	}

	ip2 = (struct ip *) ((char *)ip + ip->ip_hl * 4 + 8);
	if (bytes < ip->ip_hl * 4 + 8U + ip2->ip_hl * 4 + 8U) {
	  if (o.debugging)
	    error("ICMP (embedded) type %d code %d packet is only %d bytes\n", ping->type, ping->code, bytes);
	  continue;
	}
      
	if (ip2->ip_p == IPPROTO_ICMP) {
	  /* The response was based on a ping packet we sent */
	  if (!ptech->icmpscan && !ptech->rawicmpscan) {
	    if (o.debugging)
	      error("Got ICMP error referring to ICMP msg which we did not send");
	    continue;
	  }
	  ping2 = (struct ppkt *) ((char *)ip2 + ip2->ip_hl * 4);
	  if (ping2->id != id) {
	    if (o.debugging) {	
	      error("Illegal id %d found, should be %d (icmp type/code %d/%d)", ping2->id, id, ping->type, ping->code);
	      if (o.debugging > 1)
		lamont_hdump((char *)ip, bytes);
	    }
	    continue;
	  }
	  sequence = ping2->seq - pt->seq_offset;
	  hostnum = sequence / pt->max_tries;
	  trynum = sequence % pt->max_tries;

	  if (trynum >= (u32) pt->max_tries || hostnum > (u32) pt->group_end || 
	      hostbatch[hostnum]->v4host().s_addr != ip2->ip_dst.s_addr) {
	    if (o.debugging) {
	      error("Bogus trynum, sequence number or unexpected IP address in ICMP error message\n");
	    }
	    continue;
	  }
	  
	} else if (ip2->ip_p == IPPROTO_TCP) {
	  /* The response was based our TCP probe */
	  if (!ptech->rawtcpscan) {
	    if (o.debugging)
	      error("Got ICMP error referring to TCP msg which we did not send");
	    continue;
	  }
	  tcp = (struct tcphdr *) (((char *) ip2) + 4 * ip2->ip_hl);
	  /* No need to check size here, the "+8" check a ways up takes care 
	     of it */
	  newport = ntohs(tcp->th_dport);
	  
	  trynum = ntohs(tcp->th_sport) - sportbase;
	  if (trynum >= (u32) pt->max_tries) {
	    if (o.debugging)
	      error("Bogus trynum %d", trynum);
	    continue;
	  }
	  
	  /* Grab the sequence nr */
	  tmpl = ntohl(tcp->th_seq);
	  
	  if ((tmpl & 0x3F) == 0x1E) {
	    sequence = ((tmpl >> 6) & 0xffff) - pt->seq_offset;
	    hostnum = sequence / pt->max_tries;
	    trynum = sequence % pt->max_tries;
	  } else {
	    if (o.debugging) {
	      error("Whacked seq number from %s", inet_ntoa(ip->ip_src));
	    }
	    continue;	
	  }

	  if (trynum >= (u32) pt->max_tries || hostnum > (u32) pt->group_end || 
	      hostbatch[hostnum]->v4host().s_addr != ip2->ip_dst.s_addr) {
	    if (o.debugging) {
	      error("Bogus trynum, sequence number or unexpected IP address in ICMP error message\n");
	    }
	    continue;
	  }


	} else if (ip2->ip_p == IPPROTO_UDP) {
	  /* The response was based our UDP probe */
	  if (!ptech->rawudpscan) {
	    if (o.debugging)
	      error("Got ICMP error referring to UDP msg which we did not send");
	    continue;
	  }
	  
	  sequence = ntohs(ip2->ip_id) - pt->seq_offset;
	  hostnum = sequence / pt->max_tries;
	  trynum = sequence % pt->max_tries;
	  
	  if (trynum >= (u32) pt->max_tries || hostnum > (u32) pt->group_end || 
	      hostbatch[hostnum]->v4host().s_addr != ip2->ip_dst.s_addr) {
	    if (o.debugging) {
	      error("Bogus trynum, sequence number or unexpected IP address in ICMP error message\n");
	    }
	    continue;
	  }

	} else {
	  if (o.debugging)
	    error("Got ICMP response to a packet which was not TCP, UDP, or ICMP");
	  continue;
	}

	assert (hostnum <= (u32) pt->group_end);
        
	if (ping->type == 3) {
	  dotimeout = 1;	
	  foundsomething = 1;
	  pingstyle = pingstyle_icmp;
	  if (ping->code == 3 && ptech->rawudpscan) {
            /* ICMP port unreachable -- the port is closed but aparently the machine is up! */
	    newstate = HOST_UP;
          } else {
	    if (o.debugging) 
	      log_write(LOG_STDOUT, "Got destination unreachable for %s\n", hostbatch[hostnum]->targetipstr());
	    /* Since this gives an idea of how long it takes to get an answer,
	       we add it into our times */
	    newstate = HOST_DOWN;
	    newportstate = PORT_FIREWALLED;
	  }
	} else if (ping->type == 11) {
	  if (o.debugging) 
	    log_write(LOG_STDOUT, "Got Time Exceeded for %s\n", hostbatch[hostnum]->targetipstr());
	  dotimeout = 0; /* I don't want anything to do with timing this */
	  foundsomething = 1;
	  pingstyle = pingstyle_icmp;
	  newstate = HOST_DOWN;
	}
	else if (ping->type == 4) {      
	  if (o.debugging) log_write(LOG_STDOUT, "Got ICMP source quench\n");
	  usleep(50000);
	} 
	else if (o.debugging > 0) {
	  log_write(LOG_STDOUT, "Got ICMP message type %d code %d\n", ping->type, ping->code);
	}
      }
    } else if (ip->ip_p == IPPROTO_TCP) 
      {
	if (!ptech->rawtcpscan) {
	  continue;
	}
	tcp = (struct tcphdr *) (((char *) ip) + 4 * ip->ip_hl);
	if (!(tcp->th_flags & TH_RST) && ((tcp->th_flags & (TH_SYN|TH_ACK)) != (TH_SYN|TH_ACK)))
	  continue;
	newport = ntohs(tcp->th_sport);
	tmpl = ntohl(tcp->th_ack);
	if ((tmpl & 0x3F) != 0x1E && (tmpl & 0x3F) != 0x1F)
	  tmpl = ntohl(tcp->th_seq); // We'll try the seq -- it is often helpful
	                             // in ACK scan responses

	if ((tmpl & 0x3F) == 0x1E || (tmpl & 0x3F) == 0x1F) {
	  sequence = ((tmpl >> 6) & 0xffff) - pt->seq_offset;
	  hostnum = sequence / pt->max_tries;
	  trynum = sequence % pt->max_tries;
	} else {
	  // Didn't get it back in either field -- we'll brute force it ...
	  for(hostnum = pt->group_end; hostnum != (u32) -1; hostnum--) {
	    if (hostbatch[hostnum]->v4host().s_addr == ip->ip_src.s_addr)
	      break;
	  }
	  if (hostnum == (u32) -1) {	
	    if (o.debugging > 1) 
	      error("Warning, unexpected packet from machine %s", inet_ntoa(ip->ip_src));
	    continue;
	  }
	  trynum = ntohs(tcp->th_dport) - sportbase;
	  sequence = hostnum * pt->max_tries + trynum;
	}

	if (trynum >= (u32) pt->max_tries) {
	  if (o.debugging)
	    error("Bogus trynum %d", trynum);
	  continue;
	}

	if (hostnum > (u32) pt->group_end) {
	  if (o.debugging) {
	    error("Response from host beyond group_end");
	  }
	  continue;
	}

	if (hostbatch[hostnum]->v4host().s_addr != ip->ip_src.s_addr) {
	  if (o.debugging) {
	    error("TCP ping response from unexpected host %s\n", inet_ntoa(ip->ip_src));
	  }
	  continue;
	}

	if (o.debugging) 
	  log_write(LOG_STDOUT, "We got a TCP ping packet back from %s port %hi (hostnum = %d trynum = %d\n", inet_ntoa(ip->ip_src), htons(tcp->th_sport), hostnum, trynum);
	pingstyle = pingstyle_rawtcp;
	foundsomething = 1;
	dotimeout = 1;
	newstate = HOST_UP;

	if (pingtype & PINGTYPE_TCP_USE_SYN) {
	  if (tcp->th_flags & TH_RST) {
	    newportstate = PORT_CLOSED;
	  } else if ((tcp->th_flags & (TH_SYN|TH_ACK)) == (TH_SYN|TH_ACK)) {
	    newportstate = PORT_OPEN;
	  }
	}
      } else if (ip->ip_p == IPPROTO_UDP) {

	if (!ptech->rawudpscan) {
	  continue;
	}
	udp = (udphdr_bsd *) (((char *) ip) + 4 * ip->ip_hl);
	newport = ntohs(udp->uh_sport);

	trynum = ntohs(udp->uh_dport) - sportbase;
	if (trynum >= (u32) pt->max_tries) {
	  if (o.debugging)
	    error("Bogus trynum %d", trynum);
	  continue;
	}

	/* Since this UDP response doesn't give us the sequence number, we'll have to brute force 
	   lookup to find the hostnum */
	for(hostnum = pt->group_end; hostnum != (u32) -1; hostnum--) {
	  if (hostbatch[hostnum]->v4host().s_addr == ip->ip_src.s_addr)
	    break;
	}
	if (hostnum == (u32) -1) {	
	  if (o.debugging > 1) 
	    error("Warning, unexpected packet from machine %s", inet_ntoa(ip->ip_src));
	  continue;
	}	

	sequence = hostnum * pt->max_tries + trynum;

	if (o.debugging) 
	  log_write(LOG_STDOUT, "In response to UDP-ping, we got UDP packet back from %s port %hi (hostnum = %d trynum = %d\n", inet_ntoa(ip->ip_src), htons(udp->uh_sport), hostnum, trynum);
	pingstyle = pingstyle_rawudp;
	foundsomething = 1;
	dotimeout = 1;
	newstate = HOST_UP;
      }
    else if (o.debugging) {
      error("Found whacked packet protocol %d in get_ping_results", ip->ip_p);
    }
    if (foundsomething) {  
      hostupdate(hostbatch, hostbatch[hostnum], newstate, dotimeout, 
		 trynum, to, &time[sequence], &rcvdtime, pt, NULL, pingstyle);
    }
    if (newport && newportstate != PORT_UNKNOWN) {
      /* OK, we can add it, but that is only appropriate if this is one
	 of the ports the user ASKED for */
      if (ports && ports->tcp_count == 1 && ports->tcp_ports[0] == newport)
	hostbatch[hostnum]->ports.addPort(newport, IPPROTO_TCP, NULL, 
					  newportstate);
    }
  }
  return 0;
}

char *readhoststate(int state) {
  switch(state) {
  case HOST_UP:
    return "HOST_UP";
  case HOST_DOWN:
    return "HOST_DOWN";
  case HOST_FIREWALLED:
    return "HOST_FIREWALLED";
  default:
    return "UNKNOWN/COMBO";
  }
  return NULL;
}


