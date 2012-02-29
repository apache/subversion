#!/usr/bin/env python
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

#
# SvnWcSub - Subscribe to a SvnPubSub stream, and keep a set of working copy
# paths in sync
#
# Example:
#  svnwcsub.py svnwcsub.conf
#
# On startup svnwcsub checks the working copy's path, runs a single svn update
# and then watches for changes to that path.
#
# See svnwcsub.conf for more information on its contents.
#

import subprocess
import threading
import sys
import os
import re
import ConfigParser
import time

from twisted.internet import defer, reactor, task, threads
from twisted.internet.utils import getProcessOutput
from twisted.application import internet
from twisted.python import failure, log
from twisted.web.client import HTTPClientFactory, HTTPPageDownloader
from urlparse import urlparse
from xml.sax import handler, make_parser
from twisted.internet import protocol


"""
Wrapper around svn(1), just to keep it from spreading everywhere, incase
we ever convert to another python-subversion bridge api.  (This has happened;
this class used to wrap pysvn.)

Yes, this exposes accessors for each piece of info we need, but it keeps it
simpler.
"""
class SvnClient(object):
    def __init__(self, svnbin, path, url):
        self.svnbin = svnbin
        self.path = path
        self.url = url
        self.info = {}

    def cleanup(self):
        # despite the name, this just deletes the client context ---
        # which is a no-op for svn(1) wrappers.
        pass

    def _run_info(self):
        "run `svn info` and return the output"
        argv = [self.svnbin, "info", "--non-interactive", "--", self.path]
        output = None

        if not os.path.isdir(self.path):
            log.msg("autopopulate %s from %s" % ( self.path, self.url))
            subprocess.check_call([self.svnbin, 'co', '-q', '--non-interactive', '--config-dir', '/home/svnwc/.subversion', '--', self.url, self.path])

        if hasattr(subprocess, 'check_output'):
            output = subprocess.check_output(argv)
        else:
            pipe = subprocess.Popen(argv, stdout=subprocess.PIPE)
            output, _ = pipe.communicate()
            if pipe.returncode:
                raise subprocess.CalledProcessError(pipe.returncode, argv)
        return output
  
    def _get_info(self, force=False):
        "run `svn info` and parse that info self.info"
        if force or not self.info:
            info = {}
            for line in self._run_info().split("\n"):
                # Ensure there's at least one colon-space in the line, to avoid
                # unpack errors.
                name, value = ("%s: " % line).split(': ', 1)
                # Canonicalize the key names.
                info[{
                  "Repository Root": 'repos',
                  "URL": 'url',
                  "Repository UUID": 'uuid',
                  "Revision": 'revision',
                }.get(name, None)] = value[:-2] # unadd the colon-space
            self.info = info

    def get_repos(self):
        self._get_info()
        return unicode(self.info['repos'])

    def get_url(self):
        self._get_info()
        return unicode(self.info['url'])

    def get_uuid(self):
        self._get_info()
        return unicode(self.info['uuid'])

    def update(self):
        subprocess.check_call(
            [self.svnbin, "update", "--non-interactive", "-q", "--", self.path]
        )
        self._get_info(True)
        return int(self.info['revision'])

    # TODO: Wrap status
    def status(self):
        return None

"""This has been historically implemented via svn(1) even when SvnClient
used pysvn."""
class ProcSvnClient(SvnClient):
  def __init__(self, path, svnbin="svn", env=None, url=None):
    super(ProcSvnClient, self).__init__(svnbin, path, url)
    self.env = env

  @defer.inlineCallbacks
  def update(self):
    # removed since this breaks when the SSL certificate names are mismatched, even
    # if we marked them as trust worthy
    # '--trust-server-cert', 
    cmd = [self.svnbin, '--config-dir', '/home/svnwc/.subversion', '--trust-server-cert', '--non-interactive', 'cleanup', self.path]
    output = yield getProcessOutput(cmd[0], args=cmd[1:], env=self.env)
    cmd = [self.svnbin, '--config-dir', '/home/svnwc/.subversion', '--trust-server-cert', '--non-interactive', 'update', '--ignore-externals', self.path]
    output = yield getProcessOutput(cmd[0], args=cmd[1:], env=self.env)
    rev = int(output[output.rfind("revision ")+len("revision "):].replace('.', ''))
    defer.returnValue(rev)

