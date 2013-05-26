#!/usr/local/bin/python
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

SVNLOOK="/usr/local/svn-install/current/bin/svnlook"
#SVNLOOK="/usr/local/bin/svnlook"

HOST="127.0.0.1"
PORT=2069

import sys
import subprocess
try:
    import simplejson as json
except ImportError:
    import json

import urllib2

def svncmd(cmd):
    return subprocess.Popen(cmd, shell=False, stdout=subprocess.PIPE)

def svncmd_uuid(repo):
    cmd = "%s uuid %s" % (SVNLOOK, repo)
    p = svncmd(cmd)
    return p.stdout.read().strip()

def svncmd_revprop(repo, revision, propname):
    cmd = "%s propget -r %s --revprop %s %s" % (SVNLOOK, revision, repo, propname)
    p = svncmd(cmd)
    data = p.stdout.read()
    #print data
    return data

def do_put(body):
    opener = urllib2.build_opener(urllib2.HTTPHandler)
    request = urllib2.Request("http://%s:%d/metadata" %(HOST, PORT), data=body)
    request.add_header('Content-Type', 'application/json')
    request.get_method = lambda: 'PUT'
    url = opener.open(request)


def main(repo, revision, author, propname, action):
    revision = revision.lstrip('r')
    if action in ('A', 'M'):
        new_value = svncmd_revprop(repo, revision, propname)
    elif action == 'D':
        new_value = None
    else:
        sys.stderr.write('Unknown revprop change action "%s"\n' % action)
        return
    if action in ('D', 'M'):
        old_value = sys.stdin.read()
    else:
        old_value = None
    data = {'type': 'svn',
            'format': 1,
            'id': int(revision),
            'repository': svncmd_uuid(repo),
            'revprop': {
                'name': propname,
                'committer': author,
                'value': new_value,
                'old_value': old_value,
                }
            }
    body = json.dumps(data)
    do_put(body)

if __name__ == "__main__":
    if len(sys.argv) != 6:
        sys.stderr.write("invalid args\n")
        sys.exit(0)

    main(*sys.argv[1:6])
