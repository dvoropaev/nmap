
/***********************************************************************/
/* targets.c -- Functions relating to "ping scanning" as well as       */
/* determining the exact IPs to hit based on CIDR and other input      */
/* formats.                                                            */
/*                                                                     */
/***********************************************************************/
/*  The Nmap Security Scanner is (C) 1995-2001 Insecure.Com LLC. This  */
/*  program is free software; you can redistribute it and/or modify    */
/*  it under the terms of the GNU General Public License as published  */
/*  by the Free Software Foundation; Version 2.  This guarantees your  */
/*  right to use, modify, and redistribute this software under certain */
/*  conditions.  If this license is unacceptable to you, we may be     */
/*  willing to sell alternative licenses (contact sales@insecure.com). */
/*                                                                     */
/*  If you received these files with a written license agreement       */
/*  stating terms other than the (GPL) terms above, then that          */
/*  alternative license agreement takes precendence over this comment. */
/*                                                                     */
/*  Source is provided to this software because we believe users have  */
/*  a right to know exactly what a program is going to do before they  */
/*  run it.  This also allows you to audit the software for security   */
/*  holes (none have been found so far).                               */
/*                                                                     */
/*  Source code also allows you to port Nmap to new platforms, fix     */
/*  bugs, and add new features.  You are highly encouraged to send     */
/*  your changes to fyodor@insecure.org for possible incorporation     */
/*  into the main distribution.  By sending these changes to Fyodor or */
/*  one the insecure.org development mailing lists, it is assumed that */
/*  you are offering Fyodor the unlimited, non-exclusive right to      */
/*  reuse, modify, and relicense the code.  This is important because  */
/*  the inability to relicense code has caused devastating problems    */
/*  for other Free Software projects (such as KDE and NASM).  Nmap     */
/*  will always be available Open Source.  If you wish to specify      */
/*  special license conditions of your contributions, just say so      */
/*  when you send them.                                                */
/*                                                                     */
/*  This program is distributed in the hope that it will be useful,    */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of     */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU  */
/*  General Public License for more details (                          */
/*  http://www.gnu.org/copyleft/gpl.html ).                            */
/*                                                                     */
/***********************************************************************/

/* $Id: targets.c,v 1.80 2002/04/02 06:57:12 fyodor Exp $ */


#include "targets.h"
#include "timing.h"
#include "osscan.h"

extern struct ops o;

enum pingstyle { pingstyle_unknown, pingstyle_rawtcp, pingstyle_connecttcp, 
		 pingstyle_icmp };

/*  predefined filters -- I need to kill these globals at some pont. */
extern unsigned long flt_dsthost, flt_srchost;
extern unsigned short flt_baseport;

/* Internal function to update the state of machine (up/down/etc) based on
   ping results */
static int hostupdate(struct hoststruct *hostbatch, struct hoststruct *target, 
	       int newstate, int dotimeout, int trynum, 
	       struct timeout_info *to, struct timeval *sent, 
	       struct pingtune *pt, struct tcpqueryinfo *tqi, 
	       enum pingstyle style)
{

  int hostnum = target - hostbatch;
  int i;
  int seq;
  int tmpsd;
  struct timeval tv;
  
  if (o.debugging)  {
    gettimeofday(&tv, NULL);
    log_write(LOG_STDOUT, "Hostupdate called for machine %s state %s -> %s (trynum %d, dotimeadj: %s time: %ld)\n", inet_ntoa(target->host), readhoststate(target->flags), readhoststate(newstate), trynum, (dotimeout)? "yes" : "no", (long) TIMEVAL_SUBTRACT(tv, *sent));
  }
  assert(hostnum <= pt->group_end);
  
  if (dotimeout) {
    adjust_timeouts(*sent, to);
  }
  
  /* If this is a tcp connect() pingscan, close all sockets */
  
