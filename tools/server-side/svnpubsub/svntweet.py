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
# SvnTweet - Subscribe to a SvnPubSub stream, and Twitter about it!
#
# Example:
#  svntweet.py  my-config.json
#  
# With my-config.json containing stream paths and the twitter auth info:
#    {"stream": "http://svn-master.apache.org:2069/commits/xml",
#     "username": "asfcommits",
#     "password": "MyLuggageComboIs1234"}
#
#
#

import threading
import sys
import os
try:
    import simplejson as json
except ImportError:
    import json

from twisted.internet import defer, reactor, task, threads
from twisted.python import failure, log
from twisted.web.client import HTTPClientFactory, HTTPPageDownloader

from urlparse import urlparse
from xml.sax import handler, make_parser
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "twitty-twister", "lib"))
try:
    import twitter
except:
    print "Get a copy of twitty-twister from <http://github.com/dustin/twitty-twister>"
    sys.exit(-1)
class Config(object):
    def __init__(self, path):
        self.path = path
        self.mtime_path = 0
        self.config = {}
        self._load_config()

    def _load_config(self):
        mtime = os.path.getmtime(self.path)
        if mtime != self.mtime_path:
            fp = open(self.path, "rb")
            self.mtime_path = mtime
            self.config = json.loads(fp.read())

class HTTPStream(HTTPClientFactory):
    protocol = HTTPPageDownloader

    def __init__(self, url):
        HTTPClientFactory.__init__(self, url, method="GET", agent="SvnTweet/0.1.0")

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
        self.author = None
        self.log = None
        self.date = None

class StreamHandler(handler.ContentHandler):   
    def __init__(self, bdec):
        handler.ContentHandler.__init__(self) 
        self.bdec =  bdec
        self.rev = None
        self.text_value = None

    def startElement(self, name, attrs):
        #print "start element: %s" % (name)
        """
        <commit repository="13f79535-47bb-0310-9956-ffa450edef68"
                revision="815618">
            <author>joehni</author>
            <date>2009-09-16 06:00:21 +0000 (Wed, 16 Sep 2009)</date>
            <log>pom.xml is not executable.</log>
            <dirs_changed><path>commons/proper/commons-parent/trunk/</path></dirs_changed>
        </commit>
        """
        if name == "commit":
            self.rev = Revision(repos=attrs['repository'],
                                rev=int(attrs['revision']))
        elif name == "stillalive":
            self.bdec.stillalive()
    def characters(self, data):
        if self.text_value is not None:
            self.text_value = self.text_value + data 
        else:
            self.text_value = data

    def endElement(self, name):
        #print "end   element: %s" % (name)
        if name == "commit":
            self.bdec.commit(self.rev)
            self.rev = None
        if self.text_value is not None and self.rev is not None:
            if name == "path":
                self.rev.dirs_changed.append(self.text_value.strip())
            if name == "author":
                self.rev.author = self.text_value.strip()
            if name == "date":
                self.rev.date = self.text_value.strip()
            if name == "log":
                self.rev.log = self.text_value.strip()
        self.text_value = None


class XMLHTTPStream(HTTPStream):
    def __init__(self, url, bdec):
        HTTPStream.__init__(self, url)
        self.bdec =  bdec
        self.parser = make_parser(['xml.sax.expatreader'])
        self.handler = StreamHandler(bdec)
        self.parser.setContentHandler(self.handler)

    def pageStart(self, parital):
        self.bdec.pageStart()

    def pagePart(self, data):
        self.parser.feed(data)

def connectTo(url, bdec):
    u = urlparse(url)
    port = u.port
    if not port:
        port = 80
    s = XMLHTTPStream(url, bdec)
    conn = reactor.connectTCP(u.hostname, u.port, s)
    return [s, conn]


CHECKBEAT_TIME = 90

class BigDoEverythingClasss(object):
    def __init__(self, config):
        self.c = config
        self.c._load_config()
        self.url = str(self.c.config.get('stream'))
        self.failures = 0
        self.alive = time.time()
        self.checker = task.LoopingCall(self._checkalive)
        self.transport = None
        self.stream = None
        self._restartStream()
        self.watch = []
        self.twit = twitter.Twitter(self.c.config.get('username'), self.c.config.get('password'))

    def pageStart(self):
        log.msg("Stream Connection Established")
        self.failures = 0
        
    def _restartStream(self):
        (self.stream, self.transport) = connectTo(self.url, self)
        self.stream.deferred.addBoth(self.streamDead)
        self.alive = time.time()
        self.checker.start(CHECKBEAT_TIME)

    def _checkalive(self):
        n = time.time()
        if n - self.alive > CHECKBEAT_TIME:
            log.msg("Stream is dead, reconnecting")
            self.transport.disconnect()

    def stillalive(self):
        self.alive = time.time()

    def streamDead(self, v):
        BACKOFF_SECS = 5
        BACKOFF_MAX = 60
        self.checker.stop()

        self.stream = None
        self.failures += 1
        backoff = min(self.failures * BACKOFF_SECS, BACKOFF_MAX)
        log.msg("Stream disconnected, trying again in %d seconds.... %s" % (backoff, self.url))
        reactor.callLater(backoff, self._restartStream)

    def _normalize_path(self, path):
        if path[0] != '/':
            return "/" + path
        return os.path.abspath(path)

    def tweet(self, msg):
        log.msg("SEND TWEET: %s" % (msg))
        self.twit.update(msg).addCallback(self.tweet_done).addErrback(log.msg)

    def tweet_done(self, x):
        log.msg("TWEET: Success!")

    def build_tweet(self, rev):
        maxlen = 144
        left = maxlen
        paths = map(self._normalize_path, rev.dirs_changed)
        if not len(paths):
            return None
        path = os.path.commonprefix(paths)
        if path[0:1] == '/' and len(path) > 1:
            path = path[1:]

        #TODO: shorter link
        link = " - http://svn.apache.org/viewvc?view=rev&revision=%d" % (rev.rev)
        left -= len(link)
        msg = "r%d in %s by %s: "  % (rev.rev, path, rev.author)
        left -= len(msg)
        if left > 3:
            msg += rev.log[0:left]
        msg += link
        return msg

    def commit(self, rev):
        log.msg("COMMIT r%d (%d paths)" % (rev.rev, len(rev.dirs_changed)))
        msg = self.build_tweet(rev)
        if msg:
            self.tweet(msg)
            #print "Common Prefix: %s" % (pre)

def main(config_file):
    c = Config(config_file)
    big = BigDoEverythingClasss(c)
    reactor.run()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print "invalid args, read source code"
        sys.exit(0) 
    log.startLogging(sys.stdout)
    main(sys.argv[1])
