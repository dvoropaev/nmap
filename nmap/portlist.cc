
/***************************************************************************
 * portlist.cc -- Functions for manipulating various lists of ports        *
 * maintained internally by Nmap.                                          *
 *                                                                         *
 ***********************IMPORTANT NMAP LICENSE TERMS************************
 *                                                                         *
 * The Nmap Security Scanner is (C) 1996-2004 Insecure.Com LLC. Nmap       *
 * is also a registered trademark of Insecure.Com LLC.  This program is    *
 * free software; you may redistribute and/or modify it under the          *
 * terms of the GNU General Public License as published by the Free        *
 * Software Foundation; Version 2.  This guarantees your right to use,     *
 * modify, and redistribute this software under certain conditions.  If    *
 * you wish to embed Nmap technology into proprietary software, we may be  *
 * willing to sell alternative licenses (contact sales@insecure.com).      *
 * Many security scanner vendors already license Nmap technology such as  *
 * our remote OS fingerprinting database and code, service/version         *
 * detection system, and port scanning code.                               *
 *                                                                         *
 * Note that the GPL places important restrictions on "derived works", yet *
 * it does not provide a detailed definition of that term.  To avoid       *
 * misunderstandings, we consider an application to constitute a           *
 * "derivative work" for the purpose of this license if it does any of the *
 * following:                                                              *
 * o Integrates source code from Nmap                                      *
 * o Reads or includes Nmap copyrighted data files, such as                *
 *   nmap-os-fingerprints or nmap-service-probes.                          *
 * o Executes Nmap and parses the results (as opposed to typical shell or  *
 *   execution-menu apps, which simply display raw Nmap output and so are  *
 *   not derivative works.)                                                * 
 * o Integrates/includes/aggregates Nmap into a proprietary executable     *
 *   installer, such as those produced by InstallShield.                   *
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
 * As a special exception to the GPL terms, Insecure.Com LLC grants        *
 * permission to link the code of this program with any version of the     *
 * OpenSSL library which is distributed under a license identical to that  *
 * listed in the included Copying.OpenSSL file, and distribute linked      *
 * combinations including the two. You must obey the GNU GPL in all        *
 * respects for all of the code used other than OpenSSL.  If you modify    *
 * this file, you may extend this exception to your version of the file,   *
 * but you are not obligated to do so.                                     *
 *                                                                         *
 * If you received these files with a written license agreement or         *
 * contract stating terms other than the terms above, then that            *
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
 * http://www.gnu.org/copyleft/gpl.html , or in the COPYING file included  *
 * with Nmap.                                                              *
 *                                                                         *
 ***************************************************************************/

/* $Id: portlist.cc 2964 2005-12-05 01:00:03Z fyodor $ */


#include "portlist.h"
#include "nmap_error.h"
#include "nmap.h"
#include "NmapOps.h"

using namespace std;

#if HAVE_STRINGS_H
#include <strings.h>
#endif /* HAVE_STRINGS_H */

extern NmapOps o;  /* option structure */

Port::Port() {
  portno = proto = 0;
  owner = NULL;
  rpc_status = RPC_STATUS_UNTESTED;
  rpc_program = rpc_lowver = rpc_highver = 0;
  state = confidence = 0;
  next = NULL;
  serviceprobe_results = PROBESTATE_INITIAL;
  serviceprobe_service = NULL;
  serviceprobe_product = serviceprobe_version = serviceprobe_extrainfo = NULL;
  serviceprobe_hostname = serviceprobe_ostype = serviceprobe_devicetype = NULL;
  serviceprobe_tunnel = SERVICE_TUNNEL_NONE;
  serviceprobe_fp = NULL;
}

Port::~Port() {
 if (owner)
   free(owner);
 if (serviceprobe_product)
   free(serviceprobe_product);
 if (serviceprobe_version)
   free(serviceprobe_version);
 if (serviceprobe_extrainfo)
   free(serviceprobe_extrainfo);
 if (serviceprobe_hostname)
   free(serviceprobe_hostname);
 if (serviceprobe_ostype)
   free(serviceprobe_ostype);
 if (serviceprobe_devicetype)
   free(serviceprobe_devicetype);
 if (serviceprobe_service)
   free(serviceprobe_service);
 if (serviceprobe_fp)
   free(serviceprobe_fp);
}

