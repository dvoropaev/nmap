
/***************************************************************************
 * NmapOps.cc -- The NmapOps class contains global options, mostly based   *
 * on user-provided command-line settings.                                 *
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

/* $Id: NmapOps.cc,v 1.21 2003/09/16 06:04:55 fyodor Exp $ */
#include "nmap.h"
#include "nbase.h"
#include "NmapOps.h"

NmapOps o;

NmapOps::NmapOps() {
  datadir = NULL;
  Initialize();
}

NmapOps::~NmapOps() {
  if (datadir) free(datadir);
}

void NmapOps::ReInit() {
  Initialize();
}

// no setpf() because it is based on setaf() values
int NmapOps::pf() {
  return (af() == AF_INET)? PF_INET : PF_INET6;
}

int NmapOps::SourceSockAddr(struct sockaddr_storage *ss, size_t *ss_len) {
  if (sourcesocklen <= 0)
    return 1;
  assert(sourcesocklen <= sizeof(*ss));
  if (ss)
    memcpy(ss, &sourcesock, sourcesocklen);
  if (ss_len)
    *ss_len = sourcesocklen;
  return 0;
}

/* Note that it is OK to pass in a sockaddr_in or sockaddr_in6 casted
     to sockaddr_storage */
void NmapOps::setSourceSockAddr(struct sockaddr_storage *ss, size_t ss_len) {
  assert(ss_len > 0 && ss_len <= sizeof(*ss));
  memcpy(&sourcesock, ss, ss_len);
  sourcesocklen = ss_len;
}

struct in_addr NmapOps::v4source() {
 const struct in_addr *addy = v4sourceip();
  struct in_addr in;
  if (addy) return *addy;
  in.s_addr = 0;
  return in;
}

const struct in_addr *NmapOps::v4sourceip() {
   struct sockaddr_in *sin = (struct sockaddr_in *) &sourcesock;
  if (sin->sin_family == AF_INET) {
    return &(sin->sin_addr);
  }
  return NULL;
}

// Number of milliseconds since getStartTime().  The current time is an
// optional argument to avoid an extre gettimeofday() call.
int NmapOps::TimeSinceStartMS(struct timeval *now) {
  struct timeval tv;
  if (!now)
    gettimeofday(&tv, NULL);
  else tv = *now;

  return TIMEVAL_MSEC_SUBTRACT(tv, start_time);
}

void NmapOps::Initialize() {
  setaf(AF_INET);
#ifndef WIN32
# ifdef __amigaos__
    isr00t = 1;
# else
    isr00t = !(geteuid());
# endif // __amigaos__
#else
  isr00t = 1;
  winip_init();	/* wrapper for all win32 initialization */
#endif
  debugging = DEBUGGING;
  verbose = DEBUGGING;
  randomize_hosts = 0;
  spoofsource = 0;
  device[0] = '\0';
  interactivemode = 0;
  host_group_sz = HOST_GROUP_SZ;
  generate_random_ips = 0;
  reference_FPs = NULL;
  magic_port = 33000 + (get_random_uint() % 31000);
  magic_port_set = 0;
  num_ping_synprobes = num_ping_ackprobes = num_ping_udpprobes = 0;
  timing_level = 3;
  max_parallelism = 0;
  min_parallelism = 0;
  max_rtt_timeout = MAX_RTT_TIMEOUT;
  min_rtt_timeout = MIN_RTT_TIMEOUT;
  initial_rtt_timeout = INITIAL_RTT_TIMEOUT;
  max_ips_to_scan = 0;
  extra_payload_length = 0;
  extra_payload = NULL;
  host_timeout = HOST_TIMEOUT;
  scan_delay = 0;
  scanflags = -1;
  resume_ip.s_addr = 0;
  osscan_limit = 0;
  osscan_guess = 0;
  numdecoys = 0;
  decoyturn = -1;
  identscan = 0;
  osscan = 0;
  servicescan = 0;
  pingtype = PINGTYPE_UNKNOWN;
  listscan = pingscan = allowall = ackscan = bouncescan = connectscan = 0;
  rpcscan = nullscan = xmasscan = fragscan = synscan = windowscan = 0;
  maimonscan = idlescan = finscan = udpscan = ipprotscan = noresolve = 0;
  force = append_output = 0;
  memset(logfd, 0, sizeof(FILE *) * LOG_TYPES);
  ttl = -1;
  nmap_stdout = stdout;
  gettimeofday(&start_time, NULL);
  pTrace = vTrace = false;
  if (datadir) free(datadir);
  datadir = NULL;
}

bool NmapOps::TCPScan() {
  return ackscan|bouncescan|connectscan|finscan|idlescan|maimonscan|nullscan|synscan|windowscan|xmasscan;
}

