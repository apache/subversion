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
# A simple test server for responding in different ways to SvnPubSub clients.
# This avoids the complexity of the Twisted framework in order to direct
# various (abnormal) conditions at the client.
#
# ### usage...
#

import sys
import BaseHTTPServer


PORT = 2069

TEST_BODY = '{"svnpubsub": {"version": 1}}\n\0{"commit": {"type": "svn", "format": 1, "repository": "12345678-1234-1234-1234-123456789012", "id": "1234", "committer": "johndoe", "date": "2012-01-01 01:01:01 +0000 (Sun, 01 Jan 2012)", "log": "Frob the ganoozle with the snookish", "changed": {"one/path/alpha": {"flags": "U  "}, "some/other/directory/": {"flags": "_U "}}}}\n\0'

SEND_KEEPALIVE = True


class TestHandler(BaseHTTPServer.BaseHTTPRequestHandler):
  def do_GET(self):
    self.send_response(200)
    self.send_header('Content-Length', str(len(TEST_BODY)))
    self.send_header('Connection', 'keep-alive')
    self.end_headers()
    self.wfile.write(TEST_BODY)


if __name__ == '__main__':
  server = BaseHTTPServer.HTTPServer(('', PORT), TestHandler)
  sys.stderr.write('Now listening on port %d...\n' % (PORT,))
  server.serve_forever()
