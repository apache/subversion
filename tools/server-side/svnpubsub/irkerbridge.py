#!/usr/bin/env python
#
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# IrkerBridge - Bridge an SvnPubSub stream to Irker.

# Example:
#  irkerbridge.py --daemon --pidfile pid --logfile log config
#
# For detailed option help use:
#  irkerbridge.py --help

# It expects a config file that has the following parameters:
# streams=url
#   Space separated list of URLs to streams.
#   This option should only be in the DEFAULT section, is ignored in
#   all other sections.
# irker=hostname:port
#   The hostname/port combination of the irker daemon.  If port is
#   omitted it defaults to 6659.  Irker is connected to over UDP.
# match=What to use to decide if the commit should be sent to irker.
#   It consists of the repository UUID followed by a slash and a glob pattern.
#   The UUID may be replaced by a * to match all UUIDs. The glob pattern will
#   be matched against all of the dirs_changed.  Both the UUID and the glob
#   pattern must match to send the message to irker.
# to=url
#   Space separated list of URLs (any URL that Irker will accept) to
#   send the resulting message to.  At current Irker only supports IRC.
# template=string
#   A string to use to format the output.  The string is a Python
#   string Template.  The following variables are available:
#   $committer, $id, $date, $repository, $log, $log_firstline,
#   $log_firstparagraph, $dirs_changed, $dirs_count, $dirs_count_s,
#   $subdirs_count, $subdirs_count_s, $dirs_root
#   Most of them should be self explanatory.  $dirs_count is the number of
#   entries in $dirs_changed, $dirs_count_s is a friendly string version,
#   $dirs_root is the common root of all the $dirs_changed, $subdirs_count
#   is the number of subdirs under the $dirs_root that changed,
#   $subdirs_root_s is a friendly string version. $log_firstparagraph cuts
#   the log message at the first blank line and replaces newlines with spaces.
#
# Within the config file you have sections.  Any configuration option
# missing from a given section is found in the [DEFAULT] section.
#
# Section names are arbitrary names that mean nothing to the bridge.  Each
# section other than the [DEFAULT] section consists of a configuration that
# may match and send a message to irker to deliver.  All matching sections
# will generate a message.
#
# Interpolation of values within the config file is allowed by including
# %(name)s within a value.  For example I can reference the UUID of a repo
# repeatedly by doing:
# [DEFAULT]
# ASF_REPO=13f79535-47bb-0310-9956-ffa450edef68
#
# [#commits]
# match=%(ASF_REPO)s/
#
# You can HUP the process to reload the config file without restarting the
# process.  However, you cannot change the streams it is listening to without
# restarting the process.
#
# TODO: Logging in a better way.

# Messages longer than this will be truncated and ... added to the end such
# that the resulting message is no longer than this:
MAX_PRIVMSG = 400

import os
import sys
import posixpath
import socket
import json
import optparse
import ConfigParser
import traceback
import signal
import re
import fnmatch
from string import Template

try:
  # Python >=3.0
  from urllib.parse import urlparse
except ImportError:
  # Python <3.0
  from urlparse import urlparse


# Packages that come with svnpubsub
import svnpubsub.client
import daemonize

class Daemon(daemonize.Daemon):
  def __init__(self, logfile, pidfile, bdec):
    daemonize.Daemon.__init__(self, logfile, pidfile)

    self.bdec = bdec

  def setup(self):
    # There is no setup which the parent needs to wait for.
    pass

  def run(self):
    print('irkerbridge started, pid=%d' % (os.getpid()))

    mc = svnpubsub.client.MultiClient(self.bdec.urls,
                                      self.bdec.commit,
                                      self.bdec.event)
    mc.run_forever()


