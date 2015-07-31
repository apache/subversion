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

import email.utils

from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText

try:
    import gnupg
except ImportError:
    import security._gnupg as gnupg


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
        for metadata in self.__notification:
            culprit |= metadata.culprit
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
            kwargs = dict(multiple='a ', culprit=culprit,
                          vulnerability='vulnerability')

        return template.format(**kwargs)

    def __attachments(self):
        filenames = set()

        def attachment(filename, description, encoding, content):
            if filename in filenames:
                raise ValueError('Named attachment already exists: '
                                 + filename)
            filenames.add(filename)

            att = MIMEText('', 'plain', 'utf-8')
            att.set_param('name', filename)
            att.replace_header('Content-Transfer-Encoding', encoding)
            att.add_header('Content-Description', description)
            att.add_header('Content-Disposition',
                            'attachment', filename=filename)
            att.set_payload(content)
            return att

        for metadata in self.__notification:
            filename = metadata.tracking_id + '-advisory.txt'
            description = metadata.tracking_id + ' Advisory'
            yield attachment(filename, description, 'quoted-printable',
                             metadata.advisory.quoted_printable)

            for patch in metadata.patches:
                filename = (metadata.tracking_id +
                            '-' + patch.base_version + '.patch')
                description = (metadata.tracking_id
                               + ' Patch for Subversion ' + patch.base_version)
                yield attachment(filename, description, 'base64', patch.base64)


class SignedMessage(MIMEMultipart):
    """
    The signed PGP/MIME message.
    """

    def __init__(self, message, attachments,
                 gpgbinary='gpg', gnupghome=None, use_agent=True,
                 keyring=None, keyid=None):

        # Hack around the fact that the Pyton 2.x MIMEMultipart is not
        # a new-style class.
        try:
            unicode                       # Doesn't exist in Python 3
            MIMEMultipart.__init__(self, 'signed')
        except NameError:
            super(SignedMessage, self).__init__('signed')

        payload = self.__payload(message, attachments)
        signature = self.__signature(
            payload, gpgbinary, gnupghome, use_agent, keyring, keyid)

        self.set_param('protocol', 'application/pgp-signature')
        self.set_param('micalg', 'pgp-sha512')   ####!!!
        self.preamble = 'This is an OpenPGP/MIME signed message.'
        self.attach(payload)
        self.attach(signature)

    def __payload(self, message, attachments):
        """
        Create the payload from the given MESSAGE and a
        set of pre-cooked ATTACHMENTS.
        """

        payload = MIMEMultipart()
        payload.preamble = 'This is a multi-part message in MIME format.'

        msg = MIMEText('', 'plain', 'utf-8')
        msg.replace_header('Content-Transfer-Encoding', 'quoted-printable')
        msg.set_payload(message.quoted_printable)
        payload.attach(msg)

        for att in attachments:
            payload.attach(att)
        return payload

    def __signature(self, payload,
                    gpgbinary, gnupghome, use_agent, keyring, keyid):
        """
        Sign the PAYLOAD and return the detached signature as
        a MIME attachment.
        """

        gpg = gnupg.GPG(gpgbinary=gpgbinary, gnupghome=gnupghome,
                        use_agent=use_agent, keyring=keyring)
        signature = gpg.sign(payload.as_string(),
                             keyid=keyid, detach=True, clearsign=False)
        sig = MIMEText('')
        sig.set_type('application/pgp-signature')
        sig.set_charset(None)
        sig.set_param('name', 'signature.asc')
        sig.add_header('Content-Description', 'OpenPGP digital signature')
        sig.add_header('Content-Disposition',
                       'attachment', filename='signature.asc')
        sig.set_payload(str(signature))
        return sig