// Uses the sd->{product,version,extrainfo} if available to fill
// out sd->fullversion.  If unavailable, it will be set to zero length.
static void populateFullVersionString(struct serviceDeductions *sd) {
  char *dst = sd->fullversion;
  unsigned int spaceleft = sizeof(sd->fullversion) - 1;

  dst[0] = '\0';

  if (sd->product && spaceleft >= strlen(sd->product)) {
    strncat(dst, sd->product, spaceleft);
    spaceleft -= strlen(sd->product);
  }

  if (sd->version && spaceleft >= (strlen(sd->version) + 1)) {
    strncat(dst, " ", spaceleft);
    strncat(dst, sd->version, spaceleft);
    spaceleft -= strlen(sd->version) + 1;
  }

  if (sd->extrainfo && spaceleft >= (strlen(sd->extrainfo) + 3)) {
    strncat(dst, " (", spaceleft);
    strncat(dst, sd->extrainfo, spaceleft);
    strncat(dst, ")", spaceleft);
    spaceleft -= strlen(sd->extrainfo) + 3;
  }

}


// pass in an allocated struct serviceDeductions (don't worry about
// initializing, and you don't have to free any internal ptrs.  See the
// serviceDeductions definition for the fields that are populated.
// Returns 0 if at least a name is available.
int Port::getServiceDeductions(struct serviceDeductions *sd) {
  struct servent *service;

  assert(sd);
  memset(sd, 0, sizeof(struct serviceDeductions));
  sd->service_fp = serviceprobe_fp;
  sd->service_tunnel = serviceprobe_tunnel;
  sd->rpc_status = rpc_status;
  sd->rpc_program = rpc_program;
  sd->rpc_lowver = rpc_lowver;
  sd->rpc_highver = rpc_highver;

  // First priority is RPC
  if (rpc_status == RPC_STATUS_UNKNOWN || rpc_status == RPC_STATUS_GOOD_PROG ) {
    assert(serviceprobe_service);
    sd->name = serviceprobe_service;
    sd->name_confidence = (rpc_status == RPC_STATUS_UNKNOWN)? 8 : 10;
    sd->dtype = SERVICE_DETECTION_PROBED; // RPC counts as probed
    sd->version = serviceprobe_version;
    sd->extrainfo = serviceprobe_extrainfo;
    sd->hostname = serviceprobe_hostname;
    sd->ostype = serviceprobe_ostype;
    sd->devicetype = serviceprobe_devicetype;
    populateFullVersionString(sd);
    return 0;
  } else if (serviceprobe_results == PROBESTATE_FINISHED_HARDMATCHED
	     || serviceprobe_results == PROBESTATE_FINISHED_SOFTMATCHED) {
    assert(serviceprobe_service);
    sd->dtype = SERVICE_DETECTION_PROBED;
    sd->name = serviceprobe_service;
    sd->name_confidence = 10;
    sd->product = serviceprobe_product;
    sd->version = serviceprobe_version;
    sd->extrainfo = serviceprobe_extrainfo;
    sd->hostname = serviceprobe_hostname;
    sd->ostype = serviceprobe_ostype;
    sd->devicetype = serviceprobe_devicetype;
    populateFullVersionString(sd);
    return 0;
  } else if (serviceprobe_results == PROBESTATE_EXCLUDED) {
    service = nmap_getservbyport(htons(portno), (proto == IPPROTO_TCP)? "tcp" : "udp");

    if (service) sd->name = service->s_name;

    sd->name_confidence = 2;  // Since we didn't even check it, we aren't very confident
    sd->dtype = SERVICE_DETECTION_TABLE;
    sd->product = serviceprobe_product;  // Should have a string that says port was excluded
    populateFullVersionString(sd);
    return 0;
  } else if (serviceprobe_results == PROBESTATE_FINISHED_TCPWRAPPED) {
    sd->dtype = SERVICE_DETECTION_PROBED;
    sd->name = "tcpwrapped";
    sd->name_confidence = 8;
    return 0;
  }

  // So much for service detection or RPC.  Maybe we can find it in the file
  service = nmap_getservbyport(htons(portno), (proto == IPPROTO_TCP)? "tcp" : "udp");
  if (service) {
    sd->dtype = SERVICE_DETECTION_TABLE;
    sd->name = service->s_name;
    sd->name_confidence = 3;
    return 0;
  }
  
  // Couldn't find it.  [shrug]
  return -1;

}