  if (style == pingstyle_connecttcp) {
    seq = (target - hostbatch) * pt->max_tries + trynum;
    assert(tqi->sockets[seq] >= 0);
    for(i=0; i <= pt->block_tries; i++) {  
      seq = (target - hostbatch) * pt->max_tries + i;
      tmpsd = tqi->sockets[seq];
      if (tmpsd >= 0) {
	assert(tqi->sockets_out > 0);
	tqi->sockets_out--;
	close(tmpsd);
	if (tmpsd == tqi->maxsd) tqi->maxsd--;
	FD_CLR(tmpsd, &(tqi->fds_r));
	FD_CLR(tmpsd, &(tqi->fds_w));
	FD_CLR(tmpsd, &(tqi->fds_x));
	tqi->sockets[seq] = -1;
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
      log_write(LOG_STDOUT, "Decreasing massping group size from %d to ", pt->group_size);
    pt->group_size = MAX((int) (pt->group_size * 0.75), 10);
    if (o.debugging) 
      log_write(LOG_STDOUT, "%d\n", pt->group_size);
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
    pt->down_this_block++;
    pt->block_unaccounted--;
    pt->num_responses++;
  } else {
    assert(newstate == HOST_UP);
    target->flags |= HOST_UP;
    pt->up_this_block++;
    pt->block_unaccounted--;
    pt->num_responses++;
  }
  return 0;
}


/* Fills up the hostgroup_state structure passed in (which must point
   to valid memory).  Lookahead is the number of hosts that can be
   checked (such as ping scanned) in advance.  Randomize causes each
   group of up to lookahead hosts to be internally shuffled around.
   The target_expressions array must remail valid in memory as long as
   this hostgroup_state structure is used -- the array is NOT copied.
   Also, REMEMBER TO CALL hostgroup_state_destroy() when you are done
   with the hostgroup_state (the latter function only frees internal
   resources -- you still have to free the alocated memory (if any)
   for the struct hostgroup_state itself.  */
int hostgroup_state_init(struct hostgroup_state *hs, int lookahead,
			 int randomize, char *target_expressions[],
			 int num_expressions) {
  bzero(hs, sizeof(struct hostgroup_state));
  assert(lookahead > 0);
  hs->hostbatch = (struct hoststruct *) safe_malloc(lookahead * sizeof(struct hoststruct));
  hs->max_batch_sz = lookahead;
  hs->current_batch_sz = 0;
  hs->next_batch_no = 0;
  hs->randomize = randomize;
  hs->target_expressions = target_expressions;
  hs->num_expressions = num_expressions;
  hs->next_expression = 0;
  hs->current_expression.nleft = 0; 
  return 0;
}

/* Free the *internal state* of a hostgroup_state structure -- it is
   important to note that this does not free the actual memory
   allocated for the "struct hostgroup_state" you pass in.  It only
   frees internal stuff -- after all, your hostgroup_state could be on
   the stack */
void hostgroup_state_destroy(struct hostgroup_state *hs) {
  if (!hs) fatal("NULL hostgroup_state passed to hostgroup_state_destroy()!");
  if (!hs->hostbatch) fatal("hostgroup_state passed to hostgroup_state_destroy() contains NULL hostbatch!");
  free(hs->hostbatch);
}


/* If there is at least one IP address left in t, one is pulled out and placed
   in sin and then zero is returned and state information in t is updated
   to reflect that the IP was pulled out.  If t is empty, -1 is returned */
int target_struct_get(struct targets *t, struct in_addr *sin) {
  int octet;

  startover: /* to hande nmap --resume where I have already
		scanned many of the IPs */  

  if (t->nleft <= 0)
    return -1;
  
  if (t->maskformat) {
    if (t->currentaddr.s_addr <= t->end.s_addr) {
      sin->s_addr = htonl(t->currentaddr.s_addr++);
    } else {
      error("Bogus target structure passed to target_struct_get");
      t->nleft = 0;
      sin->s_addr = 0;
      return -1;
    }
  }
  else {
    if (o.debugging > 2) {
      log_write(LOG_STDOUT, "doing %d.%d.%d.%d = %d.%d.%d.%d\n", t->current[0], t->current[1], t->current[2], t->current[3], t->addresses[0][t->current[0]],t->addresses[1][t->current[1]],t->addresses[2][t->current[2]],t->addresses[3][t->current[3]]);
    }
    /* Set the IP to the current value of everything */
    sin->s_addr = htonl(t->addresses[0][t->current[0]] << 24 | 
			t->addresses[1][t->current[1]] << 16 |
			t->addresses[2][t->current[2]] << 8 | 
			t->addresses[3][t->current[3]]);
    
    /* Now we nudge up to the next IP */
    for(octet = 3; octet >= 0; octet--) {
      if (t->current[octet] < t->last[octet]) {
	/* OK, this is the column I have room to nudge upwards */
	t->current[octet]++;
	break;
      } else {
	/* This octet is finished so I reset it to the beginning */
	t->current[octet] = 0;
      }
    }
    if (octet == -1) {
      /* It didn't find anything to bump up, I muast have taken the last IP */
      assert(t->nleft == 1);
      /* So I set current to last with the very final octet up one ... */
      /* Note that this may make t->current[3] == 256 */
      t->current[0] = t->last[0]; t->current[1] = t->last[1];
      t->current[2] = t->last[2]; t->current[3] = t->last[3] + 1;
    } else {
      assert(t->nleft > 1); /* There must be at least one more IP left */
    }
  }
  t->nleft--;
  assert(t->nleft >= 0);
  
  /* If we are resuming from a previous scan, we have already finished
     scans up to o.resume_ip.  */
  if (o.resume_ip.s_addr) {
    if (o.resume_ip.s_addr == sin->s_addr)
      o.resume_ip.s_addr = 0; /* So that we will KEEP the next one */
    goto startover; /* Try again */
  }

  return 1;
}

/* Undoes the previous target_struct_get operation */
void target_struct_return(struct targets *t) {
  int octet;
  t->nleft++;
  if (t->maskformat) {
    assert(t->currentaddr.s_addr > t->start.s_addr);
    t->currentaddr.s_addr--;
  }
  else {
    for(octet = 3; octet >= 0; octet--) {
      if (t->current[octet] > 0) {
	/* OK, this is the column I have room to nudge downwards */
	t->current[octet]--;
	break;
      } else {
	/* This octet is already at the beginning, so I set it to the end */
	t->current[octet] = t->last[octet];
      }
    }
    assert(octet != -1);
  }
}

void hoststructfry(struct hoststruct *hostbatch, int nelem) {
  genfry((unsigned char *)hostbatch, sizeof(struct hoststruct), nelem);
  return;
}

/* REMEMBER TO CALL hoststruct_free() on the hoststruct when you are done
   with it!!! */
struct hoststruct *nexthost(struct hostgroup_state *hs, 
			    struct scan_lists *ports, int *pingtype) {
int hidx;
char *device;
int i;

if (hs->next_batch_no < hs->current_batch_sz) {
  /* Woop!  This is easy -- we just pass back the next host struct */
  return &hs->hostbatch[hs->next_batch_no++];
}
/* Doh, we need to refresh our array */
bzero(hs->hostbatch, hs->max_batch_sz * sizeof(struct hoststruct));
hs->current_batch_sz = hs->next_batch_no = 0;
do {
  /* Grab anything we have in our current_expression */
  while (hs->current_batch_sz < hs->max_batch_sz && 
	 target_struct_get(&hs->current_expression, 
	   &(hs->hostbatch[hs->current_batch_sz].host)) != -1)
    {
      hidx = hs->current_batch_sz;

      /* Lets figure out what device this IP uses ... */
      if (o.source) {
	memcpy((char *)&hs->hostbatch[hidx].source_ip,(char *) o.source, 
	       sizeof(struct in_addr));
	strcpy(hs->hostbatch[hidx].device, o.device);
      } else {
	/* We figure out the source IP/device IFF
	   1) We are r00t AND
	   2) We are doing tcp pingscan OR
	   3) We are doing NO scan AND we are doing a raw-mode portscan or 
	   osscan */
	if (o.isr00t && 
	    ((*pingtype & PINGTYPE_TCP) || 
	     (*pingtype == PINGTYPE_NONE && 
	      (o.synscan || o.finscan || o.xmasscan || o.nullscan || o.ipprotscan ||
	       o.maimonscan || o.idlescan || o.ackscan || o.udpscan || o.osscan || o.windowscan)))) {
	 device = routethrough(&(hs->hostbatch[hidx].host), &(hs->hostbatch[hidx].source_ip));
	 if (!device) {
	   if (*pingtype == PINGTYPE_NONE) {
	     fatal("Could not determine what interface to route packets through, run again with -e <device>");
	   } else {
	     error("WARNING:  Could not determine what interface to route packets through to %s, changing ping scantype to ICMP ping only", inet_ntoa(hs->hostbatch[hidx].host));
	     *pingtype = PINGTYPE_ICMP_PING;
	   }
	 } else {
	   strcpy(hs->hostbatch[hidx].device, device);
	 }
	}  
      }

      /* In some cases, we can only allow hosts that use the same device
	 in a group. */
      if (o.isr00t && hidx > 0 && *hs->hostbatch[hidx].device && hs->hostbatch[hidx].source_ip.s_addr != hs->hostbatch[0].source_ip.s_addr) {
	/* Cancel everything!  This guy must go in the next group and we are
	   outtof here */
	target_struct_return(&(hs->current_expression));
	goto batchfull;
      }

      hs->current_batch_sz++;
    }

  if (hs->current_batch_sz < hs->max_batch_sz &&
      hs->next_expression < hs->num_expressions) {
    /* We are going to have to plop in another expression. */
    while (!parse_targets(&(hs->current_expression), hs->target_expressions[hs->next_expression++])) {
      if (hs->next_expression >= hs->num_expressions)
	break;
    }     
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
     ((hs->hostbatch[0].host.s_addr || !o.isr00t) && 
      (*pingtype != PINGTYPE_NONE))) 
   massping(hs->hostbatch, hs->current_batch_sz, ports, *pingtype);
 else for(i=0; i < hs->current_batch_sz; i++)  {
   hs->hostbatch[i].to.srtt = -1;
   hs->hostbatch[i].to.rttvar = -1;
   hs->hostbatch[i].to.timeout = o.initial_rtt_timeout * 1000;
   hs->hostbatch[i].flags |= HOST_UP; /*hostbatch[i].up = 1;*/
 }
 return &hs->hostbatch[hs->next_batch_no++];
}

/* Frees the *INTERNAL STRUCTURES* inside a hoststruct -- does not
   free the actual memory allocated to the hoststruct itself (for all
   this function knows, you could have declared it on the stack */
void hoststruct_free(struct hoststruct *currenths) {
  int i;

  /* Free the DNS name if we resolved one */
  if (currenths->name && *currenths->name)
    free(currenths->name);

  /* Free OS fingerprints of OS scanning was done */
  for(i=0; i < currenths->numFPs; i++) {
    freeFingerPrint(currenths->FPs[i]);
    currenths->FPs[i] = NULL;
  }
  currenths->numFPs = 0;

  /* Free the port lists */
  resetportlist(&currenths->ports);

}



int parse_targets(struct targets *targets, char *h) {
int i=0,j=0,k=0;
int start, end;
char *r,*s, *target_net;
char *addy[5];
char *hostexp = strdup(h);
struct hostent *target;
unsigned long longtmp;
int namedhost = 0;

bzero(targets, sizeof(struct targets));
targets->nleft = 0;
/*struct in_addr current_in;*/
addy[0] = addy[1] = addy[2] = addy[3] = addy[4] = NULL;
addy[0] = r = hostexp;
/* First we break the expression up into the four parts of the IP address
   + the optional '/mask' */
target_net = strtok(hostexp, "/");
s = strtok(NULL, "");    /* find the end of the token from hostexp */
targets->netmask  = ( s ) ? atoi(s) : 32;
if ((int) targets->netmask < 0 || targets->netmask > 32) {
  fprintf(stderr, "Illegal netmask value (%d), must be /0 - /32 .  Assuming /32 (one host)\n", targets->netmask);
  targets->netmask = 32;
}
for(i=0; *(hostexp + i); i++) 
  if (isupper((int) *(hostexp +i)) || islower((int) *(hostexp +i))) {
  namedhost = 1;
  break;
}
if (targets->netmask != 32 || namedhost) {
  targets->maskformat = 1;
 if (!inet_aton(target_net, &(targets->start))) {
    if ((target = gethostbyname(target_net)))
      memcpy(&(targets->start), target->h_addr_list[0], sizeof(struct in_addr));
    else {
      fprintf(stderr, "Failed to resolve given hostname/IP: %s.  Note that you can't use '/mask' AND '[1-4,7,100-]' style IP ranges\n", target_net);
      free(hostexp);
      return 0;
    }
 } 
 longtmp = ntohl(targets->start.s_addr);
 targets->start.s_addr = longtmp & (unsigned long) (0 - (1<<(32 - targets->netmask)));
 targets->end.s_addr = longtmp | (unsigned long)  ((1<<(32 - targets->netmask)) - 1);
 targets->currentaddr = targets->start;
 if (targets->start.s_addr <= targets->end.s_addr) { 
   targets->nleft = targets->end.s_addr - targets->start.s_addr + 1;
   free(hostexp); 
   return 1; 
 }
 fprintf(stderr, "Host specification invalid");
 free(hostexp);
 return 0;
}
else {
  i=0;
  targets->maskformat = 0;
  while(*++r) {
    if (*r == '.' && ++i < 4) {
      *r = '\0';
      addy[i] = r + 1;
    }
    else if (*r == '[') {
      *r = '\0';
      addy[i]++;
    }
    else if (*r == ']') *r = '\0';
    /*else if ((*r == '/' || *r == '\\') && i == 3) {
     *r = '\0';
     addy[4] = r + 1;
     }*/
    else if (*r != '*' && *r != ',' && *r != '-' && !isdigit((int)*r)) fatal("Invalid character in  host specification.");
  }
  if (i != 3) fatal("Target host specification is illegal.");
  
  for(i=0; i < 4; i++) {
    j=0;
    while((s = strchr(addy[i],','))) {
      *s = '\0';
      if (*addy[i] == '*') { start = 0; end = 255; } 
      else if (*addy[i] == '-') {
	start = 0;
	if (!addy[i] + 1) end = 255;
	else end = atoi(addy[i]+ 1);
      }
      else {
	start = end = atoi(addy[i]);
	if ((r = strchr(addy[i],'-')) && *(r+1) ) end = atoi(r + 1);
	else if (r && !*(r+1)) end = 255;
      }
      if (o.debugging)
	log_write(LOG_STDOUT, "The first host is %d, and the last one is %d\n", start, end);
      if (start < 0 || start > end) fatal("Your host specifications are illegal!");
      for(k=start; k <= end; k++)
	targets->addresses[i][j++] = k;
      addy[i] = s + 1;
    }
    if (*addy[i] == '*') { start = 0; end = 255; } 
    else if (*addy[i] == '-') {
      start = 0;
      if (!addy[i] + 1) end = 255;
      else end = atoi(addy[i]+ 1);
    }
    else {
      start = end = atoi(addy[i]);
      if ((r =  strchr(addy[i],'-')) && *(r+1) ) end = atoi(r+1);
      else if (r && !*(r+1)) end = 255;
    }
    if (o.debugging)
      log_write(LOG_STDOUT, "The first host is %d, and the last one is %d\n", start, end);
    if (start < 0 || start > end) fatal("Your host specifications are illegal!");
    if (j + (end - start) > 255) fatal("Your host specifications are illegal!");
    for(k=start; k <= end; k++) 
      targets->addresses[i][j++] = k;
    targets->last[i] = j - 1;
    
  }
}
  bzero((char *)targets->current, 4);
  targets->nleft = (targets->last[0] + 1) * (targets->last[1] + 1) *
    (targets->last[2] + 1) * (targets->last[3] + 1);
  free(hostexp);
  return 1;
}


void massping(struct hoststruct *hostbatch, int num_hosts, 
              struct scan_lists *ports, int pingtype) {
static struct timeout_info to = { 0,0,0};
static int gsize = LOOKAHEAD;
int hostnum;
struct pingtune pt;
struct scanstats ss;
struct timeval begin_select;
struct pingtech ptech;
struct tcpqueryinfo tqi;
int max_block_size = 40;
struct ppkt {
  unsigned char type;
  unsigned char code;
  unsigned short checksum;
  unsigned short id;
  unsigned short seq;
};
int elapsed_time;
int blockinc;
int sd_blocking = 1;
struct sockaddr_in sock;
short seq = 0;
int sd = -1, rawsd = -1, rawpingsd = -1;
struct timeval *time;
struct timeval start, end, t2;
unsigned short id;
pcap_t *pd = NULL;
char filter[512];
unsigned short sportbase;
int max_width = 0;

bzero((char *)&ptech, sizeof(struct pingtech));

bzero((char *) &pt, sizeof(struct pingtune)); 

pt.up_this_block = 0;
pt.block_unaccounted = LOOKAHEAD;
pt.discardtimesbefore = 0;
pt.down_this_block = 0;
pt.num_responses = 0;
pt.max_tries = 5; /* Maximum number of tries for a block */
pt.group_size = (o.max_parallelism)? MIN(o.max_parallelism, gsize) : gsize;
pt.group_start = 0;
pt.block_tries = 0; /* How many tries this block has gone through */

/* What port should we send from? */
if (o.magic_port_set) sportbase = o.magic_port;
else sportbase = o.magic_port + 20;

/* What kind of scans are we doing? */
 if ((pingtype & (PINGTYPE_ICMP_PING|PINGTYPE_ICMP_MASK|PINGTYPE_ICMP_TS)) && 
     hostbatch[0].source_ip.s_addr) 
  ptech.rawicmpscan = 1;
else if (pingtype & (PINGTYPE_ICMP_PING|PINGTYPE_ICMP_MASK|PINGTYPE_ICMP_TS)) 
  ptech.icmpscan = 1;
if (pingtype & PINGTYPE_TCP) {
  if (o.isr00t)
    ptech.rawtcpscan = 1;
  else ptech.connecttcpscan = 1;
}

time = (struct timeval *) safe_malloc(sizeof(struct timeval) * ((pt.max_tries) * num_hosts));
bzero(time, sizeof(struct timeval) * pt.max_tries * num_hosts);
id = (unsigned short) get_random_uint();

if (ptech.connecttcpscan)  {
  max_width = (o.max_parallelism)? o.max_parallelism : MAX(1, max_sd() - 4);
  max_block_size = MIN(50, max_width);
}


bzero((char *)&tqi, sizeof(tqi));
if (ptech.connecttcpscan) {
  tqi.sockets = (int *) safe_malloc(sizeof(int) * (pt.max_tries) * num_hosts);
  memset(tqi.sockets, 255, sizeof(int) * (pt.max_tries) * num_hosts);
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
  to.timeout = o.initial_rtt_timeout * 1000;
  to.srtt = -1;
  to.rttvar = -1;
} 

/* Init our raw socket */
if (o.numdecoys > 1 || ptech.rawtcpscan || ptech.rawicmpscan) {
  if ((rawsd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0 )
    pfatal("socket trobles in massping");
  broadcast_socket(rawsd);

  
  if ((rawpingsd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0 )
    pfatal("socket trobles in massping");
  broadcast_socket(rawpingsd);
}
 else { rawsd = -1; rawpingsd = -1; }

if (ptech.rawicmpscan || ptech.rawtcpscan) {
  /* we need a pcap descript0r! */
  /* MAX snaplen needed = 
     24 bytes max link_layer header
     64 bytes max IPhdr
     16 bytes of the TCP header
     ---
   = 104 byte snaplen */
  pd = my_pcap_open_live(hostbatch[0].device, 104, o.spoofsource, 20);

  flt_dsthost = hostbatch[0].source_ip.s_addr;
  flt_baseport = sportbase;

  snprintf(filter, sizeof(filter), "(icmp and dst host %s) or (tcp and dst host %s and ( dst port %d or dst port %d or dst port %d or dst port %d or dst port %d))", 
	  inet_ntoa(hostbatch[0].source_ip),inet_ntoa(hostbatch[0].source_ip),
	  sportbase , sportbase + 1, sportbase + 2, sportbase + 3, 
	  sportbase + 4);

  set_pcap_filter(hostbatch, pd, flt_icmptcp_5port, filter); 
}

 if (ptech.rawicmpscan + ptech.icmpscan + ptech.connecttcpscan +
     ptech.rawtcpscan == 1)
   blockinc = 8;
 else blockinc = 5;

bzero((char *)&sock,sizeof(struct sockaddr_in));
gettimeofday(&start, NULL);

 pt.group_end = MIN(pt.group_start + pt.group_size -1, num_hosts -1);
 
 while(pt.group_start < num_hosts) { /* while we have hosts left to scan */
   do { /* one block */
     pt.discardtimesbefore = -1;
     pt.up_this_block = 0;
     pt.down_this_block = 0;
     pt.block_unaccounted = 0;
     for(hostnum=pt.group_start; hostnum <= pt.group_end; hostnum++) {      
       /* If (we don't know whether the host is up yet) ... */
       if (!(hostbatch[hostnum].flags & HOST_UP) && !hostbatch[hostnum].wierd_responses && !(hostbatch[hostnum].flags & HOST_DOWN)) {  
	 /* Send a ping packet to it */
	 seq = hostnum * pt.max_tries + pt.block_tries;
	 if (ptech.icmpscan && !sd_blocking) { 
	   block_socket(sd); sd_blocking = 1; 
	 }
	 if (o.scan_delay) enforce_scan_delay(NULL);
	 if (ptech.icmpscan || ptech.rawicmpscan)
	   sendpingquery(sd, rawpingsd, &hostbatch[hostnum],  
			 seq, id, &ss, time, pingtype, ptech);
       
	 if (ptech.rawtcpscan) {
	   sendrawtcppingquery(rawsd, &hostbatch[hostnum],  pingtype, seq, 
			       time, &pt);
	 }
	 else if (ptech.connecttcpscan) {
	   sendconnecttcpquery(hostbatch, &tqi, &hostbatch[hostnum], seq, time, &pt, &to, max_width);
	 }
	 pt.block_unaccounted++;
	 gettimeofday(&t2, NULL);
	 if (TIMEVAL_SUBTRACT(t2,time[seq]) > 1000000) {
	   pt.discardtimesbefore = hostnum;
	   if (o.debugging) 
	     log_write(LOG_STDOUT, "Huge send delay: %lu microseconds\n", (unsigned long) TIMEVAL_SUBTRACT(t2,time[seq]));
	 }
       }
     } /* for() loop */
     /* OK, we have sent our ping packets ... now we wait for responses */
     gettimeofday(&begin_select, NULL);
     do {
       if (ptech.icmpscan && sd_blocking ) { 
	 unblock_socket(sd); sd_blocking = 0; 
       }
       if(ptech.icmpscan || ptech.rawicmpscan || ptech.rawtcpscan) {       
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
	we haven't run out of retries.  Also retry if the block is extremely
        small.
     */
     pt.dropthistry = 0;
     pt.block_tries++;
   } while ((pt.up_this_block > 0 || pt.group_end - pt.group_start <= 3) && pt.block_unaccounted > 0 && pt.block_tries < pt.max_tries);

   if (o.debugging)
     log_write(LOG_STDOUT, "Finished block: srtt: %d rttvar: %d timeout: %d block_tries: %d up_this_block: %d down_this_block: %d group_sz: %d\n", to.srtt, to.rttvar, to.timeout, pt.block_tries, pt.up_this_block, pt.down_this_block, pt.group_end - pt.group_start + 1);

   if ((pt.block_tries == 1) || (pt.block_tries == 2 && pt.up_this_block == 0 && pt.down_this_block == 0)) 
     /* Then it did not miss any hosts (that we know of)*/
       pt.group_size = MIN(pt.group_size + blockinc, max_block_size);
   
   /* Move to next block */
   pt.block_tries = 0;
   pt.group_start = pt.group_end +1;
   pt.group_end = MIN(pt.group_start + pt.group_size -1, num_hosts -1);
   /*   pt.block_unaccounted = pt.group_end - pt.group_start + 1;   */
 }

 close(sd);
 if (ptech.connecttcpscan) free(tqi.sockets);
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

int sendconnecttcpquery(struct hoststruct *hostbatch, struct tcpqueryinfo *tqi,
			struct hoststruct *target, int seq, 
			struct timeval *time, struct pingtune *pt, 
			struct timeout_info *to, int max_width) {

  int res,i;
  int tmpsd;
  int hostnum, trynum;
  struct sockaddr_in sock;
  int sockaddr_in_len = sizeof(struct sockaddr_in);
  
  trynum = seq % pt->max_tries;
  hostnum = seq / pt->max_tries;

  assert(tqi->sockets_out <= max_width);
  if (tqi->sockets_out == max_width) {
    /* We've got to free one! */
    for(i=0; i < trynum; i++) {
      tmpsd = hostnum * pt->max_tries + i;
      if (tqi->sockets[tmpsd] >= 0) {
	if (o.debugging) 
	  log_write(LOG_STDOUT, "sendconnecttcpquery: Scavenging a free socket due to serious shortage\n");
	close(tqi->sockets[tmpsd]);
	tqi->sockets[tmpsd] = -1;
	tqi->sockets_out--;
	break;
      }
    }
    if (i == trynum)
      fatal("sendconnecttcpquery: Could not scavenge a free socket!");
  }
    
  /* Since we know we now have a free s0cket, lets take it */

  assert(tqi->sockets[seq] == -1);
  tqi->sockets[seq] =  socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (tqi->sockets[seq] == -1) 
    fatal("Socket creation in sendconnecttcpquery");
  tqi->maxsd = MAX(tqi->maxsd, tqi->sockets[seq]);
  tqi->sockets_out++;
  unblock_socket(tqi->sockets[seq]);
  init_socket(tqi->sockets[seq]);

  bzero(&sock, sockaddr_in_len);
  sock.sin_family = AF_INET;
  sock.sin_port = htons(o.tcp_probe_port);
  sock.sin_addr.s_addr = target->host.s_addr;
  
  res = connect(tqi->sockets[seq],(struct sockaddr *)&sock,sizeof(struct sockaddr));
  gettimeofday(&time[seq], NULL);

  if ((res != -1 || errno == ECONNREFUSED)) {
    /* This can happen on localhost, successful/failing connection immediately
       in non-blocking mode */
      hostupdate(hostbatch, target, HOST_UP, 1, trynum, to, 
		 &time[seq], pt, tqi, pingstyle_connecttcp);
    if (tqi->maxsd == tqi->sockets[seq]) tqi->maxsd--;
  }
  else if (errno == ENETUNREACH) {
    if (o.debugging) 
      error("Got ENETUNREACH from sendconnecttcpquery connect()");
    hostupdate(hostbatch, target, HOST_DOWN, 1, trynum, to, 
	       &time[seq], pt, tqi, pingstyle_connecttcp);
  }
  else {
    /* We'll need to select() and wait it out */
    FD_SET(tqi->sockets[seq], &(tqi->fds_r));
    FD_SET(tqi->sockets[seq], &(tqi->fds_w));
    FD_SET(tqi->sockets[seq], &(tqi->fds_x));
  }
return 0;
}

int sendrawtcppingquery(int rawsd, struct hoststruct *target, int pingtype, 
			int seq, struct timeval *time, struct pingtune *pt) {
int trynum;
int myseq;
unsigned short sportbase;
unsigned long myack = get_random_uint();

if (o.magic_port_set) sportbase = o.magic_port;
else sportbase = o.magic_port + 20;
trynum = seq % pt->max_tries;

 myseq = (get_random_uint() << 19) + (seq << 3) + 3; /* Response better end in 011 or 100 */
 memcpy((char *)&(o.decoys[o.decoyturn]), (char *)&target->source_ip, sizeof(struct in_addr));
 if (pingtype & PINGTYPE_TCP_USE_SYN) {   
   send_tcp_raw_decoys( rawsd, &(target->host), sportbase + trynum, o.tcp_probe_port, myseq, myack, TH_SYN, 0, NULL, 0, o.extra_payload, 
			o.extra_payload_length);
 } else {
   send_tcp_raw_decoys( rawsd, &(target->host), sportbase + trynum, o.tcp_probe_port, myseq, myack, TH_ACK, 0, NULL, 0, o.extra_payload, 
			o.extra_payload_length);
 }

 gettimeofday(&time[seq], NULL);
 return 0;
}


int sendpingquery(int sd, int rawsd, struct hoststruct *target,  
		  int seq, unsigned short id, struct scanstats *ss, 
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
int icmplen;
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
   bzero(datastart, 12);
   datastart += 12;
   datalen -= 12;
   pingpkt.type = 13;
 }
 else fatal("sendpingquery: unknown pingtype: %d", pingtype);

 if (o.extra_payload_length > 0) {
   icmplen += MIN(datalen, o.extra_payload_length);
   bzero(datastart, MIN(datalen, o.extra_payload_length));
 }
/* Fill out the ping packet */

pingpkt.code = 0;
pingpkt.id = id;
pingpkt.seq = seq;
pingpkt.checksum = 0;
pingpkt.checksum = in_cksum((unsigned short *)ping, icmplen);

/* Now for our sock */
if (ptech.icmpscan) {
  bzero((char *)&sock, sizeof(struct sockaddr_in));
  sock.sin_family= AF_INET;
  sock.sin_addr = target->host;
  
  memcpy((char *) &(o.decoys[o.decoyturn]), (char *)&target->source_ip, sizeof(struct in_addr));
}

 for (decoy = 0; decoy < o.numdecoys; decoy++) {
   if (ptech.icmpscan && decoy == o.decoyturn) {
     /* FIXME: If EHOSTUNREACH (Windows does that) then we were
	probably unable to obtain an arp response from the machine.
	We should just considering the host down rather than ignoring
	the error */
     if ((res = sendto(sd,(char *) ping,icmplen,0,(struct sockaddr *)&sock,
		       sizeof(struct sockaddr))) != icmplen && 
		       errno != EHOSTUNREACH 
#ifdef WIN32
        // Windows (correctly) returns this if we scan an address that is
        // known to be nonsensical (e.g. myip & mysubnetmask)
	&& errno != WSAEADDRNOTAVAIL
#endif 
		       ) {
       fprintf(stderr, "sendto in sendpingquery returned %d (should be 8)!\n", res);
       perror("sendto");
     }
   } else {
     send_ip_raw( rawsd, &o.decoys[decoy], &(target->host), IPPROTO_ICMP, ping, icmplen);
   }
 }
 gettimeofday(&time[seq], NULL);
 return 0;
}

int get_connecttcpscan_results(struct tcpqueryinfo *tqi, 
			       struct hoststruct *hostbatch, 
			       struct timeval *time, struct pingtune *pt, 
			       struct timeout_info *to) {

int res, res2;
int tm;
struct timeval myto, start, end;
int hostindex;
int trynum, newstate = HOST_DOWN;
int seq;
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
	if (tqi->sockets[seq] >= 0) {
	  if (o.debugging > 1) {
	    if (FD_ISSET(tqi->sockets[seq], &(myfds_r))) {
	      log_write(LOG_STDOUT, "WRITE selected for machine %s\n", inet_ntoa(hostbatch[hostindex].host));  
	    }
	    if ( FD_ISSET(tqi->sockets[seq], &myfds_w)) {
	      log_write(LOG_STDOUT, "READ selected for machine %s\n", inet_ntoa(hostbatch[hostindex].host)); 
	    }
	    if  ( FD_ISSET(tqi->sockets[seq], &myfds_x)) {
	      log_write(LOG_STDOUT, "EXC selected for machine %s\n", inet_ntoa(hostbatch[hostindex].host));
	    }
	  }
	  if (FD_ISSET(tqi->sockets[seq], &myfds_r) || FD_ISSET(tqi->sockets[seq], &myfds_w) ||  FD_ISSET(tqi->sockets[seq], &myfds_x)) {
	    foundsomething = 0;
	    res2 = read(tqi->sockets[seq], buf, sizeof(buf));
	    if (res2 == -1) {
	      switch(errno) {
	      case ECONNREFUSED:
	      case EAGAIN:
#ifdef WIN32
//		  case WSAENOTCONN:	//	needed?  this fails around here on my system
#endif
		if (errno == EAGAIN && o.verbose) {
		  log_write(LOG_STDOUT, "Machine %s MIGHT actually be listening on probe port %d\n", inet_ntoa(hostbatch[hostindex].host), o.tcp_probe_port);
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
		snprintf (buf, sizeof(buf), "Strange read error from %s", inet_ntoa(hostbatch[hostindex].host));
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
			 inet_ntoa(hostbatch[hostindex].host), 
			 o.tcp_probe_port);
		else 
		  log_write(LOG_STDOUT, "Machine %s is actually LISTENING on probe port %d, banner: %s\n",
			 inet_ntoa(hostbatch[hostindex].host), 
			 o.tcp_probe_port, buf);
	      }
	    }
	    if (foundsomething) {
	      hostupdate(hostbatch, &hostbatch[hostindex], newstate, 1, trynum,
			 to,  &time[seq], pt, tqi, pingstyle_connecttcp);
	      /*	      break;*/
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
	if ( tqi->sockets[seq] >= 0) {
	  tqi->sockets_out--;
	  close(tqi->sockets[seq]);
	  tqi->sockets[seq] = -1;
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


int get_ping_results(int sd, pcap_t *pd, struct hoststruct *hostbatch, int pingtype, struct timeval *time,  struct pingtune *pt, struct timeout_info *to, int id, struct pingtech *ptech, struct scan_lists *ports) {
fd_set fd_r, fd_x;
struct timeval myto, tmpto, start, end;
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
struct ip *ip, *ip2;
int hostnum = -99999; /* This ought to crash us if it is used uninitialized */
int tm;
int dotimeout = 1;
int newstate = HOST_DOWN;
int foundsomething;
unsigned short newport;
int newportstate; /* Hack so that in some specific cases we can determine the 
		     state of a port and even skip the real scan */
int trynum = -999999;
enum pingstyle pingstyle = pingstyle_unknown;
int timeout = 0;
unsigned short sequence = 65534;
unsigned long tmpl;
unsigned short sportbase;


FD_ZERO(&fd_r);
FD_ZERO(&fd_x);

/* Decide on the timeout, based on whether we need to also watch for TCP stuff */
if (ptech->icmpscan && !ptech->rawtcpscan) {
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
    ip = (struct ip *) readip_pcap(pd, &bytes, to->timeout);
  } else {    
    FD_SET(sd, &fd_r);
    FD_SET(sd, &fd_x);
    res = select(sd+1, &fd_r, NULL, &fd_x, &tmpto);
    if (res == 0) break;
    bytes = read(sd,&response,sizeof(response));
    ip = (struct ip *) &(response);
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
    error("%d byte micro packet received in get_ping_results");
    continue;
  }  

  foundsomething = 0;
  dotimeout = 0;
  
  /* First check if it is ICMP or TCP */
  if (ip->ip_p == IPPROTO_ICMP) {    
    /* if it is our response */
    ping = (struct ppkt *) ((ip->ip_hl * 4) + (char *) ip);
    if (bytes < ip->ip_hl * 4 + 8U) {
      error("Supposed ping packet is only %d bytes long!", bytes);
      continue;
    }
    if  ( (ping->type == 0 || ping->type == 14 || ping->type == 18)
	  && !ping->code && ping->id == id) {
      hostnum = ping->seq / pt->max_tries;
      if (hostnum > pt->group_end) {
	if (o.debugging) 
	  error("Ping sequence %d leads to hostnum %d which is beyond the end of this group (%d)", ping->seq, hostnum, pt->group_end);
	continue;
      }
      if (!hostbatch[hostnum].source_ip.s_addr)
	hostbatch[hostnum].source_ip.s_addr = ip->ip_dst.s_addr;
      if (o.debugging) 
	log_write(LOG_STDOUT, "We got a ping packet back from %s: id = %d seq = %d checksum = %d\n", inet_ntoa(ip->ip_src), ping->id, ping->seq, ping->checksum);
      if (hostbatch[hostnum].host.s_addr == ip->ip_src.s_addr) {
	foundsomething = 1;
	pingstyle = pingstyle_icmp;
	sequence = ping->seq;
	newstate = HOST_UP;
	trynum = sequence % pt->max_tries;
	if (pt->discardtimesbefore < ping->seq)
	  dotimeout = 1;
	else dotimeout = 0;
      }
      else hostbatch[hostnum].wierd_responses++;
    }
    else if (ping->type == 3 || ping->type == 11 || ping->type == 4 || 
	     o.debugging) {
      if (bytes <  ip->ip_hl * 4 + 28U) {
	if (o.debugging)
	  error("ICMP type %d code %d packet is only %d bytes\n", ping->type, ping->code, bytes);
	continue;
      }

      ip2 = (struct ip *) ((char *)ip + ip->ip_hl * 4 + 8);
      if (bytes < ip->ip_hl * 4 + 8U + ip2->ip_hl * 4 + 8U) {
	if (o.debugging)
	  error("ICMP type %d code %d packet is only %d bytes\n", ping->type, ping->code, bytes);
	continue;
      }
      
      if (ip2->ip_p == IPPROTO_ICMP) {
	/* The response was based on a ping packet we sent */
	if (!ptech->icmpscan) {
	  if (o.debugging)
	    error("Got ICMP error referring to ICMP msg which we did not send");
	  continue;
	}
	ping2 = (struct ppkt *) ((char *)ip2 + ip2->ip_hl * 4);
	if (ping2->id != id) {
	  if (o.debugging) {	
	    error("Illegal id %d found, should be %d (icmp type/code %d/%d)", ping2->id, id, ping->type, ping->code);
	    if (o.debugging > 1)
	      lamont_hdump((unsigned char *)ip, bytes);
	  }
	  continue;
	}
	sequence = ping2->seq;
	hostnum = sequence / pt->max_tries;
	trynum = sequence % pt->max_tries;

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
	if (trynum >= pt->max_tries) {
	  if (o.debugging)
	    error("Bogus trynum %d", trynum);
	  continue;
	}

	/* Grab the sequence nr */
	tmpl = ntohl(tcp->th_seq);
	
	if ((tmpl & 7) == 3) {
	  sequence = (tmpl >> 3) & 0xffff;
	  hostnum = sequence / pt->max_tries;
	  trynum = sequence % pt->max_tries;
	} else {
	  if (o.debugging) {
	    error("Whacked seq number from %s", inet_ntoa(ip->ip_src));
	  }
	  continue;	
	}	
      } else {
	if (o.debugging)
	  error("Got ICMP response to a packet which was not ICMP or TCP");
	continue;
      }

      if (hostnum > pt->group_end) {
	if (o.debugging)
	  error("Bogus ping sequence: %d leads to bogus hostnum %d (icmp type/code %d/%d", sequence, hostnum, ping->type, ping->code);
	continue;
      }
        
      if (ping->type == 3) {
	if (o.debugging) 
	  log_write(LOG_STDOUT, "Got destination unreachable for %s\n", inet_ntoa(hostbatch[hostnum].host));
	/* Since this gives an idea of how long it takes to get an answer,
	   we add it into our times */
	if (pt->discardtimesbefore < sequence)
	  dotimeout = 1;	
	foundsomething = 1;
	pingstyle = pingstyle_icmp;
	newstate = HOST_DOWN;
	newportstate = PORT_FIREWALLED;
      } else if (ping->type == 11) {
	if (o.debugging) 
	  log_write(LOG_STDOUT, "Got Time Exceeded for %s\n", inet_ntoa(hostbatch[hostnum].host));
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
      /* Grab the sequence nr */
      if (pingtype & PINGTYPE_TCP_USE_SYN) {      
	if ((tmpl & 7) == 4 || (tmpl & 7) == 3) {
	  sequence = (tmpl >> 3) & 0xffff;
	  hostnum = sequence / pt->max_tries;
	  trynum = sequence % pt->max_tries;
	} else {
	  if (o.debugging) {
	    error("Whacked ACK number from %s", inet_ntoa(ip->ip_src));
	  }
	  continue;	
	}
      } else {
	trynum = ntohs(tcp->th_dport) - sportbase;
	if (trynum >= pt->max_tries) {
	  if (o.debugging)
	    error("Bogus trynum %d", trynum);
	  continue;
	}
	/* FUDGE!  This ACK scan is cool but we don't get sequence numbers
	   back! We'll have to brute force lookup to find the hostnum */
	for(hostnum = pt->group_end; hostnum >= 0; hostnum--) {
	  if (hostbatch[hostnum].host.s_addr == ip->ip_src.s_addr)
	    break;
	}
	if (hostnum < 0) {	
	  if (o.debugging > 1) 
	    error("Warning, unexpacted packet from machine %s", inet_ntoa(ip->ip_src));
	  continue;
	}	
	sequence = hostnum * pt->max_tries + trynum;
      }
      if (hostnum > pt->group_end) {
	if (o.debugging) {
	  error("Response from host beyond group_end");
	}
	continue;
      }
      if (o.debugging) 
	log_write(LOG_STDOUT, "We got a TCP ping packet back from %s (hostnum = %d trynum = %d\n", inet_ntoa(ip->ip_src), hostnum, trynum);
      pingstyle = pingstyle_rawtcp;
      foundsomething = 1;
      if (pt->discardtimesbefore < sequence)
	dotimeout = 1;
      newstate = HOST_UP;

      if (pingtype & PINGTYPE_TCP_USE_SYN) {
	if (tcp->th_flags & TH_RST) {
	  newportstate = PORT_CLOSED;
	} else if ((tcp->th_flags & (TH_SYN|TH_ACK)) == (TH_SYN|TH_ACK)) {
	  newportstate = PORT_OPEN;
	}
      }
    } else if (o.debugging) {
      error("Found whacked packet protocol %d in get_ping_results", ip->ip_p);
    }
    if (foundsomething) {  
      hostupdate(hostbatch, &hostbatch[hostnum], newstate, dotimeout, 
		 trynum, to, &time[sequence], pt, NULL,pingstyle);
    }
    if (newport && newportstate != PORT_UNKNOWN) {
      /* OK, we can add it, but that is only appropriate if this is one
	 of the ports the user ASKED for */
      if (ports && ports->tcp_count == 1 && ports->tcp_ports[0] == newport)
	addport(&(hostbatch[hostnum].ports), newport, IPPROTO_TCP, NULL, 
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



