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
#   curl  -i http://127.0.0.1:2069/dirs-changed/xml
#   curl  -i http://127.0.0.1:2069/dirs-changed/json
#   curl  -i http://127.0.0.1:2069/commits/json
#   curl  -i http://127.0.0.1:2069/commits/13f79535-47bb-0310-9956-ffa450edef68/json
#   curl  -i http://127.0.0.1:2069/dirs-changed/13f79535-47bb-0310-9956-ffa450edef68/json
#
#   URL is built into 3 parts:
#       /${type}/${optional_repo_uuid}/${format}
#
#   If the repository UUID is included in the URl, you will only receive
#   messages about that repository.
#
# Example Pub clients:
#   curl -T revinfo.json -i http://127.0.0.1:2069/commit
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
from twisted.web import server, static
from twisted.web import resource
from twisted.python import log

try:
    from xml.etree import cElementTree as ET
except:
    from xml.etree import ElementTree as ET
import time

class Revision:
    def __init__(self, r):
        # Don't escape the values; json handles binary values fine.
        # ET will happily emit literal control characters (eg, NUL),
        # thus creating invalid XML, so the XML code paths do escaping.
        self.rev = r.get('revision')
        self.repos = r.get('repos')
        self.dirs_changed = [x for x in r.get('dirs_changed')]
        self.author = r.get('author')
        self.log = r.get('log')
        self.date = r.get('date')

    def render_commit(self, format):
        if format == "json":
            return json.dumps({'commit': {'repository': self.repos,
                                          'revision': self.rev,
                                          'dirs_changed': self.dirs_changed,
                                          'author': self.author,
                                          'log': self.log,
                                          'date': self.date}}) +","
        elif format == "xml":
            c = ET.Element('commit', {'repository': self.repos, 'revision': "%d" % (self.rev)})
            ET.SubElement(c, 'author').text = self.author.encode('unicode_escape')
            ET.SubElement(c, 'date').text = self.date.encode('unicode_escape')
            ET.SubElement(c, 'log').text = self.log.encode('unicode_escape')
            d = ET.SubElement(c, 'dirs_changed')
            for p in self.dirs_changed:
                x = ET.SubElement(d, 'path')
                x.text = p.encode('unicode_escape')
            str = ET.tostring(c, 'UTF-8') + "\n"
            return str[39:]
        else:
            raise Exception("Ooops, invalid format")

    def render_dirs_changed(self, format):
        if format == "json":
            return json.dumps({'commit': {'repository': self.repos,
                                          'revision': self.rev,
                                          'dirs_changed': self.dirs_changed}}) +","
        elif format == "xml":
            c = ET.Element('commit', {'repository': self.repos, 'revision': "%d" % (self.rev)})
            d = ET.SubElement(c, 'dirs_changed')
            for p in self.dirs_changed:
                x = ET.SubElement(d, 'path')
                x.text = p.encode('unicode_escape')
            str = ET.tostring(c, 'UTF-8') + "\n"
            return str[39:]
        else:
            raise Exception("Ooops, invalid format")

HEARTBEAT_TIME = 15

class Client(object):
    def __init__(self, pubsub, r, repos, fmt):
        self.pubsub = pubsub
        r.notifyFinish().addErrback(self.finished)
        self.r = r
        self.format = fmt
        self.repos = repos
        self.alive = True
        log.msg("OPEN: %s:%d (%d clients online)"% (r.getClientIP(), r.client.port, pubsub.cc()+1))

    def finished(self, reason):
        self.alive = False
        log.msg("CLOSE: %s:%d (%d clients online)"% (self.r.getClientIP(), self.r.client.port, self.pubsub.cc()))
        try:
            self.pubsub.remove(self)
        except ValueError:
            pass

    def interested_in(self, uuid):
        if self.repos is None:
            return True
        if uuid == self.repos:
            return True
        return False

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
        self.write(data[self.format] + "\n")

    """ "Data must not be unicode" is what the interfaces.ITransport says... grr. """
    def write(self, input):
        self.r.write(str(input))

class JSONClient(Client):
    def write_start(self):
        self.r.setHeader('content-type', 'application/json')
        self.write('{"commits": [\n')

    def write_heartbeat(self):
        self.write(json.dumps({"stillalive": time.time()}) + ",\n")

class XMLClient(Client):
    def write_start(self):
        self.r.setHeader('content-type', 'application/xml')
        self.write("<?xml version='1.0' encoding='UTF-8'?>\n<commits>")

    def write_heartbeat(self):
        self.write("<stillalive>%f</stillalive>\n" % (time.time()))

class SvnPubSub(resource.Resource):
    isLeaf = True
    clients = {'commits': [],
               'dirs-changed': []}

    def cc(self):
        return reduce(lambda x,y: len(x)+len(y), self.clients.values())

    def remove(self, c):
        for k in self.clients.keys():
            self.clients[k].remove(c)

    def render_GET(self, request):
        log.msg("REQUEST: %s"  % (request.uri))
        uri = request.uri.split('/')

        request.setHeader('content-type', 'text/plain')
        if len(uri) != 3 and len(uri) != 4:
            request.setResponseCode(400)
            return "Invalid path\n"

        uuid = None
        fmt = None
        type = uri[1]

        if len(uri) == 3:
            fmt = uri[2]
        else:
            fmt = uri[3]
            uuid = uri[2]

        if type not in self.clients.keys():
            request.setResponseCode(400)
            return "Invalid Reuqest Type\n"

        clients = {'json': JSONClient, 'xml': XMLClient}
        clientCls = clients.get(fmt)
        if clientCls == None:
            request.setResponseCode(400)
            return "Invalid Format Requested\n"

        c = clientCls(self, request, uuid, fmt)
        self.clients[type].append(c)
        c.start()
        return twisted.web.server.NOT_DONE_YET

    def notifyAll(self, rev):
        data = {'commits': {},
               'dirs-changed': {}}
        for x in ['xml', 'json']:
            data['commits'][x] = rev.render_commit(x)
            data['dirs-changed'][x] = rev.render_dirs_changed(x)

        log.msg("COMMIT: r%d in %d paths (%d clients)" % (rev.rev,
                                                        len(rev.dirs_changed),
                                                        self.cc()))
        for k in self.clients.keys():
            for c in self.clients[k]:
                if c.interested_in(rev.repos):
                    c.write_data(data[k])

    def render_PUT(self, request):
        request.setHeader('content-type', 'text/plain')
        ip = request.getClientIP()
        if ip != "127.0.0.1":
            request.setResponseCode(401)
            return "Access Denied"
        input = request.content.read()
        #import pdb;pdb.set_trace()
        #print "input: %s" % (input)
        r = json.loads(input)
        rev = Revision(r)
        self.notifyAll(rev)
        return "Ok"

def svnpubsub_server():
    root = static.File("/dev/null")
    s = SvnPubSub()
    root.putChild("dirs-changed", s)
    root.putChild("commits", s)
    root.putChild("commit", s)
    return server.Site(root)

if __name__ == "__main__":
    log.startLogging(sys.stdout)
    # Port 2069 "HTTP Event Port", whatever, sounds good to me
    reactor.listenTCP(2069, svnpubsub_server())
    reactor.run()