// sname should be NULL if sres is not
// PROBESTATE_FINISHED_MATCHED. product,version, and/or extrainfo
// will be NULL if unavailable. Note that this function makes its
// own copy of sname and product/version/extrainfo.  This function
// also takes care of truncating the version strings to a
// 'reasonable' length if neccessary, and cleaning up any unprintable
// chars. (these tests are to avoid annoying DOS (or other) attacks
// by malicious services).  The fingerprint should be NULL unless
// one is available and the user should submit it.  tunnel must be
// SERVICE_TUNNEL_NULL (normal) or SERVICE_TUNNEL_SSL (means ssl was
// detected and we tried to tunnel through it ).
void Port::setServiceProbeResults(enum serviceprobestate sres, 
				  const char *sname,	
				  enum service_tunnel_type tunnel, 
				  const char *product, const char *version, 
				  const char *extrainfo, const char *hostname,
				  const char *ostype, const char *devicetype,
				  const char *fingerprint) {

  int slen;
  serviceprobe_results = sres;
  unsigned char *p;
  serviceprobe_tunnel = tunnel;
  if (sname) serviceprobe_service = strdup(sname);
  if (fingerprint) serviceprobe_fp = strdup(fingerprint);

  if (product) {
    slen = strlen(product);
    if (slen > 64) slen = 64;
    serviceprobe_product = (char *) safe_malloc(slen + 1);
    memcpy(serviceprobe_product, product, slen);
    serviceprobe_product[slen] = '\0';
    p = (unsigned char *) serviceprobe_product;
    while(*p) {
      if (!isprint((int)*p)) *p = '.';
      p++;
    }
  }

  if (version) {
    slen = strlen(version);
    if (slen > 64) slen = 64;
    serviceprobe_version = (char *) safe_malloc(slen + 1);
    memcpy(serviceprobe_version, version, slen);
    serviceprobe_version[slen] = '\0';
    p = (unsigned char *) serviceprobe_version;
    while(*p) {
      if (!isprint((int)*p)) *p = '.';
      p++;
    }
  }

  if (extrainfo) {
    slen = strlen(extrainfo);
    if (slen > 128) slen = 128;
    serviceprobe_extrainfo = (char *) safe_malloc(slen + 1);
    memcpy(serviceprobe_extrainfo, extrainfo, slen);
    serviceprobe_extrainfo[slen] = '\0';
    p = (unsigned char *) serviceprobe_extrainfo;
    while(*p) {
      if (!isprint((int)*p)) *p = '.';
      p++;
    }
  }

  if (hostname) {
    slen = strlen(hostname);
    if (slen > 64) slen = 64;
    serviceprobe_hostname = (char *) safe_malloc(slen + 1);
    memcpy(serviceprobe_hostname, hostname, slen);
    serviceprobe_hostname[slen] = '\0';
    p = (unsigned char *) serviceprobe_hostname;
    while(*p) {
      if (!isprint((int)*p)) *p = '.';
      p++;
    }
  }

  if (ostype) {
    slen = strlen(ostype);
    if (slen > 64) slen = 64;
    serviceprobe_ostype = (char *) safe_malloc(slen + 1);
    memcpy(serviceprobe_ostype, ostype, slen);
    serviceprobe_ostype[slen] = '\0';
    p = (unsigned char *) serviceprobe_ostype;
    while(*p) {
      if (!isprint((int)*p)) *p = '.';
      p++;
    }
  }
  
  if (devicetype) {
    slen = strlen(devicetype);
    if (slen > 64) slen = 64;
    serviceprobe_devicetype = (char *) safe_malloc(slen + 1);
    memcpy(serviceprobe_devicetype, devicetype, slen);
    serviceprobe_devicetype[slen] = '\0';
    p = (unsigned char *) serviceprobe_devicetype;
    while(*p) {
      if (!isprint((int)*p)) *p = '.';
      p++;
    }
  }

}

