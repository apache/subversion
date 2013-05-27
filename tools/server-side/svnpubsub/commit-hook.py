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
try:
    import simplejson as json
except ImportError:
    import json

import urllib2

import svnpubsub.util

def svnlook(cmd, **kwargs):
    args = [SVNLOOK] + cmd
    return svnpubsub.util.check_output(args, **kwargs)

def svnlook_uuid(repo):
    cmd = ["uuid", "--", repo]
    return svnlook(cmd).strip()

def svnlook_info(repo, revision):
    cmd = ["info", "-r", revision, "--", repo]
    data = svnlook(cmd, universal_newlines=True).split("\n")
    #print data
    return {'author': data[0].strip(),
            'date': data[1].strip(),
            'log': "\n".join(data[3:]).strip()}

def svnlook_changed(repo, revision):
    cmd = ["changed", "-r", revision, "--", repo]
    lines = svnlook(cmd, universal_newlines=True).split("\n")
    changed = {}
    for line in lines:
        line = line.strip()
        if not line:
            continue
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
    i = svnlook_info(repo, revision)
    data = {'type': 'svn',
            'format': 1,
            'id': int(revision),
            'changed': {},
            'repository': svnlook_uuid(repo),
            'committer': i['author'],
            'log': i['log'],
            'date': i['date'],
            }
    data['changed'].update(svnlook_changed(repo, revision))
    body = json.dumps(data)
    do_put(body)

if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.stderr.write("invalid args\n")
        sys.exit(1)

    main(*sys.argv[1:3])
