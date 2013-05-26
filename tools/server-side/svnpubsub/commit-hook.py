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
    return subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)

def svncmd_uuid(repo):
    cmd = "%s uuid %s" % (SVNLOOK, repo)
    p = svncmd(cmd)
    return p.stdout.read().strip()

def svncmd_info(repo, revision):
    cmd = "%s info -r %s %s" % (SVNLOOK, revision, repo)
    p = svncmd(cmd)
    data = p.stdout.read().split("\n")
    #print data
    return {'author': data[0].strip(),
            'date': data[1].strip(),
            'log': "\n".join(data[3:]).strip()}

def svncmd_changed(repo, revision):
    cmd = "%s changed -r %s %s" % (SVNLOOK, revision, repo)
    p = svncmd(cmd)
    changed = {}
    while True:
        line = p.stdout.readline()
        if not line:
            break
        line = line.strip()
        (flags, filename) = (line[0:3], line[4:])
        changed[filename] = {'flags': flags}
    return changed

def do_put(body):
    opener = urllib2.build_opener(urllib2.HTTPHandler)
    request = urllib2.Request("http://%s:%d/commits" %(HOST, PORT), data=body)
    request.add_header('Content-Type', 'application/json')
    request.get_method = lambda: 'PUT'
    url = opener.open(request)


def main(repo, revision):
    revision = revision.lstrip('r')
    i = svncmd_info(repo, revision)
    data = {'type': 'svn',
            'format': 1,
            'id': int(revision),
            'changed': {},
            'repository': svncmd_uuid(repo),
            'committer': i['author'],
            'log': i['log'],
            'date': i['date'],
            }
    data['changed'].update(svncmd_changed(repo, revision))
    body = json.dumps(data)
    do_put(body)

if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.stderr.write("invalid args\n")
        sys.exit(0)

    main(*sys.argv[1:3])
