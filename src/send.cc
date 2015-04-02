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
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "ac/time.h"
#include "configio.h"
#include "defines.h"
#include "errcodes.h"
#include "fdbuf/fdbuf.h"
#include "hostname.h"
#include "itoa.h"
#include "list.h"
#include "selfpipe.h"
#include "setenv.h"
#include "cli++/cli++.h"

const char* cli_program = "nullmailer-send";

selfpipe selfpipe;

typedef list<mystring> slist;

static int use_syslog = 0;
static int daemonize  = 0;

const char* cli_program     = "nullmailer-send";
const char* cli_help_prefix = "nullmailer daemon\n";
const char* cli_help_suffix = "";
const char* cli_args_usage  = "";
const int cli_args_min = 0;
const int cli_args_max = 0;
cli_option cli_options[] = {
  { 'd', "daemon", cli_option::flag, 1, &daemonize,  "daemonize , implies --syslog", 0 },
  { 's', "syslog", cli_option::flag, 1, &use_syslog, "use syslog", 0 },
  { 0, 0, cli_option::flag, 0, 0, 0, 0 }
};

#define fail(MSG) do { if (use_syslog) syslog(LOG_ERR, "%s", MSG); if (!daemonize) ferr << MSG << endl; return false; } while (0)
#define fail2(MSG1,MSG2) do { if (use_syslog) syslog(LOG_ERR, "%s %s", MSG1, MSG2); if (!daemonize) fout << MSG1 << MSG2 << endl; return false; } while (0)
#define fail_sys(MSG) do { if (use_syslog) syslog(LOG_ERR, "%s %s", MSG, strerror(errno)); if (!daemonize) ferr << MSG << strerror(errno) << endl; return false; } while (0)

struct remote
{
  static const mystring default_proto;
  
  mystring host;
  mystring proto;
  mystring program;
  slist options;
  remote(const slist& list);
  ~remote();
  void exec(int fd);
};

const mystring remote::default_proto = "smtp";

remote::remote(const slist& lst)
{
  slist::const_iter iter = lst;
  host = *iter;
  ++iter;
  if(!iter)
    proto = default_proto;
  else {
    proto = *iter;
    for(++iter; iter; ++iter)
      options.append(*iter);
  }
  program = PROTOCOL_DIR + proto;
}

remote::~remote() { }

void remote::exec(int fd)
{
  if (!daemonize && close(STDIN_FILENO) < 0)
    return;
  if (fd != STDIN_FILENO)
    if (dup2(fd, STDIN_FILENO) < 0 || close(fd) < 0)
      return;
  const char* args[5+options.count()];
  unsigned i = 0;
  args[i++] = program.c_str();
  if (daemonize)
    args[i++] = "-d";
  if (use_syslog)
    args[i++] = "-s";
  for(slist::const_iter opt(options); opt; opt++)
    args[i++] = strdup((*opt).c_str());
  args[i++] = host.c_str();
  args[i++] = 0;
  execv(args[0], (char**)args);
}

typedef list<remote> rlist;

unsigned ws_split(const mystring& str, slist& lst)
{
  lst.empty();
  const char* ptr = str.c_str();
  const char* end = ptr + str.length();
  unsigned count = 0;
  for(;;) {
    while(ptr < end && isspace(*ptr))
      ++ptr;
    const char* start = ptr;
    while(ptr < end && !isspace(*ptr))
      ++ptr;
    if(ptr == start)
      break;
    lst.append(mystring(start, ptr-start));
    ++count;
  }
  return count;
}

static rlist remotes;
static int minpause = 60;
static int pausetime = minpause;
static int maxpause = 24*60*60;
static int sendtimeout = 60*60;

bool load_remotes()
{
  slist rtmp;
  config_readlist("remotes", rtmp);
  remotes.empty();
  for(slist::const_iter r(rtmp); r; r++) {
    if((*r)[0] == '#')
      continue;
    slist parts;
    if(!ws_split(*r, parts))
      continue;
    remotes.append(remote(parts));
  }
  if (remotes.count() == 0)
    fail("No remote hosts listed for delivery");
  return true;
}

bool load_config()
{
  mystring hh;

  if (!config_read("helohost", hh))
    hh = me;
  setenv("HELOHOST", hh.c_str(), 1);

  int oldminpause = minpause;
  if(!config_readint("pausetime", minpause))
    minpause = 60;
  if(!config_readint("maxpause", maxpause))
    maxpause = 24*60*60;
  if(!config_readint("sendtimeout", sendtimeout))
    sendtimeout = 60*60;

  if (minpause != oldminpause)
    pausetime = minpause;

  return load_remotes();
}

static slist files;
static bool reload_files = false;

void catch_alrm(int)
{
  signal(SIGALRM, catch_alrm);
  reload_files = true;
}

bool load_files()
{
  reload_files = false;
  if (use_syslog)
    syslog(LOG_INFO, "Rescanning queue.");
  if (!daemonize)
    fout << "Rescanning queue." << endl;
  DIR* dir = opendir(".");
  if(!dir)
    fail_sys("Cannot open queue directory: ");
  files.empty();
  struct dirent* entry;
  while((entry = readdir(dir)) != 0) {
    const char* name = entry->d_name;
    if(name[0] == '.')
      continue;
    files.append(name);
  }
  closedir(dir);
  return true;
}

