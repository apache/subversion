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
# Generic client for SvnPubSub
#
# ### usage...
#
#
# EVENTS
#
#   connected: a connection to the server has been opened (though not
#                 necessarily established)
#   closed:    the connection was closed. reconnect will be attempted.
#   error:     an error closed the connection. reconnect will be attempted.
#   ping:      the server has sent a keepalive
#   stale:     no activity has been seen, so the connection will be closed
#                 and reopened
#

import asyncore
import asynchat
import socket
import functools
import time
import xml.sax

# How long the polling loop should wait for activity before returning.
TIMEOUT = 30.0

# Always delay a bit when trying to reconnect. This is not precise, but sets
# a minimum amount of delay. At the moment, there is no further backoff.
RECONNECT_DELAY = 25.0

# If we don't see anything from the server for this amount time, then we
# will drop and reconnect. The TCP connection may have gone down without
# us noticing it somehow.
STALE_DELAY = 60.0


class Client(asynchat.async_chat):

  def __init__(self, host, port, commit_callback, event_callback):
    asynchat.async_chat.__init__(self)

    self.last_activity = time.time()

    self.host = host
    self.port = port
    self.event_callback = event_callback

    handler = XMLStreamHandler(commit_callback, event_callback)

    self.parser = xml.sax.make_parser(['xml.sax.expatreader'])
    self.parser.setContentHandler(handler)

    # Wait for the end of headers. Then we start parsing XML.
    self.set_terminator('\r\n\r\n')
    self.skipping_headers = True

    self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
      self.connect((host, port))
    except:
      self.handle_error()
      return
        
    ### should we allow for repository restrictions?
    self.push('GET /commits/xml HTTP/1.0\r\n\r\n')

  def handle_connect(self):
    self.event_callback('connected')

  def handle_close(self):
    self.event_callback('closed')
    self.close()

  def handle_error(self):
    self.event_callback('error')
    self.close()

  def found_terminator(self):
    self.skipping_headers = False

    # From here on, collect everything. Never look for a terminator.
    self.set_terminator(None)

  def collect_incoming_data(self, data):
    # Remember the last time we saw activity
    self.last_activity = time.time()

    if not self.skipping_headers:
      # Just shove this into the XML parser. As the elements are processed,
      # we'll collect them into an appropriate structure, and then invoke
      # the callback when we have fully received a commit.
      self.parser.feed(data)


class XMLStreamHandler(xml.sax.handler.ContentHandler):

  def __init__(self, commit_callback, event_callback):
    self.commit_callback = commit_callback
    self.event_callback = event_callback

    self.rev = None
    self.chars = ''

  def startElement(self, name, attrs):
    if name == 'commit':
      self.rev = Revision(attrs['repository'], int(attrs['revision']))
    # No other elements to worry about.

  def characters(self, data):
    self.chars += data

  def endElement(self, name):
    if name == 'commit':
      self.commit_callback(self.rev)
      self.rev = None
    elif name == 'stillalive':
      self.event_callback('ping')
    elif self.chars and self.rev:
      value = self.chars.strip()
      if name == 'path':
        self.rev.dirs_changed.append(value)
      elif name == 'author':
        self.rev.author = value
      elif name == 'date':
        self.rev.date = value
      elif name == 'log':
        self.rev.log = value

    # Toss out any accumulated characters for this element.
    self.chars = ''


class Revision(object):
  def __init__(self, uuid, rev):
    self.uuid = uuid
    self.rev = rev
    self.dirs_changed = [ ]
    self.author = None
    self.date = None
    self.log = None


class MultiClient(object):
  def __init__(self, hostports, commit_callback, event_callback):
    self.commit_callback = commit_callback
    self.event_callback = event_callback

    # No target time, as no work to do
    self.target_time = 0
    self.work_items = [ ]

    for host, port in hostports:
      self._add_channel(host, port)

  def _reconnect(self, host, port, event_name):
    if event_name == 'closed' or event_name == 'error':
      # Stupid connection closed for some reason. Set up a reconnect. Note
      # that it should have been removed from asyncore.socket_map already.
      self._reconnect_later(host, port)

    # Call the user's callback now.
    self.event_callback(host, port, event_name)

  def _reconnect_later(self, host, port):
    # Set up a work item to reconnect in a little while.
    self.work_items.append((host, port))

    # Only set a target if one has not been set yet. Otherwise, we could
    # create a race condition of continually moving out towards the future
    if not self.target_time:
      self.target_time = time.time() + RECONNECT_DELAY

  def _add_channel(self, host, port):
    # Simply instantiating the client will install it into the global map
    # for processing in the main event loop.
    Client(host, port,
           functools.partial(self.commit_callback, host, port),
           functools.partial(self._reconnect, host, port))

  def _check_stale(self):
    now = time.time()
    for client in asyncore.socket_map.values():
      if client.last_activity + STALE_DELAY < now:
        # Whoops. No activity in a while. Signal this fact, Close the
        # Client, then have it reconnected later on.
        self.event_callback(client.host, client.port, 'stale')

        # This should remove it from .socket_map.
        client.close()

        self._reconnect_later(client.host, client.port)

  def _maybe_work(self):
    # If we haven't reach the targetted time, or have no work to do,
    # then fast-path exit
    if time.time() < self.target_time or not self.work_items:
      return

    # We'll take care of all the work items, so no target for future work
    self.target_time = 0

    # Play a little dance just in case work gets added while we're
    # currently working on stuff
    work = self.work_items
    self.work_items = [ ]

    for host, port in work:
      self._add_channel(host, port)

  def run_forever(self):
    while True:
      if asyncore.socket_map:
        asyncore.loop(timeout=TIMEOUT, count=1)
      else:
        time.sleep(TIMEOUT)

      self._check_stale()
      self._maybe_work()
