#!/usr/bin/env python
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
#
# strip-po-charset.py
#

import sys

def strip_po_charset(inp, out):

    out.write(inp.read().replace("\"Content-Type: text/plain; charset=UTF-8\\n\"\n",""))

def main():

    if len(sys.argv) != 3:
        print("Usage: %s <input (po) file> <output (spo) file>" % sys.argv[0])
        print("")
        print("Unsupported number of arguments; 2 required.")
        sys.exit(1)

    strip_po_charset(open(sys.argv[1],'r'), open(sys.argv[2],'w'))

if __name__ == '__main__':
    main()
