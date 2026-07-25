#!/bin/sh
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#

# Required version of Python
VERSION=${1:-0x2070000}

for pypath in "$PYTHON" "$PYTHON2" "$PYTHON3" python python2 python3; do
  if [ "x$pypath" != "x" ]; then
    DETECT_PYTHON="import sys;sys.exit((sys.hexversion < $VERSION) and 1 or 0)"
    if "$pypath" -c "$DETECT_PYTHON" >/dev/null 2>/dev/null; then
      echo $pypath
      exit 0
    fi
  fi
done
exit 1
