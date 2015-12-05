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

"""
Parser for advisory e-mail distribution addresses
"""

from __future__ import absolute_import


import os
import re


class MailingList(object):
    """
    A list of e-mail addresses for security advisory pre-notifications.
    Parses ^/pmc/subversion/security/pre-notifications.txt
    """

    __ADDRESS_LINE = re.compile(r'^\s{6}(?:[^<]+)?<[^<>]+>\s*$')

    def __init__(self, mailing_list):
        self.__addresses = []
        self.__parse_addresses(mailing_list)

    def __iter__(self):
        return self.__addresses.__iter__()

    def __len__(self):
        return len(self.__addresses)

    def __parse_addresses(self, mailing_list):
        with open(mailing_list, 'rt') as pn:
            for line in pn:
                m = self.__ADDRESS_LINE.match(line)
                if not m:
                    continue

                self.__addresses.append(line.strip())