class BigDoEverythingClass(object):
  def __init__(self, config, options):
    self.config = config
    self.options = options
    self.urls = config.get_value('streams').split()

  def locate_matching_configs(self, commit):
    result = [ ]
    for section in self.config.sections():
      match = self.config.get(section, "match").split('/', 1)
      if len(match) < 2:
        # No slash so assume all paths
        match.append('*')
      match_uuid, match_path = match
      if commit.repository == match_uuid or match_uuid == "*":
        for path in commit.changed:
          if fnmatch.fnmatch(path, match_path):
            result.append(section)
            break
    return result

  def _generate_dirs_changed(self, commit):
    if hasattr(commit, 'dirs_changed') or not hasattr(commit, 'changed'):
      return

    dirs_changed = set()
    for p in commit.changed:
      if p[-1] == '/' and commit.changed[p]['flags'][1] == 'U':
        # directory with property changes add the directory itself.
        dirs_changed.add(p)
      else:
        # everything else add the parent of the path
        # directories have a trailing slash so if it's present remove
        # it before finding the parent.  The result will be a directory
        # so it needs a trailing slash
        dirs_changed.add(posixpath.dirname(p.rstrip('/')) + '/')

    commit.dirs_changed = dirs_changed
    return

  def fill_in_extra_args(self, commit):
    # Set any empty members to the string "<null>"
    v = vars(commit)
    for k in v.keys():
      if not v[k]:
        v[k] = '<null>'

    self._generate_dirs_changed(commit)
    # Add entries to the commit object that are useful for
    # formatting.
    commit.log_firstline = commit.log.split("\n",1)[0]
    commit.log_firstparagraph = re.split("\r?\n\r?\n",commit.log,1)[0]
    commit.log_firstparagraph = re.sub("\r?\n"," ",commit.log_firstparagraph)
    if commit.dirs_changed:
      commit.dirs_root = posixpath.commonprefix(commit.dirs_changed)
      if commit.dirs_root == '':
        commit.dirs_root = '/'
      commit.dirs_count = len(commit.dirs_changed)
      if commit.dirs_count > 1:
        commit.dirs_count_s = " (%d dirs)" %(commit.dirs_count)
      else:
        commit.dirs_count_s = ""

      commit.subdirs_count = commit.dirs_count
      if commit.dirs_root in commit.dirs_changed:
        commit.subdirs_count -= 1
      if commit.subdirs_count >= 1:
        commit.subdirs_count_s = " + %d subdirs" % (commit.subdirs_count)
      else:
        commit.subdirs_count_s = ""

  def _send(self, irker, msg):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    irker_list = irker.split(':')
    if len(irker_list) < 2:
      irker_list.append(6659)
    json_msg = json.dumps(msg)
    sock.sendto(json_msg, (irker_list[0],int(irker_list[1])))
    if self.options.verbose:
      print("SENT: %s to %s" % (json_msg, irker))

  def join_all(self):
    # Like self.commit(), but ignores self.config.get(section, "template").
    for section in self.config.sections():
      irker = self.config.get(section, "irker")
      to_list = self.config.get(section, "to").split()
      if not irker or not to_list:
        continue
      for to in to_list:
        msg = {'to': to, 'privmsg': ''}
        self._send(irker, msg)

  def commit(self, url, commit):
    if self.options.verbose:
      print("RECV: from %s" % url)
      print(json.dumps(vars(commit), indent=2))

    try:
      config_sections = self.locate_matching_configs(commit)
      if len(config_sections) > 0:
        self.fill_in_extra_args(commit)
        for section in config_sections:
          irker = self.config.get(section, "irker")
          to_list = self.config.get(section, "to").split()
          template = self.config.get(section, "template")
          if not irker or not to_list or not template:
            continue
          privmsg = Template(template).safe_substitute(vars(commit))
          if len(privmsg) > MAX_PRIVMSG:
            privmsg = privmsg[:MAX_PRIVMSG-3] + '...'
          for to in to_list:
            msg = {'to': to, 'privmsg': privmsg}
            self._send(irker, msg)

    except:
      print("Unexpected error:")
      traceback.print_exc()
      sys.stdout.flush()
      raise

  def event(self, url, event_name, event_arg):
    if self.options.verbose or event_name != "ping":
      print('EVENT: %s from %s' % (event_name, url))
      sys.stdout.flush()



class ReloadableConfig(ConfigParser.SafeConfigParser):
  def __init__(self, fname):
    ConfigParser.SafeConfigParser.__init__(self)

    self.fname = fname
    self.read(fname)

    signal.signal(signal.SIGHUP, self.hangup)

  def hangup(self, signalnum, frame):
    self.reload()

  def reload(self):
    print("RELOAD: config file: %s" % self.fname)
    sys.stdout.flush()

    # Delete everything. Just re-reading would overlay, and would not
    # remove sections/options. Note that [DEFAULT] will not be removed.
    for section in self.sections():
      self.remove_section(section)

    # Get rid of [DEFAULT]
    self.remove_section(ConfigParser.DEFAULTSECT)

    # Now re-read the configuration file.
    self.read(self.fname)

  def get_value(self, which):
    return self.get(ConfigParser.DEFAULTSECT, which)


def main(args):
  parser = optparse.OptionParser(
      description='An SvnPubSub client that bridges the data to irker.',
      usage='Usage: %prog [options] CONFIG_FILE',
      )
  parser.add_option('--logfile',
      help='filename for logging')
  parser.add_option('--verbose', action='store_true',
      help="enable verbose logging")
  parser.add_option('--pidfile',
      help="the process' PID will be written to this file")
  parser.add_option('--daemon', action='store_true',
      help='run as a background daemon')

  options, extra = parser.parse_args(args)

  if len(extra) != 1:
    parser.error('CONFIG_FILE is requried')
  config_file = os.path.abspath(extra[0])

  logfile, pidfile = None, None
  if options.daemon:
    if options.logfile:
      logfile = os.path.abspath(options.logfile)
    else:
      parser.error('LOGFILE is required when running as a daemon')

    if options.pidfile:
      pidfile = os.path.abspath(options.pidfile)
    else:
      parser.error('PIDFILE is required when running as a daemon')


  config = ReloadableConfig(config_file)
  bdec = BigDoEverythingClass(config, options)

  d = Daemon(logfile, pidfile, bdec)
  if options.daemon:
    d.daemonize_exit()
  else:
    d.foreground()

if __name__ == "__main__":
  main(sys.argv[1:])