bool NmapOps::UDPScan() {
  return udpscan;
}


void NmapOps::ValidateOptions() {

  if (pingtype == PINGTYPE_UNKNOWN) {
    if (isr00t && af() == AF_INET) pingtype = PINGTYPE_TCP|PINGTYPE_TCP_USE_ACK|PINGTYPE_ICMP_PING;
    else pingtype = PINGTYPE_TCP; // if nonr00t or IPv6
    num_ping_ackprobes = 1;
    ping_ackprobes[0] = DEFAULT_TCP_PROBE_PORT;
  }

  /* Insure that at least one scantype is selected */
  if (TCPScan() + UDPScan() + ipprotscan + listscan + pingscan == 0) {
    if (isr00t && af() == AF_INET)
      synscan++;
    else connectscan++;
    //    if (verbose) error("No tcp, udp, or ICMP scantype specified, assuming %s scan. Use -sP if you really don't want to portscan (and just want to see what hosts are up).", synscan? "SYN Stealth" : "vanilla tcp connect()");
  }

  if (pingtype != PINGTYPE_NONE && spoofsource) {
    error("WARNING:  If -S is being used to fake your source address, you may also have to use -e <iface> and -P0 .  If you are using it to specify your real source address, you can ignore this warning.");
  }

  if (pingtype != PINGTYPE_NONE && idlescan) {
    error("WARNING: Many people use -P0 w/Idlescan to prevent pings from their true IP.  On the other hand, timing info Nmap gains from pings can allow for faster, more reliable scans.");
    sleep(2); /* Give ppl a chance for ^C :) */
  }

 if (numdecoys > 1 && idlescan) {
    error("WARNING: Your decoys won't be used in the Idlescan portion of your scanning (although all packets sent to the target are spoofed anyway");
  }

 if (connectscan && spoofsource) {
    error("WARNING:  -S will only affect the source address used in a connect() scan if you specify one of your own addresses.  Use -sS or another raw scan if you want to completely spoof your source address, but then you need to know what you're doing to obtain meaningful results.");
  }

 if ((pingtype & PINGTYPE_UDP) && (!o.isr00t || o.af() != AF_INET)) {
   fatal("Sorry, UDP Ping (-PU) only works if you are root (because we need to read raw responses off the wire) and only for IPv4 (cause fyodor is too lazy right now to add IPv6 support and nobody has sent a patch)");
 }

 if ((pingtype & PINGTYPE_TCP) && (!o.isr00t || o.af() != AF_INET)) {
   /* We will have to do a connect() style ping */
   if (num_ping_synprobes && num_ping_ackprobes) {
     fatal("Cannot use both SYN and ACK ping probes if you are nonroot or using IPv6");
   }

   if (num_ping_ackprobes > 0) { 
     memcpy(ping_synprobes, ping_ackprobes, num_ping_ackprobes * sizeof(*ping_synprobes));
     num_ping_synprobes = num_ping_ackprobes;
     num_ping_ackprobes = 0;
   }
 }

 if (ipprotscan + (TCPScan() || UDPScan()) + listscan + pingscan > 1) {
   fatal("Sorry, the IPProtoscan, Listscan, and Pingscan (-sO, -sL, -sP) must currently be used alone rathre than combined with other scan types.");
 }

 if ((pingscan && pingtype == PINGTYPE_NONE)) {
    fatal("-P0 (skip ping) is incompatable with -sP (ping scan).  If you only want to enumerate hosts, try list scan (-sL)");
  }

 if (pingscan && (TCPScan() || UDPScan() || ipprotscan || listscan)) {
   fatal("Ping scan is not valid with any other scan types (the other ones all include a ping scan");
 }

/* We start with stuff users should not do if they are not root */
  if (!isr00t) {

#ifndef WIN32	/*	Win32 has perfectly fine ICMP socket support */
    if (pingtype & (PINGTYPE_ICMP_PING|PINGTYPE_ICMP_MASK|PINGTYPE_ICMP_TS)) {
      error("Warning:  You are not root -- using TCP pingscan rather than ICMP");
      pingtype = PINGTYPE_TCP;
      if (num_ping_synprobes == 0)
	{
	  num_ping_synprobes = 1;
	  ping_synprobes[0] = DEFAULT_TCP_PROBE_PORT;
	}
    }
#endif
    
    if (ackscan|finscan|idlescan|ipprotscan|maimonscan|nullscan|synscan|udpscan|windowscan|xmasscan) {
#ifndef WIN32
      fatal("You requested a scan type which requires r00t privileges, and you do not have them.\n");
#else
      winip_barf(0);
#endif
    }
    
    if (numdecoys > 0) {
#ifndef WIN32
      fatal("Sorry, but you've got to be r00t to use decoys, boy!");
#else
      winip_barf(0);
#endif
    }
    
    if (fragscan) {
#ifndef WIN32
      fatal("Sorry, but fragscan requires r00t privileges\n");
#else
      winip_barf(0);
#endif
    }
    
    if (osscan) {
#ifndef WIN32
      fatal("TCP/IP fingerprinting (for OS scan) requires root privileges which you do not appear to possess.  Sorry, dude.\n");
#else
      winip_barf(0);
#endif
    }
  }
  
  
  if (numdecoys > 0 && rpcscan) {
    error("WARNING:  RPC scan currently does not make use of decoys so don't count on that protection");
  }
  
  if (bouncescan && pingtype != PINGTYPE_NONE) 
    log_write(LOG_STDOUT, "Hint: if your bounce scan target hosts aren't reachable from here, remember to use -P0 so we don't try and ping them prior to the scan\n");
  
  if (ackscan+bouncescan+connectscan+finscan+idlescan+maimonscan+nullscan+synscan+windowscan+xmasscan > 1)
    fatal("You specified more than one type of TCP scan.  Please choose only one of -sA, -b, -sT, -sF, -sI, -sM, -sN, -sS, -sW, and -sX");
  
  if (numdecoys > 0 && (bouncescan || connectscan)) {
    error("WARNING: Decoys are irrelevant to the bounce or connect scans");
  }
  
  if (fragscan && !(ackscan|finscan|maimonscan|nullscan|synscan|windowscan|xmasscan)) {
    fatal("Fragscan only works with ACK, FIN, Maimon, NULL, SYN, Window, and XMAS scan types");
  }
  
  if (identscan && !connectscan) {
    error("Identscan only works with connect scan (-sT) ... ignoring option");
    identscan = 0;
  }
  
  if (osscan && bouncescan)
    error("Combining bounce scan with OS scan seems silly, but I will let you do whatever you want!");
  
#if !defined(LINUX) && !defined(OPENBSD) && !defined(FREEBSD) && !defined(NETBSD)
  if (fragscan) {
    fprintf(stderr, "Warning: Packet fragmentation selected on a host other than Linux, OpenBSD, FreeBSD, or NetBSD.  This may or may not work.\n");
  }
#endif
  
  if (osscan && pingscan) {
    fatal("WARNING:  OS Scan is unreliable with a ping scan.  You need to use a scan type along with it, such as -sS, -sT, -sF, etc instead of -sP");
  }
  
  if (resume_ip.s_addr && generate_random_ips)
    resume_ip.s_addr = 0;
  
  if (magic_port_set && connectscan) {
    error("WARNING:  -g is incompatible with the default connect() scan (-sT).  Use a raw scan such as -sS if you want to set the source port.");
  }

  if (max_parallelism && min_parallelism && (min_parallelism > max_parallelism)) {
    fatal("--min_parallelism must be less than or equal to --max_parallelism");
  }
  
  if (af() == AF_INET6 && (numdecoys|osscan|bouncescan|fragscan|ackscan|finscan|idlescan|ipprotscan|maimonscan|nullscan|rpcscan|synscan|udpscan|windowscan|xmasscan)) {
    fatal("Sorry -- IPv6 support is currently only available for connect() scan (-sT), ping scan (-sP), and list scan (-sL).  Further support is under consideration.");
  }
}
  
void NmapOps::setMaxRttTimeout(int rtt) 
{ 
  if (rtt <= 0) fatal("NmapOps::setMaxRttTimeout(): maximum round trip time must be greater than 0");
  max_rtt_timeout = rtt; 
  if (rtt < min_rtt_timeout) min_rtt_timeout = rtt; 
  if (rtt < initial_rtt_timeout) initial_rtt_timeout = rtt;
}

void NmapOps::setMinRttTimeout(int rtt) 
{ 
  if (rtt < 0) fatal("NmapOps::setMaxRttTimeout(): minimum round trip time must be at least 0");
  min_rtt_timeout = rtt; 
  if (rtt > max_rtt_timeout) max_rtt_timeout = rtt;  
  if (rtt > initial_rtt_timeout) initial_rtt_timeout = rtt;
}

void NmapOps::setInitialRttTimeout(int rtt) 
{ 
  if (rtt <= 0) fatal("NmapOps::setMaxRttTimeout(): initial round trip time must be greater than 0");
  initial_rtt_timeout = rtt; 
  if (rtt > max_rtt_timeout) max_rtt_timeout = rtt;  
  if (rtt < min_rtt_timeout) min_rtt_timeout = rtt;
}