/* Sets the results of an RPC scan.  if rpc_status is not
   RPC_STATUS_GOOD_PROGRAM, pass 0 for the other args.  This function
   takes care of setting the port's service and version appropriately. */
void Port::setRPCProbeResults(int rpcs, unsigned long rpcp, 
			unsigned int rpcl, unsigned int rpch) {
  rpc_status = rpcs;
  const char *newsvc;
  char verbuf[128];

  rpc_status = rpcs;
  if (rpc_status == RPC_STATUS_GOOD_PROG) {
    rpc_program = rpcp;
    rpc_lowver = rpcl;
    rpc_highver = rpch;

    // Now set the service/version info
    newsvc = nmap_getrpcnamebynum(rpcp);
    if (!newsvc) newsvc = "rpc.unknownprog"; // should never happen
    if (serviceprobe_service)
      free(serviceprobe_service);
    serviceprobe_service = strdup(newsvc);
    serviceprobe_product = strdup(newsvc);
    if (rpc_lowver == rpc_highver)
      snprintf(verbuf, sizeof(verbuf), "%i", rpc_lowver);
    else
      snprintf(verbuf, sizeof(verbuf), "%i-%i", rpc_lowver, rpc_highver);
    serviceprobe_version = strdup(verbuf);
    snprintf(verbuf, sizeof(verbuf), "rpc #%li", rpc_program);
    serviceprobe_extrainfo = strdup(verbuf);
  } else if (rpc_status == RPC_STATUS_UNKNOWN) {
    if (serviceprobe_service)
      free(serviceprobe_service);
    
    serviceprobe_service = strdup("rpc.unknown");
  }
}

PortList::PortList() {
  memset(state_counts, 0, sizeof(state_counts));
  memset(state_counts_udp, 0, sizeof(state_counts_udp));
  memset(state_counts_tcp, 0, sizeof(state_counts_tcp));
  memset(state_counts_ip, 0, sizeof(state_counts_ip));
  numports = 0;
  idstr = NULL;
}

PortList::~PortList() {

  for(map<u16,Port*>::iterator iter = tcp_ports.begin(); iter != tcp_ports.end(); iter++)
     {
        delete iter->second;
     }
  

  for(map<u16,Port*>::iterator iter = udp_ports.begin(); iter != udp_ports.end(); iter++)
     {
        delete iter->second;
     }

  for(map<u16,Port*>::iterator iter = ip_prots.begin(); iter != ip_prots.end(); iter++)
     {
        delete iter->second;
     }

  if (idstr) { 
    free(idstr);
    idstr = NULL;
  }

}


