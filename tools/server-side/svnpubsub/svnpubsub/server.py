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
# SvnPubSub - Simple Push Notification of Subversion commits
#
# Based on the theory behind the Live Journal Atom Streaming Service:
#   <http://atom.services.livejournal.com/>
#
# Instead of using a complicated XMPP/AMPQ/JMS/super messaging service,
# we have simple HTTP GETs and PUTs to get data in and out.
#
# Currently supports both XML and JSON serialization.
#
# Example Sub clients:
#   curl -sN http://127.0.0.1:2069/commits
#   curl -sN http://127.0.0.1:2069/commits/svn/*
#   curl -sN http://127.0.0.1:2069/commits/svn
#   curl -sN http://127.0.0.1:2069/commits/*/13f79535-47bb-0310-9956-ffa450edef68
#   curl -sN http://127.0.0.1:2069/commits/svn/13f79535-47bb-0310-9956-ffa450edef68
#
#   URL is built into 2 parts:
#       /commits/${optional_type}/${optional_repository}
#
#   If the type is included in the URL, you will only get commits of that type.
#   The type can be * and then you will receive commits of any type.
#
#   If the repository is included in the URL, you will only receive
#   messages about that repository.  The repository can be * and then you
#   will receive messages about all repositories.
#
# Example Pub clients:
#   curl -T revinfo.json -i http://127.0.0.1:2069/commits
#
# TODO:
#   - Add Real access controls (not just 127.0.0.1)
#   - Document PUT format
#   - Convert to twisted.python.log




try:
    import simplejson as json
except ImportError:
    import json

import sys

import twisted
from twisted.internet import reactor
from twisted.internet import defer
from twisted.web import server
from twisted.web import resource
from twisted.python import log

import time

class Commit:
    def __init__(self, r):
        self.__dict__.update(r)
        if not self.check_value('repository'):
            raise ValueError('Invalid Repository Value')
        if not self.check_value('type'):
            raise ValueError('Invalid Type Value')
        if not self.check_value('format'):
            raise ValueError('Invalid Format Value')
        if not self.check_value('id'):
            raise ValueError('Invalid ID Value')

    def check_value(self, k):
        return hasattr(self, k) and self.__dict__[k]

    def render_commit(self):
        obj = {'commit': {}}
        obj['commit'].update(self.__dict__)
        return json.dumps(obj)

    def render_log(self):
        try:
            paths_changed = " %d paths changed" % len(self.changed)
        except:
            paths_changed = ""
        return "%s:%s repo '%s' id '%s'%s" % (self.type,
                                  self.format,
                                  self.repository,
                                  self.id,
                                  paths_changed)


HEARTBEAT_TIME = 15

class Client(object):
    def __init__(self, pubsub, r, type, repository):
        self.pubsub = pubsub
        r.notifyFinish().addErrback(self.finished)
        self.r = r
        self.type = type
        self.repository = repository
        self.alive = True
        log.msg("OPEN: %s:%d (%d clients online)"% (r.getClientIP(), r.client.port, pubsub.cc()+1))

    def finished(self, reason):
        self.alive = False
        log.msg("CLOSE: %s:%d (%d clients online)"% (self.r.getClientIP(), self.r.client.port, self.pubsub.cc()))
        try:
            self.pubsub.remove(self)
        except ValueError:
            pass

    def interested_in(self, commit):
        if self.type and self.type != commit.type:
            return False

        if self.repository and self.repository != commit.repository:
            return False

        return True

    def notify(self, data):
        self.write(data)

    def start(self):
        self.write_start()
        reactor.callLater(HEARTBEAT_TIME, self.heartbeat, None)

    def heartbeat(self, args):
        if self.alive:
            self.write_heartbeat()
            reactor.callLater(HEARTBEAT_TIME, self.heartbeat, None)

    def write_data(self, data):
        self.write(data + "\n\0")

    """ "Data must not be unicode" is what the interfaces.ITransport says... grr. """
    def write(self, input):
        self.r.write(str(input))

    def write_start(self):
        self.r.setHeader('X-SVNPubSub-Version', '1')
        self.r.setHeader('content-type', 'application/vnd.apache.vc-notify+json')
        self.write('{"svnpubsub": {"version": 1}}\n\0')

    def write_heartbeat(self):
        self.write(json.dumps({"stillalive": time.time()}) + "\n\0")


class SvnPubSub(resource.Resource):
    isLeaf = True
    clients = []

    def cc(self):
        return len(self.clients)

    def remove(self, c):
        self.clients.remove(c)

    def render_GET(self, request):
        log.msg("REQUEST: %s"  % (request.uri))
        request.setHeader('content-type', 'text/plain')

        repository = None
        type = None

        uri = request.uri.split('/')
        uri_len = len(uri)
        if uri_len < 2 or uri_len > 4:
            request.setResponseCode(400)
            return "Invalid path\n"

        if uri_len >= 3:
          type = uri[2]

        if uri_len == 4:
          repository = uri[3]

        # Convert wild card to None.
        if type == '*':
          type = None
        if repository == '*':
          repository = None

        c = Client(self, request, type, repository)
        self.clients.append(c)
        c.start()
        return twisted.web.server.NOT_DONE_YET

    def notifyAll(self, commit):
        data = commit.render_commit()

        log.msg("COMMIT: %s (%d clients)" % (commit.render_log(), self.cc()))
        for client in self.clients:
            if client.interested_in(commit):
                client.write_data(data)

    def render_PUT(self, request):
        request.setHeader('content-type', 'text/plain')
        ip = request.getClientIP()
        if ip != "127.0.0.1":
            request.setResponseCode(401)
            return "Access Denied"
        input = request.content.read()
        #import pdb;pdb.set_trace()
        #print "input: %s" % (input)
        try:
            c = json.loads(input)
            commit = Commit(c)
        except ValueError as e:
            request.setResponseCode(400)
            log.msg("COMMIT: failed due to: %s" % str(e))
            return str(e)
        self.notifyAll(commit)
        return "Ok"

def svnpubsub_server():
    root = resource.Resource()
    s = SvnPubSub()
    root.putChild("commits", s)
    return server.Site(root)

if __name__ == "__main__":
    log.startLogging(sys.stdout)
    # Port 2069 "HTTP Event Port", whatever, sounds good to me
    reactor.listenTCP(2069, svnpubsub_server())
    reactor.run()

