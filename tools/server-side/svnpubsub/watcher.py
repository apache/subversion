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
#

import sys
import pprint
try:
  import urlparse
except ImportError:
  import urllib.parse as urlparse

import svnpubsub.client


def _commit(url, commit):
  print('COMMIT: from %s' % url)
  pprint.pprint(vars(commit), indent=2)

def _metadata(url, metadata):
  print('METADATA: from %s' % url)
  pprint.pprint(vars(metadata), indent=2)

def _event(url, event_name, event_arg):
  if event_arg:
    print('EVENT: from %s "%s" "%s"' % (url, event_name, event_arg))
  else:
    print('EVENT: from %s "%s"' % (url, event_name))


def main(urls):
  mc = svnpubsub.client.MultiClient(urls, _commit, _event, _metadata)
  mc.run_forever()


if __name__ == "__main__":
  if len(sys.argv) < 2:
    print("usage: watcher.py URL [URL...]")
    sys.exit(0)
  main(sys.argv[1:])
