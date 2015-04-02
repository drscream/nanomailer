// nullmailer -- a simple relay-only MTA
// Copyright (C) 2012  Bruce Guenter <bruce@untroubled.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// You can contact me at <bruce@untroubled.org>.  There is also a mailing list
// available to discuss this package.  To subscribe, send an email to
// <nullmailer-subscribe@lists.untroubled.org>.

#include "config.h"
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "errcodes.h"
#include "connect.h"
#ifdef HAVE_GETADDRINFO
#include "lib/itoa.h"
#endif

#ifndef HAVE_GETADDRINFO
static int sethostbyname(const mystring& hostname, struct sockaddr_in& sa)
{
  struct hostent *he = gethostbyname(hostname.c_str());
  if(!he) {
    switch(h_errno) {
    case HOST_NOT_FOUND: return -ERR_HOST_NOT_FOUND;
    case NO_ADDRESS: return -ERR_NO_ADDRESS;
    case NO_RECOVERY: return -ERR_GHBN_FATAL;
    case TRY_AGAIN: return -ERR_GHBN_TEMP;
    default: return -ERR_GHBN_TEMP;
    }
  }
  memcpy(&sa.sin_addr, he->h_addr, he->h_length);
  return 0;
}
#endif

int tcpconnect(const mystring& hostname, int port)
{
#ifdef HAVE_GETADDRINFO
  struct addrinfo req, *res, *orig_res;
  const char *service = itoa(port, 6);

  memset(&req, 0, sizeof(req));
  req.ai_flags = AI_NUMERICSERV;
  req.ai_socktype = SOCK_STREAM;
  int e = getaddrinfo(hostname.c_str(), service, &req, &res);
#else
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  int e = sethostbyname(hostname, sa);
#endif
  if(e) return e;
#ifdef HAVE_GETADDRINFO
  int s = -1;
  orig_res = res;

  for (; res; res = res->ai_next ) {
    s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if(s < 0)
	continue;

    if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
    	s = -1;
	continue;
    }

    /* sucessful connection */
    break;
  }

  freeaddrinfo(orig_res);

  if(s < 0)
    return -ERR_CONN_FAILED;
#else
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  int s = socket(PF_INET, SOCK_STREAM, 0);
  if(s == -1)
    return -ERR_SOCKET;
  if(connect(s, (sockaddr*)&sa, sizeof(sa)) != 0) {
    switch(errno) {
    case ECONNREFUSED: return -ERR_CONN_REFUSED;
    case ETIMEDOUT: return -ERR_CONN_TIMEDOUT;
    case ENETUNREACH: return -ERR_CONN_UNREACHABLE;
    default: return -ERR_CONN_FAILED;
    }
  }
#endif
  return s;
}
