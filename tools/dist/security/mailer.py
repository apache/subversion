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

import re
import uuid
import hashlib
import smtplib
import textwrap
import email.utils

from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText

try:
    import gnupg
except ImportError:
    import security._gnupg as gnupg

import security.parser


class Mailer(object):
    """
    Constructs signed PGP/MIME advisory mails.
    """

    def __init__(self, notification, sender, message_template,
                 release_date, dist_revision, *release_versions):
        assert len(notification) > 0
        self.__sender = sender
        self.__notification = notification
        self.__message_content = self.__message_content(
            message_template, release_date, dist_revision, release_versions)

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

    def __message_content(self, message_template,
                          release_date, dist_revision, release_versions):
        """
        Construct the message from the notification mail template.
        """

        # Construct the replacement arguments for the notification template
        culprits = set()
        advisories = []
        base_version_keys = self.__notification.base_version_keys()
        for metadata in self.__notification:
            culprits |= metadata.culprit
            advisories.append(
                '  * {}\n    {}'.format(metadata.tracking_id, metadata.title))
        release_version_keys = set(security.parser.Patch.split_version(n)
                                   for n in release_versions)

        multi = (len(self.__notification) > 1)
        kwargs = dict(multiple=(multi and 'multiple ' or 'a '),
                      alert=(multi and 'alerts' or 'alert'),
                      culprits=self.__culprits(culprits),
                      advisories='\n'.join(advisories),
                      release_date=release_date.strftime('%d %B %Y'),
                      release_day=release_date.strftime('%d %B'),
                      base_versions = self.__versions(base_version_keys),
                      release_versions = self.__versions(release_version_keys),
                      dist_revision=str(dist_revision))

        # Parse, interpolate and rewrap the notification template
        wrapped = []
        content = security.parser.Text(message_template)
        for line in content.text.format(**kwargs).split('\n'):
            if len(line) > 0 and not line[0].isspace():
                for part in textwrap.wrap(line,
                                          break_long_words=False,
                                          break_on_hyphens=False):
                    wrapped.append(part)
            else:
                wrapped.append(line)
        return security.parser.Text(None, '\n'.join(wrapped).encode('utf-8'))

    def __versions(self, versions):
        """
        Return a textual representation of the set of VERSIONS
        suitable for inclusion in a notification mail.
        """

        text = tuple(security.parser.Patch.join_version(n)
                     for n in sorted(versions))
        assert len(text) > 0
        if len(text) == 1:
            return text[0]
        elif len(text) == 2:
            return ' and '.join(text)
        else:
            return ', '.join(text[:-1]) + ' and ' + text[-1]

    def __culprits(self, culprits):
        """
        Return a textual representation of the set of CULPRITS
        suitable for inclusion in a notification mail.
        """

        if self.__notification.Metadata.CULPRIT_CLIENT in culprits:
            if self.__notification.Metadata.CULPRIT_SERVER in culprits:
                return 'clients and servers'
            else:
                return 'clients'
        elif self.__notification.Metadata.CULPRIT_SERVER in culprits:
            return 'servers'
        else:
            raise ValueError('Unknown culprit ' + repr(culprits))

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

    def generate_message(self):
        message = SignedMessage(
            self.__message_content,
            self.__attachments())
        message['From'] = self.__sender
        message['Reply-To'] = self.__sender
        message['To'] = self.__sender     # Will be replaced later
        message['Subject'] = self.__subject()
        message['Date'] = email.utils.formatdate()

        # Try to make the message-id refer to the sender's domain
        address = email.utils.parseaddr(self.__sender)[1]
        if not address:
            domain = None
        else:
            domain = address.split('@')[1]
            if not domain:
                domain = None

        idstring = uuid.uuid1().hex
        try:
            msgid = email.utils.make_msgid(idstring, domain=domain)
        except TypeError:
            # The domain keyword was added in Python 3.2
            msgid = email.utils.make_msgid(idstring)
        message["Message-ID"] = msgid
        return message

    def send_mail(self, message, username, password, recipients=None,
                  host='mail-relay.apache.org', starttls=True, port=None):
        if not port and starttls:
            port = 587
        server = smtplib.SMTP(host, port)
        if starttls:
            server.starttls()
        if username and password:
            server.login(username, password)

        def send(message):
            # XXX: The from,to arguments should be bare addresses with no "foo:"
            #      prefix.  It works this way in practice, but that appears to
            #      be an accident of implementation of smtplib.
            server.sendmail("From: " + message['From'],
                            "To: " + message['To'],
                            message.as_string())

        if recipients is None:
            # Test mode, send message back to originator to checck
            # that contents and signature are OK.
            message.replace_header('To', message['From'])
            send(message)
        else:
            for recipient in recipients:
                message.replace_header('To', recipient)
                send(message)
        server.quit()


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
        self.set_param('micalg', 'pgp-sha512')   ####!!! GET THIS FROM KEY!
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

        # RFC3156 section 5 says line endings in the signed message
        # must be canonical <CR><LF>.
        cleartext = re.sub(r'\r?\n', '\r\n', payload.as_string())

        gpg = gnupg.GPG(gpgbinary=gpgbinary, gnupghome=gnupghome,
                        use_agent=use_agent, keyring=keyring)
        signature = gpg.sign(cleartext,
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
