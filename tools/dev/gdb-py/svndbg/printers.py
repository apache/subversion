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

import gdb
import re

import gdb.printing


class SvnStringPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        # Make sure string * works, too
        val = self.val

        ptr = val['data']
        len = val['len']

        return "length: " + str(int(len)) + "; contents: '" + ptr.string(length=len) + "'"

    def display_hint(self):
        return 'string'


def build_libsvn_printer():
    global libsvn_printer

    libsvn_printer = gdb.printing.RegexpCollectionPrettyPrinter("libsvn")

    libsvn_printer.add_printer('svn_string_t', '^svn_string_t$',
                               SvnStringPrinter)


libsvn_printer = None

def register_libsvn_printers(obj):
    global libsvn_printer

    gdb.printing.register_pretty_printer(obj, libsvn_printer)


build_libsvn_printer()
