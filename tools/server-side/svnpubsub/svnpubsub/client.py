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
import json
try:
  import urlparse
except ImportError:
  import urllib.parse as urlparse

# How long the polling loop should wait for activity before returning.
TIMEOUT = 30.0

# Always delay a bit when trying to reconnect. This is not precise, but sets
# a minimum amount of delay. At the moment, there is no further backoff.
RECONNECT_DELAY = 25.0

# If we don't see anything from the server for this amount time, then we
# will drop and reconnect. The TCP connection may have gone down without
# us noticing it somehow.
STALE_DELAY = 60.0


class SvnpubsubClientException(Exception):
  pass

class Client(asynchat.async_chat):

  def __init__(self, url, commit_callback, event_callback,
               metadata_callback = None):
    asynchat.async_chat.__init__(self)

    self.last_activity = time.time()
    self.ibuffer = []

    self.url = url
    parsed_url = urlparse.urlsplit(url)
    if parsed_url.scheme != 'http':
      raise ValueError("URL scheme must be http: '%s'" % url)
    host = parsed_url.hostname
    port = parsed_url.port
    resource = parsed_url.path
    if parsed_url.query:
      resource += "?%s" % parsed_url.query
    if parsed_url.fragment:
      resource += "#%s" % parsed_url.fragment

    self.event_callback = event_callback

    self.parser = JSONRecordHandler(commit_callback, event_callback,
                                    metadata_callback)

    # Wait for the end of headers. Then we start parsing JSON.
    self.set_terminator(b'\r\n\r\n')
    self.skipping_headers = True

    self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
      self.connect((host, port))
    except:
      self.handle_error()
      return

    self.push(('GET %s HTTP/1.0\r\n\r\n' % resource).encode('ascii'))

  def handle_connect(self):
    self.event_callback('connected', None)

  def handle_close(self):
    self.event_callback('closed', None)
    self.close()

  def handle_error(self):
    self.event_callback('error', None)
    self.close()

  def found_terminator(self):
    if self.skipping_headers:
      self.skipping_headers = False
      # Each JSON record is terminated by a null character
      self.set_terminator(b'\0')
    else:
      record = b"".join(self.ibuffer)
      self.ibuffer = []
      self.parser.feed(record.decode())

  def collect_incoming_data(self, data):
    # Remember the last time we saw activity
    self.last_activity = time.time()

    if not self.skipping_headers:
      self.ibuffer.append(data)


class Notification(object):
  def __init__(self, data):
    self.__dict__.update(data)

class Commit(Notification):
  KIND = 'COMMIT'

class Metadata(Notification):
  KIND = 'METADATA'


class JSONRecordHandler:
  def __init__(self, commit_callback, event_callback, metadata_callback):
    self.commit_callback = commit_callback
    self.event_callback = event_callback
    self.metadata_callback = metadata_callback

  EXPECTED_VERSION = 1

  def feed(self, record):
    obj = json.loads(record)
    if 'svnpubsub' in obj:
      actual_version = obj['svnpubsub'].get('version')
      if actual_version != self.EXPECTED_VERSION:
        raise SvnpubsubClientException(
          "Unknown svnpubsub format: %r != %d"
          % (actual_version, self.EXPECTED_VERSION))
      self.event_callback('version', obj['svnpubsub']['version'])
    elif 'commit' in obj:
      commit = Commit(obj['commit'])
      self.commit_callback(commit)
    elif 'stillalive' in obj:
      self.event_callback('ping', obj['stillalive'])
    elif 'metadata' in obj and self.metadata_callback:
      metadata = Metadata(obj['metadata'])
      self.metadata_callback(metadata)


class MultiClient(object):
  def __init__(self, urls, commit_callback, event_callback,
               metadata_callback = None):
    self.commit_callback = commit_callback
    self.event_callback = event_callback
    self.metadata_callback = metadata_callback

    # No target time, as no work to do
    self.target_time = 0
    self.work_items = [ ]

    for url in urls:
      self._add_channel(url)

  def _reconnect(self, url, event_name, event_arg):
    if event_name == 'closed' or event_name == 'error':
      # Stupid connection closed for some reason. Set up a reconnect. Note
      # that it should have been removed from asyncore.socket_map already.
      self._reconnect_later(url)

    # Call the user's callback now.
    self.event_callback(url, event_name, event_arg)

  def _reconnect_later(self, url):
    # Set up a work item to reconnect in a little while.
    self.work_items.append(url)

    # Only set a target if one has not been set yet. Otherwise, we could
    # create a race condition of continually moving out towards the future
    if not self.target_time:
      self.target_time = time.time() + RECONNECT_DELAY

  def _add_channel(self, url):
    # Simply instantiating the client will install it into the global map
    # for processing in the main event loop.
    if self.metadata_callback:
      Client(url,
             functools.partial(self.commit_callback, url),
             functools.partial(self._reconnect, url),
             functools.partial(self.metadata_callback, url))
    else:
      Client(url,
             functools.partial(self.commit_callback, url),
             functools.partial(self._reconnect, url))

  def _check_stale(self):
    now = time.time()
    for client in asyncore.socket_map.values():
      if client.last_activity + STALE_DELAY < now:
        # Whoops. No activity in a while. Signal this fact, Close the
        # Client, then have it reconnected later on.
        self.event_callback(client.url, 'stale', client.last_activity)

        # This should remove it from .socket_map.
        client.close()

        self._reconnect_later(client.url)

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

    for url in work:
      self._add_channel(url)

  def run_forever(self):
    while True:
      if asyncore.socket_map:
        asyncore.loop(timeout=TIMEOUT, count=1)
      else:
        time.sleep(TIMEOUT)

      self._check_stale()
      self._maybe_work()