class WorkingCopy(object):
    def __init__(self, bdec, path, url):
        self.lock = threading.Lock()
        self.bdec = bdec
        self.path = path
        self.url = url
        self.repos = None
        self.match = None
        d = threads.deferToThread(self._get_match)
        d.addCallback(self._set_match)

    def _set_match(self, value):
        self.match = str(value[0])
        self.url = value[1]
        self.repos = value[2]
        self.uuid = value[3]
        self.bdec.wc_ready(self)

    def update_applies(self, uuid, path):
        if self.uuid != uuid:
            return False

        path = str(path)
        if path == self.match:
            #print "ua: Simple match"
            # easy case. woo.
            return True
        if len(path) < len(self.match):
            # path is potentially a parent directory of match?
            #print "ua: parent check"
            if self.match[0:len(path)] == path:
                return True
        if len(path) > len(self.match):
            # path is potentially a sub directory of match
            #print "ua: sub dir check"
            if path[0:len(self.match)] == self.match:
                return True
        return False

    @defer.inlineCallbacks
    def update(self):
        c = ProcSvnClient(self.path, self.bdec.svnbin, self.bdec.env, self.url)
        rev = yield c.update()
        c.cleanup()
        defer.returnValue(rev)

    def _get_match(self):
        self.lock.acquire()
        try:
            c = SvnClient(self.bdec.svnbin, self.path, self.url)
            repos = c.get_repos()
            url = c.get_url()
            uuid = c.get_uuid()
            match  = url[len(repos):]
            c.cleanup()
            return [match, url, repos, uuid]
        finally:
            self.lock.release()
        

class HTTPStream(HTTPClientFactory):
    protocol = HTTPPageDownloader

    def __init__(self, url):
        self.url = url
        HTTPClientFactory.__init__(self, url, method="GET", agent="SvnWcSub/0.1.0")

    def pageStart(self, partial):
        pass

    def pagePart(self, data):
        pass

    def pageEnd(self):
        pass

class Revision:
    def __init__(self, repos, rev):
        self.repos = repos
        self.rev = rev
        self.dirs_changed = []

class StreamHandler(handler.ContentHandler):   
    def __init__(self, stream, bdec):
        handler.ContentHandler.__init__(self) 
        self.stream = stream
        self.bdec =  bdec
        self.rev = None
        self.text_value = None

    def startElement(self, name, attrs):
        #print "start element: %s" % (name)
        """
        <commit revision="7">
                        <dirs_changed><path>/</path></dirs_changed>
                      </commit> 
        """
        if name == "commit":
            self.rev = Revision(attrs['repository'], int(attrs['revision']))
        elif name == "stillalive":
            self.bdec.stillalive(self.stream)
    def characters(self, data):
        if self.text_value is not None:
            self.text_value = self.text_value + data 
        else:
            self.text_value = data

    def endElement(self, name):
        #print "end   element: %s" % (name)
        if name == "commit":
            self.bdec.commit(self.stream, self.rev)
            self.rev = None
        if name == "path" and self.text_value is not None and self.rev is not None:
            self.rev.dirs_changed.append(self.text_value.strip())
        self.text_value = None


class XMLHTTPStream(HTTPStream):
    def __init__(self, url, bdec):
        HTTPStream.__init__(self, url)
        self.alive = 0
        self.bdec =  bdec
        self.parser = make_parser(['xml.sax.expatreader'])
        self.handler = StreamHandler(self, bdec)
        self.parser.setContentHandler(self.handler)

    def pageStart(self, parital):
        self.bdec.pageStart(self)

    def pagePart(self, data):
        self.parser.feed(data)

    def pageEnd(self):
        self.bdec.pageEnd(self)

def connectTo(url, bdec):
    u = urlparse(url)
    port = u.port
    if not port:
        port = 80
    s = XMLHTTPStream(url, bdec)
    if bdec.service:
      conn = internet.TCPClient(u.hostname, u.port, s)
      conn.setServiceParent(bdec.service)
    else:
      conn = reactor.connectTCP(u.hostname, u.port, s)
    return [s, conn]


CHECKBEAT_TIME = 60
PRODUCTION_RE_FILTER = re.compile("/websites/production/[^/]+/")