bool catchsender(pid_t pid)
{
  int status;

  for (;;) {
    switch (selfpipe.waitsig(sendtimeout)) {
    case 0:			// timeout
      kill(pid, SIGTERM);
      waitpid(pid, &status, 0);
      selfpipe.waitsig();	// catch the signal from killing the child
      fail("Sending timed out, killing protocol");
    case -1:
      fail_sys("Error waiting for the child signal: ");
    case SIGCHLD:
      break;
    default:
      continue;
    }
    break;
  }

  if(waitpid(pid, &status, 0) == -1)
    fail_sys("Error catching the child process return value: ");
  else {
    if(WIFEXITED(status)) {
      status = WEXITSTATUS(status);
      if(status)
	fail2("Sending failed: ", errorstr(status));
      else {
        if (use_syslog)
          syslog(LOG_INFO, "Sent file.");
        if (!daemonize)
             fout << "Sent file." << endl;
	return true;
      }
    }
    else
      fail("Sending process crashed or was killed.");
  }
}

bool send_one(mystring filename, remote& remote)
{
  int fd = open(filename.c_str(), O_RDONLY);
  if(fd == -1) {
    if (use_syslog)
      syslog(LOG_ERR, "Can't open file '%s'", filename.c_str());
    if (!daemonize)
      fout << "Can't open file '" << filename << "'" << endl;
    return false;
  }
  if (use_syslog)
     syslog(LOG_INFO, "Starting delivery: protocol: %s host: %s file: %s",
      remote.proto.c_str(), remote.host.c_str(), filename.c_str());
  if (!daemonize)
  if (use_syslog)
    syslog(LOG_INFO, "Starting delivery, %d message(s) in queue.", files.count());
  if (!daemonize)
    fout << "Starting delivery: protocol: " << remote.proto
  const mystring program = PROTOCOL_DIR + remote.proto;
  fout << "Starting delivery: protocol: " << remote.proto
       << " host: " << remote.host
       << " file: " << filename << endl;
  pid_t pid = fork();
  switch(pid) {
  case -1:
    fail_sys("Fork failed: ");
  case 0:
    remote.exec(fd);
    exit(ERR_EXEC_FAILED);
  default:
    close(fd);
    if(!catchsender(pid))
      return false;
    if(unlink(filename.c_str()) == -1)
      fail_sys("Can't unlink file: ");
  }
  return true;
}

bool send_all()
{
  if(!load_config())
    return false;		// Error message already printed
  if(remotes.count() <= 0)
    fail("No remote hosts listed for delivery");
  if(files.count() == 0)
    return true;
  fout << "Starting delivery, "
       << itoa(files.count()) << " message(s) in queue." << endl;
  for(rlist::iter remote(remotes); remote; remote++) {
    slist::iter file(files);
    while(file) {
      if(send_one(*file, *remote))
	files.remove(file);
      else
	file++;
    }
  }
  if (use_syslog)
    syslog(LOG_INFO, "Delivery complete, %d message(s) remain.", files.count());
  if (!daemonize)
    fout << "Delivery complete, "
       << itoa(files.count()) << " message(s) remain." << endl;
  return true;
}

static int trigger;
#ifdef NAMEDPIPEBUG
static int trigger2;
#endif

bool open_trigger()
{
  trigger = open(QUEUE_TRIGGER, O_RDONLY|O_NONBLOCK);
#ifdef NAMEDPIPEBUG
  trigger2 = open(QUEUE_TRIGGER, O_WRONLY|O_NONBLOCK);
#endif
  if(trigger == -1)
    fail_sys("Could not open trigger file: ");
  return true;
}

bool read_trigger()
{
  if(trigger != -1) {
    char buf[1024];
    read(trigger, buf, sizeof buf);
#ifdef NAMEDPIPEBUG
    close(trigger2);
#endif
    close(trigger);
  }
  return open_trigger();
}

bool do_select()
{
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(trigger, &readfds);
  struct timeval timeout;

  if (files.count() == 0)
    pausetime = maxpause;
  timeout.tv_sec = pausetime;
  timeout.tv_usec = 0;

  pausetime *= 2;
  if (pausetime > maxpause)
    pausetime = maxpause;

  int s = select(trigger+1, &readfds, 0, 0, &timeout);
  if(s == 1) {
    if (use_syslog)
      syslog(LOG_INFO, "Trigger pulled.");
    if (!daemonize)
      fout << "Trigger pulled." << endl;
    read_trigger();
    reload_files = true;
    pausetime = minpause;
  }
  else if(s == -1 && errno != EINTR)
    fail_sys("Internal error in select: ");
  else if(s == 0)
    reload_files = true;
  if(reload_files)
    load_files();
  return true;
}

int cli_main(int, char*[])
{
  pid_t pid;

  if (daemonize)
    use_syslog = 1;
  if (use_syslog)
    openlog("nullmailer", LOG_CONS | LOG_PID, LOG_MAIL);
  read_hostnames();

  if(!selfpipe) {
    fout << "Could not set up self-pipe." << endl;
    return 1;
  }
  selfpipe.catchsig(SIGCHLD);
  
  if(!open_trigger()) {
    if (use_syslog)
      syslog(LOG_CRIT, "Could not open trigger.");
    if (!daemonize)
      ferr << "Could not open trigger." << endl;
    return 1;
  }
  if(chdir(QUEUE_MSG_DIR) == -1) {
    fout << "Could not chdir to queue message directory." << endl;
    if (use_syslog)
      syslog(LOG_CRIT, "Could not chdir to queue message directory.");
    if (!daemonize)
      ferr << "Could not chdir to queue message directory." << endl;
    return 1;
  }

  if (daemonize) {
    if ((pid = fork()) < 0) {
      syslog(LOG_CRIT, "Could not fork.");
      return 1;
    }
    if (pid)
      return 0;
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
  }

  signal(SIGALRM, catch_alrm);
  signal(SIGHUP, SIG_IGN);
  load_config();
  load_files();
  for(;;) {
    send_all();
    if (minpause == 0) break;
    do_select();
  }
  return 0;
}
