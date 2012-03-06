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

import sys
import os
from twisted.application import service, internet
from twisted.internet import threads

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import svnwcsub

application = service.Application("SvnWcSub")

def get_service():
    m = service.MultiService()
    c = svnwcsub.ReloadableConfig("/etc/svnwcsub.conf")
    bdec = svnwcsub.BigDoEverythingClasss(c, service=m)
    # Start the BDEC on a background thread, after twisted is up and running
    threads.deferToThread(bdec.start)
    return m

service = get_service()
service.setServiceParent(application)


### crazy hack. the first time Twisted logs something, we will track down
### the logfile it is using, then use that for the Python logging framework
from twisted.python import log
import logging

def capture_log(unused_msg):
    for ob in log.theLogPublisher.observers:
        if hasattr(ob, 'im_class') and ob.im_class is log.FileLogObserver:
            flo = ob.im_self
            logfile = flo.write.im_self
            stream = logfile._file

            ### the follow is similar to svnwcsub.prepare_logging()
            handler = logging.StreamHandler(stream)
            formatter = logging.Formatter(
                            '%(asctime)s [%(levelname)s] %(message)s',
                            '%Y-%m-%d %H:%M:%S')
            handler.setFormatter(formatter)

            # Apply the handler to the root logger
            root = logging.getLogger()
            root.addHandler(handler)

            ### use logging.INFO for now
            root.setLevel(logging.INFO)

            # okay. remove our capture function.
            log.removeObserver(capture_log)

# hook in the capturing...
log.addObserver(capture_log)