class BigDoEverythingClasss(object):
    def __init__(self, config, service = None):
        self.urls = [s.strip() for s in config.get_value('streams').split()]
        self.svnbin = config.get_value('svnbin')
        self.env = config.get_env()
        self.service = service
        self.failures = 0
        self.alive = time.time()
        self.checker = task.LoopingCall(self._checkalive)
        self.transports = {}
        self.streams = {}
        for u in self.urls:
          self._restartStream(u)
        self.watch = []
        for path, url in config.get_track().items():
            # working copies auto-register with the BDEC when they are ready.
            WorkingCopy(self, path, url)
        self.checker.start(CHECKBEAT_TIME)

    def pageStart(self, stream):
        log.msg("Stream %s Connection Established" % (stream.url))
        self.failures = 0

    def pageEnd(self, stream):
        log.msg("Stream %s Connection Dead" % (stream.url))
        self.streamDead(stream.url)

    def _restartStream(self, url):
        (self.streams[url], self.transports[url]) = connectTo(url, self)
        self.streams[url].deferred.addBoth(self.streamDead, url)
        self.streams[url].alive = time.time()

    def _checkalive(self):
        n = time.time()
        for k in self.streams.keys():
          s = self.streams[k]
          if n - s.alive > CHECKBEAT_TIME:
            log.msg("Stream %s is dead, reconnecting" % (s.url))
            #self.transports[s.url].disconnect()
            self.streamDead(self, s.url)

#        d=filter(lambda x:x not in self.streams.keys(), self.urls)
#        for u in d:
#          self._restartStream(u)

    def stillalive(self, stream):
        stream.alive = time.time()

    def streamDead(self, url, result=None):
        s = self.streams.get(url)
        if not s:
          log.msg("Stream %s is messed up" % (url))
          return
        BACKOFF_SECS = 5
        BACKOFF_MAX = 60
        #self.checker.stop()

        self.streams[url] = None
        self.transports[url] = None
        self.failures += 1
        backoff = min(self.failures * BACKOFF_SECS, BACKOFF_MAX)
        log.msg("Stream disconnected, trying again in %d seconds.... %s" % (backoff, s.url))
        reactor.callLater(backoff, self._restartStream, url)

    @defer.inlineCallbacks
    def wc_ready(self, wc):
        # called when a working copy object has its basic info/url,
        # Add it to our watchers, and trigger an svn update.
        log.msg("Watching WC at %s <-> %s" % (wc.path, wc.url))
        self.watch.append(wc)
        rev = yield wc.update()
        log.msg("wc update: %s is at r%d" % (wc.path, rev))

    def _normalize_path(self, path):
        if path[0] != '/':
            return "/" + path
        return os.path.abspath(path)

    @defer.inlineCallbacks
    def commit(self, stream, rev):
        log.msg("COMMIT r%d (%d paths) via %s" % (rev.rev, len(rev.dirs_changed), stream.url))
        paths = map(self._normalize_path, rev.dirs_changed)
        if len(paths):
            pre = os.path.commonprefix(paths)
            if pre == "/websites/":
                # special case for svnmucc "dynamic content" buildbot commits
                # just take the first production path to avoid updating all cms working copies
                for p in paths:
                    m = PRODUCTION_RE_FILTER.match(p)
                    if m:
                        pre = m.group(0)
                        break

            #print "Common Prefix: %s" % (pre)
            wcs = [wc for wc in self.watch if wc.update_applies(rev.repos, pre)]
            log.msg("Updating %d WC for r%d" % (len(wcs), rev.rev))
            for wc in wcs:
                rev = yield wc.update()
                log.msg("wc update: %s is at r%d" % (wc.path, rev))


class ReloadableConfig(ConfigParser.SafeConfigParser):
    def __init__(self, fname):
        ConfigParser.SafeConfigParser.__init__(self)

        self.fname = fname
        self.read(fname)

        ### install a signal handler to set SHOULD_RELOAD. BDEC should
        ### poll this flag, and then adjust its internal structures after
        ### the reload.
        self.should_reload = False

    def reload(self):
        # Delete everything. Just re-reading would overlay, and would not
        # remove sections/options. Note that [DEFAULT] will not be removed.
        for section in self.sections():
            self.remove_section(section)

        # Now re-read the configuration file.
        self.read(fname)

    def get_value(self, which):
        return self.get(ConfigParser.DEFAULTSECT, which)

    def get_env(self):
        env = os.environ.copy()
        default_options = self.defaults().keys()
        for name, value in self.items('env'):
            if name not in default_options:
                env[name] = value
        return env

    def get_track(self):
        "Return the {PATH: URL} dictionary of working copies to track."
        track = dict(self.items('track'))
        for name in self.defaults().keys():
            del track[name]
        return track

    def optionxform(self, option):
        # Do not lowercase the option name.
        return str(option)


def main(config_file):
    c = ReloadableConfig(config_file)
    big = BigDoEverythingClasss(c)
    reactor.run()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print "invalid args, read source code"
        sys.exit(0) 
    log.startLogging(sys.stdout)
    main(sys.argv[1])