int PortList::addPort(u16 portno, u8 protocol, char *owner, int state) {
  Port *current = NULL;
  map < u16, Port* > *portarray = NULL;  // This has to be a pointer so that we change the original and not a copy
  map <u16, Port *>::iterator pt;
  char msg[128];

  assert(state < PORT_HIGHEST_STATE);

  if ((state == PORT_OPEN && o.verbose) || (o.debugging > 1)) {
    if (owner && *owner) {
      snprintf(msg, sizeof(msg), " (owner: %s)", owner);
    } else msg[0] = '\0';
    
    log_write(LOG_STDOUT, "Discovered %s port %hu/%s%s%s\n",
	      statenum2str(state), portno, 
	      proto2ascii(protocol), msg, idstr? idstr : "");
    log_flush(LOG_STDOUT);
  }


/* Make sure state is OK */
  if (state != PORT_OPEN && state != PORT_CLOSED && state != PORT_FILTERED &&
      state != PORT_UNFILTERED && state != PORT_OPENFILTERED && 
      state != PORT_CLOSEDFILTERED)
    fatal("addPort: attempt to add port number %d with illegal state %d\n", portno, state);

  if (protocol == IPPROTO_TCP) {
    portarray = &tcp_ports;
  } else if (protocol == IPPROTO_UDP) {
    portarray = &udp_ports;
  } else if (protocol == IPPROTO_IP) {
    assert(portno < 256);
    portarray = &ip_prots;
  } else fatal("addPort: attempted port insertion with invalid protocol");

  pt = portarray->find(portno);
  if (pt != portarray->end()) {
    /* We must discount our statistics from the old values.  Also warn
       if a complete duplicate */
    current = pt->second;
    if (o.debugging && current->state == state && (!owner || !*owner)) {
      error("Duplicate port (%hu/%s)\n", portno, proto2ascii(protocol));
    } 
    state_counts[current->state]--;
    if (current->proto == IPPROTO_TCP) {
      state_counts_tcp[current->state]--;
    } else if (current->proto == IPPROTO_UDP) {
      state_counts_udp[current->state]--;
    } else
      state_counts_ip[current->state]--;
  } else {
    current = new Port();
    (*portarray)[portno] = current;
    numports++;
    current->portno = portno;
  }
  
  state_counts[state]++;
  current->state = state;
  if (protocol == IPPROTO_TCP) {
    state_counts_tcp[state]++;
  } else if (protocol == IPPROTO_UDP) {
    state_counts_udp[state]++;
  } else
    state_counts_ip[state]++;
  current->proto = protocol;

  if (owner && *owner) {
    if (current->owner)
      free(current->owner);
    current->owner = strdup(owner);
  }
  
  return 0; /*success */
}

int PortList::removePort(u16 portno, u8 protocol) {
  Port *answer = NULL;

  printf("Removed %d\n", portno);

  if (protocol == IPPROTO_TCP) {
   answer = tcp_ports[portno];
   tcp_ports.erase(portno);
  }

  if (protocol == IPPROTO_UDP) {  
    answer = udp_ports[portno];
    udp_ports.erase(portno);
  } else if (protocol == IPPROTO_IP) {
    answer = ip_prots[portno];
    ip_prots.erase(portno);
  }

  if (!answer)
    return -1;

  if (o.verbose) {  
    log_write(LOG_STDOUT, "Deleting port %hu/%s, which we thought was %s\n",
	      portno, proto2ascii(answer->proto),
	      statenum2str(answer->state));
    log_flush(LOG_STDOUT);
  }    

  delete answer;
  return 0;
}

  /* Saves an identification string for the target containing these
     ports (an IP address might be a good example, but set what you
     want).  Only used when printing new port updates.  Optional.  A
     copy is made. */
void PortList::setIdStr(const char *id) {
  int len = 0;
  if (idstr) free(idstr);
  if (!id) { idstr = NULL; return; }
  len = strlen(id);
  len += 5; // " on " + \0
  idstr = (char *) safe_malloc(len);
  snprintf(idstr, len, " on %s", id);
}

Port *PortList::lookupPort(u16 portno, u8 protocol) {
  map <u16, Port *>::iterator pt;
  if (protocol == IPPROTO_TCP) {
    pt = tcp_ports.find(portno);
    if (pt != tcp_ports.end())
      return pt->second;
  }

  else if (protocol == IPPROTO_UDP) {
    pt = udp_ports.find(portno);
    if (pt != udp_ports.end())
      return pt->second;
  }

  else if (protocol == IPPROTO_IP) {
    pt = ip_prots.find(portno);
    if (pt != ip_prots.end())
      return pt->second;
  }

  return NULL;
}

