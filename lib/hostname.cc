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
#include "mystring/mystring.h"
#include "configio.h"
#include "hostname.h"
#include "canonicalize.h"

mystring me;
mystring defaulthost;
mystring defaultdomain;

void read_hostnames()
{
  int nome;
  nome = 0;
  // introduced as bufix for #120660, #157259, #158412 in 1.00RC5-17;
  // still there since it's more appropriate for Debian systems
  if (!config_read("../mailname", me)) {
    nome = 1;
    me = "me";
  }
  // allow defaultdomain to be empty for Debian
  config_read("defaultdomain", defaultdomain);
  if (!config_read("defaulthost", defaulthost))
    defaulthost = nome ? "defaulthost" : me.c_str();
  canonicalize(defaulthost);
}
