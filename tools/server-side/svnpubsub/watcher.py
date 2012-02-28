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
# Watch for events from SvnPubSub and print them to stdout
#
# ### usage...
#

import sys
import urlparse
import pprint

import svnpubsub.client
import svnwcsub  ### for ReloadableConfig


def _commit(host, port, rev):
  print 'COMMIT: from %s:%s' % (host, port)
  pprint.pprint(vars(rev), indent=2)


def _event(host, port, event_name):
  print 'EVENT: from %s:%s "%s"' % (host, port, event_name)


def main(config_file):
  config = svnwcsub.ReloadableConfig(config_file)
  hostports = [ ]
  for url in config.get_value('streams').split():
    parsed = urlparse.urlparse(url)
    hostports.append((parsed.hostname, parsed.port))

  mc = svnpubsub.client.MultiClient(hostports, _commit, _event)
  mc.run_forever()


if __name__ == "__main__":
  if len(sys.argv) != 2:
    print "invalid args, read source code"
    sys.exit(0) 
  main(sys.argv[1])
