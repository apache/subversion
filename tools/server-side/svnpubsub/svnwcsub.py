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
import logging
import Queue

from twisted.internet import reactor, task, threads
from twisted.internet.utils import getProcessOutput
from twisted.application import internet
from twisted.web.client import HTTPClientFactory, HTTPPageDownloader
from urlparse import urlparse
from xml.sax import handler, make_parser
from twisted.internet import protocol


# check_output() is only available in Python 2.7. Allow us to run with
# earlier versions
try:
    check_output = subprocess.check_output
except AttributeError:
    def check_output(args):  # note: we don't use anything beyond args
        pipe = subprocess.Popen(args, stdout=subprocess.PIPE)
        output, _ = pipe.communicate()
        if pipe.returncode:
            raise subprocess.CalledProcessError(pipe.returncode, args)
        return output


### note: this runs synchronously. within the current Twisted environment,
### it is called from ._get_match() which is run on a thread so it won't
### block the Twisted main loop.
def svn_info(svnbin, path):
    "Run 'svn info' on the target path, returning a dict of info data."
    args = [svnbin, "info", "--non-interactive", "--", path]
    output = check_output(args).strip()
    info = { }
    for line in output.split('\n'):
        idx = line.index(':')
        info[line[:idx]] = line[idx+1:].strip()
    return info


class WorkingCopy(object):
    def __init__(self, bdec, path, url):
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

    def _get_match(self):
        ### quick little hack to auto-checkout missing working copies
        if not os.path.isdir(self.path):
            logging.info("autopopulate %s from %s" % (self.path, self.url))
            subprocess.check_call([self.bdec.svnbin, 'co', '-q',
                                   '--non-interactive',
                                   '--config-dir',
                                   '/home/svnwc/.subversion',
                                   '--', self.url, self.path])

        # Fetch the info for matching dirs_changed against this WC
        info = svn_info(self.bdec.svnbin, self.path)
        url = info['URL']
        repos = info['Repository Root']
        uuid = info['Repository UUID']
        relpath = url[len(repos):]  # also has leading '/'
        return [relpath, url, repos, uuid]
        

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
        self.worker = BackgroundWorker(self.svnbin, self.env)
        self.worker.start()
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
        logging.info("Stream %s Connection Established" % (stream.url))
        self.failures = 0

    def pageEnd(self, stream):
        logging.info("Stream %s Connection Dead" % (stream.url))
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
            logging.info("Stream %s is dead, reconnecting" % (s.url))
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
          logging.info("Stream %s is messed up" % (url))
          return
        BACKOFF_SECS = 5
        BACKOFF_MAX = 60
        #self.checker.stop()

        self.streams[url] = None
        self.transports[url] = None
        self.failures += 1
        backoff = min(self.failures * BACKOFF_SECS, BACKOFF_MAX)
        logging.info("Stream disconnected, trying again in %d seconds.... %s" % (backoff, s.url))
        reactor.callLater(backoff, self._restartStream, url)

    def wc_ready(self, wc):
        # called when a working copy object has its basic info/url,
        # Add it to our watchers, and trigger an svn update.
        logging.info("Watching WC at %s <-> %s" % (wc.path, wc.url))
        self.watch.append(wc)
        self.worker.add_work(OP_UPDATE, wc)

    def _normalize_path(self, path):
        if path[0] != '/':
            return "/" + path
        return os.path.abspath(path)

    def commit(self, stream, rev):
        logging.info("COMMIT r%d (%d paths) via %s" % (rev.rev, len(rev.dirs_changed), stream.url))
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
            logging.info("Updating %d WC for r%d" % (len(wcs), rev.rev))
            for wc in wcs:
                self.worker.add_work(OP_UPDATE, wc)


# Start logging warnings if the work backlog reaches this many items
BACKLOG_TOO_HIGH = 20
OP_UPDATE = 'update'
OP_CLEANUP = 'cleanup'

class BackgroundWorker(threading.Thread):
    def __init__(self, svnbin, env):
        threading.Thread.__init__(self)

        # The main thread/process should not wait for this thread to exit.
        self.daemon = True

        self.svnbin = svnbin
        self.env = env
        self.q = Queue.Queue()

    def run(self):
        while True:
            if self.q.qsize() > BACKLOG_TOO_HIGH:
                logging.warn('worker backlog is at %d', self.q.qsize())

            # This will block until something arrives
            operation, wc = self.q.get()
            try:
                if operation == OP_UPDATE:
                    self._update(wc)
                elif operation == OP_CLEANUP:
                    self._cleanup(wc)
                else:
                    logging.critical('unknown operation: %s', operation)
            except:
                logging.exception('exception in worker')

            # In case we ever want to .join() against the work queue
            self.q.task_done()

    def add_work(self, operation, wc):
        self.q.put((operation, wc))

    def _update(self, wc):
        "Update the specified working copy."

        # For giggles, let's clean up the working copy in case something
        # happened earlier.
        self._cleanup(wc)

        logging.info("updating: %s", wc.path)

        ### we need to move some of these args into the config. these are
        ### still specific to the ASF setup.
        args = [self.svnbin, 'update',
                '--quiet',
                '--config-dir', '/home/svnwc/.subversion',
                '--non-interactive',
                '--trust-server-cert',
                '--ignore-externals',
                wc.path]
        subprocess.check_call(args, env=self.env)

        ### check the loglevel before running 'svn info'?
        info = svn_info(self.svnbin, wc.path)
        logging.info("updated: %s now at r%s", wc.path, info['Revision'])

    def _cleanup(self, wc):
        "Run a cleanup on the specified working copy."

        ### we need to move some of these args into the config. these are
        ### still specific to the ASF setup.
        args = [self.svnbin, 'cleanup',
                '--config-dir', '/home/svnwc/.subversion',
                '--non-interactive',
                '--trust-server-cert',
                wc.path]
        subprocess.check_call(args, env=self.env)


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

    ### use logging.INFO for now. review/adjust the calls above for the
    ### proper logging level. then remove the level (to return to default).
    ### future: switch to config for logfile and loglevel.
    logging.basicConfig(level=logging.INFO, stream=sys.stdout,
                        datefmt='%Y-%m-%d %H:%M:%S',
                        format='%(asctime)s [%(levelname)s] %(message)s')
    main(sys.argv[1])