int PortList::getIgnoredPortState() {
  int ignored = PORT_UNKNOWN;
  int ignoredNum = 0;
  int i;
  for(i=0; i < PORT_HIGHEST_STATE; i++) {
    if (i == PORT_OPEN || i == PORT_UNKNOWN || i == PORT_TESTING || 
	i == PORT_FRESH) continue; /* Cannot be ignored */
    if (state_counts[i] > ignoredNum) {
      ignored = i;
      ignoredNum = state_counts[i];
    }
  }

  if (state_counts[ignored] < 15)
    ignored = PORT_UNKNOWN;

  return ignored;
}

/* A function for iterating through the ports.  Give NULL for the
   first "afterthisport".  Then supply the most recent returned port
   for each subsequent call.  When no more matching ports remain, NULL
   will be returned.  To restrict returned ports to just one protocol,
   specify IPPROTO_TCP or IPPROTO_UDP for allowed_protocol.  A 0 for
   allowed_protocol matches either.  allowed_state works in the same
   fashion as allowed_protocol. This function returns ports in numeric
   order from lowest to highest, except that if you ask for both TCP &
   UDP, every TCP port will be returned before we start returning UDP
   ports.  */

Port *PortList::nextPort(Port *afterthisport, 
			 u8 allowed_protocol, int allowed_state, 
			 bool allow_portzero) {
  
  /* These two are chosen because they come right "before" port 1/tcp */
  map<u16,Port*>::iterator iter;
  
  if (afterthisport) {
    if (afterthisport->proto == IPPROTO_TCP) {
      iter = tcp_ports.find(afterthisport->portno);
      assert(iter != tcp_ports.end());
      iter++;
      while(iter != tcp_ports.end()) {
	if (!allowed_state || iter->second->state == allowed_state)
	  return iter->second;
	iter++;
      }
      /* No more TCP ports ... */
      if (allowed_protocol != 0)
	return NULL;
      
      iter = udp_ports.begin();
    } else {
      assert(afterthisport->proto == IPPROTO_UDP);
      iter = udp_ports.find(afterthisport->portno);
      assert(iter != udp_ports.end());
      iter++;
    }
    while(iter != udp_ports.end()) {
      if (!allowed_state || iter->second->state == allowed_state)
	return iter->second;
      iter++;
    }
    return NULL;
  } 

  // First-time call - try TCP ports first
  if (allowed_protocol == 0 || allowed_protocol == IPPROTO_TCP) {
    iter = tcp_ports.begin();
    while (iter != tcp_ports.end()) {
      if (!allowed_state || iter->second->state == allowed_state)
	return iter->second;
      iter++;
    }
  }
  
  // Maybe we'll have better luck with UDP
  if (allowed_protocol == 0 || allowed_protocol == IPPROTO_UDP) {
    iter = udp_ports.begin();
    while (iter != udp_ports.end()) {
      if (!allowed_state || iter->second->state == allowed_state)
	return iter->second;
      iter++;
    }
  }
  
  // Nuthing found
  return NULL;
}


// Move some popular TCP ports to the beginning of the portlist, because
// that can speed up certain scans.  You should have already done any port
// randomization, this should prevent the ports from always coming out in the
// same order.
void random_port_cheat(u16 *ports, int portcount) {
  int allportidx = 0;
  int popportidx = 0;
  int earlyreplidx = 0;
  u16 pop_ports[] = { 21, 22, 23, 25, 53, 80, 113, 256, 389, 443, 554, 636, 1723, 3389 };
  int num_pop_ports = sizeof(pop_ports) / sizeof(u16);

  for(allportidx = 0; allportidx < portcount; allportidx++) {
    // see if the currentport is a popular port
    for(popportidx = 0; popportidx < num_pop_ports; popportidx++) {
      if (ports[allportidx] == pop_ports[popportidx]) {
	// This one is popular!  Swap it near to the beginning.
	if (allportidx != earlyreplidx) {
	  ports[allportidx] = ports[earlyreplidx];
	  ports[earlyreplidx] = pop_ports[popportidx];
	}
	earlyreplidx++;
	break;
      }
    }
  }
}

