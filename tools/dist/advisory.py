#!/usr/bin/env python
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
Send GPG-signed security advisory e-mails from an @apache.org address
to a known list of recipients, or write the advisory text in a form
suitable for publishing on http://subversion.apache.org/.

Usage: cd to the root directory of the advisory descriptions, then:

    $ ${TRUNK_WC}/tools/dist/advisory.py send \
          --username=<ASF-username> \
          --revision=<dist-dev-revision-number>
          --release-versions=<target-releases> \
          --release-date=<expected-release-date> <CVE-number>...

or

    $ ${TRUNK_WC}/tools/dist/advisory.py test \
          (... --username, etc. as above)

or

    $ ${TRUNK_WC}/tools/dist/advisory.py generate \
          --destination=${SITE_WC}/publish/security \
          <CVE-number>...
"""

from __future__ import absolute_import

import os
import sys
import argparse
import datetime
import getpass
import re

import security.parser
import security.adviser
import security.mailer
import security.mailinglist

ROOTDIR = os.path.abspath(os.getcwd())
NOTICE_TEMPLATE = 'notice-template.txt'
MAILING_LIST = 'pre-notifications.txt'


def parse_args(argv):
    parser = argparse.ArgumentParser(
        prog=os.path.basename(__file__), add_help=True,
        description="""\
Send GPG-signed security advisory e-mails from an @apache.org address
to a known list of recipients, or write the advisory text in a form
suitable for publishing on http://subversion.apache.org/.
""")
    parser.add_argument(
        'command', action='store',
        choices=['send', 'test', 'generate'],
        help=('send: send mail; '
              'test: write the mail to standard output; '
              'generate: write an advisory for the website'))
    parser.add_argument(
        '--username', action='store', required=False,
        help='the @apache.org username of the sender')
    parser.add_argument(
        '--revision', action='store', required=False, type=int,
        help=('revision on dist.a.o./repos/dist/dev/subversion '
              'in which the patched tarballs are available'))
    parser.add_argument(
        '--release-versions', action='store', required=False,
        help=('comma-separated list of future released versions '
              'that will contain the fix(es)'))
    parser.add_argument(
        '--release-date', action='store', required=False,
        help=('expected release date for the above mentioned'
              ' versions (in ISO format, YYYY-MM-DD)'))
    parser.add_argument(
        '--destination', action='store', required=False,
        help=('the directory where the website advisory should be '
              'written; usually ${SITE_WC}/publish/security'))
    parser.add_argument('cve', nargs='+')

    return parser.parse_args(argv)


def check_root():
    if not os.path.isfile(os.path.join(ROOTDIR, NOTICE_TEMPLATE)):
        sys.stderr.write('Missing file: ' + NOTICE_TEMPLATE + '\n')
        sys.exit(1)
    if not os.path.isfile(os.path.join(ROOTDIR, MAILING_LIST)):
        sys.stderr.write('Missing file: ' + MAILING_LIST + '\n')
        sys.exit(1)


def check_sendmail(args):
    if (not (args.username and args.revision
             and args.release_versions
             and args.release_date and args.cve)
        or args.destination):
        sys.stderr.write(
            'The "' + args.command + '" command requires the '
            'following options:\n'
            '  --username, --revision, --release-versions, --release-date\n'
            ' and a list of CVE numbers.\n')
        sys.exit(1)
    args.release_versions = re.split(r'\s*,\s*', args.release_versions)
    args.release_date = datetime.datetime.strptime(args.release_date,
                                                   '%Y-%m-%d')


def sendmail(really_send, args):
    notice_template = os.path.join(ROOTDIR, NOTICE_TEMPLATE)
    mailing_list = os.path.join(ROOTDIR, MAILING_LIST)
    sender = args.username + '@apache.org'
    notification = security.parser.Notification(ROOTDIR, *args.cve)
    mailer = security.mailer.Mailer(notification,
                                    args.username + '@apache.org',
                                    notice_template,
                                    args.release_date,
                                    args.revision,
                                    *args.release_versions)
    message = mailer.generate_message()
    recipients = security.mailinglist.MailingList(mailing_list)
    if (not really_send):
        sys.stdout.write(message.as_string())
        return

    password = getpass.getpass('Password for ' + args.username
                               + ' at mail-relay.apache.org: ')
    mailer.send_mail(message, args.username, password,
                     recipients=recipients)


def check_generate(args):
    if (not (args.destination and args.cve)
        or args.username or args.revision
        or args.release_versions
        or args.release_date):
        sys.stderr.write(
            'The "generate" command requires the '
            '--destination option '
            'and a list of CVE numbers.\n')
        sys.exit(1)
    if not os.path.isdir(args.destination):
        sys.stderr.write(args.destination + ' is not a directory')
        sys.exit(1)

def generate(args):
    notification = security.parser.Notification(ROOTDIR, *args.cve)
    security.adviser.generate(notification, args.destination);


def main():
    check_root()
    args = parse_args(sys.argv[1:])
    if args.command in ('send', 'test'):
        check_sendmail(args)
        sendmail(args.command == 'send', args)
    elif args.command == 'generate':
        check_generate(args)
        generate(args)


if __name__ == '__main__':
    main()
