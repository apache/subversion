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
Generator of signed advisory mails
"""

from __future__ import absolute_import


class Mailer(object):
    """
    Constructs signed PGP/MIME advisory mails.
    """

    def __init__(self, notification):
        assert len(notification) > 0
        self.__notification = notification

    def __subject(self):
        """
        Construct a subject line for the notification mail.
        """

        template = ('Confidential pre-notification of'
                    ' {multiple}Subversion {culprit}{vulnerability}')

        # Construct the {culprit} replacement value. If all advisories
        # are either about the server or the client, use the
        # appropriate value; for mixed server/client advisories, use
        # an empty string.
        culprit = set()
        for advisory in self.__notification:
            culprit |= advisory.culprit
        assert len(culprit) > 0
        if len(culprit) > 1:
            culprit = ''
        elif self.__notification.Metadata.CULPRIT_CLIENT in culprit:
            culprit = 'client '
        elif self.__notification.Metadata.CULPRIT_SERVER in culprit:
            culprit = 'server '
        else:
            raise ValueError('Unknown culprit ' + repr(culprit))

        # Construct the format parameters
        if len(self.__notification) > 1:
            kwargs = dict(multiple='multiple ', culprit=culprit,
                          vulnerability='vulnerabilities')
        else:
            kwargs = dict(multiple='', culprit=culprit,
                          vulnerability='vulnerability')

        return template.format(**kwargs)
