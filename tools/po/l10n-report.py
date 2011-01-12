#!/usr/bin/env python
#
# $Id$
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

"""Usage: l10n-report.py [OPTION...]

Send the l10n translation status report to an email address. If the
email address is not specified, print in stdout.

Options:

 -h, --help           Show this help message.

 -m, --to-email-id    Send the l10n translation status report to this
                      email address.
"""

import sys
import getopt
import os
import re
import subprocess

LIST_ADDRESS = "dev@subversion.apache.org"

def _rev():
  dollar = "$Revision$"
  return int(re.findall('[0-9]+', dollar)[0]);

def usage_and_exit(errmsg=None):
    """Print a usage message, plus an ERRMSG (if provided), then exit.
    If ERRMSG is provided, the usage message is printed to stderr and
    the script exits with a non-zero error code.  Otherwise, the usage
    message goes to stdout, and the script exits with a zero
    errorcode."""
    if errmsg is None:
        stream = sys.stdout
    else:
        stream = sys.stderr
    stream.write("%s\n" % __doc__)
    stream.flush()
    if errmsg:
        stream.write("\nError: %s\n" % errmsg)
        stream.flush()
        sys.exit(2)
    sys.exit(0)


class l10nReport:
    def __init__(self, to_email_id=""):
        self.to_email_id = to_email_id
        self.from_email_id = "<%s>" % LIST_ADDRESS

    def safe_command(self, cmd_and_args, cmd_in=""):
        [stdout, stderr] = subprocess.Popen(cmd_and_args, \
                           stdin=subprocess.PIPE, \
                           stdout=subprocess.PIPE, \
                           stderr=subprocess.PIPE).communicate(input=cmd_in)
        return stdout, stderr

    def match(self, pattern, string):
        match = re.compile(pattern).search(string)
        if match and match.groups():
            return match.group(1)
        else:
            return None

    def get_msgattribs(self, file):
        msgout = self.safe_command(['msgattrib', '--translated', file])[0]
        grepout = self.safe_command(['grep', '-E', '^msgid *"'], msgout)[0]
        sedout = self.safe_command(['sed', '1d'], grepout)[0]
        trans = self.safe_command(['wc', '-l'], sedout)[0]

        msgout = self.safe_command(['msgattrib', '--untranslated', file])[0]
        grepout = self.safe_command(['grep', '-E', '^msgid *"'], msgout)[0]
        sedout = self.safe_command(['sed', '1d'], grepout)[0]
        untrans = self.safe_command(['wc', '-l'], sedout)[0]

        msgout = self.safe_command(['msgattrib', '--only-fuzzy', file])[0]
        grepout = self.safe_command(['grep', '-E', '^msgid *"'], msgout)[0]
        sedout = self.safe_command(['sed', '1d'], grepout)[0]
        fuzzy = self.safe_command(['wc', '-l'], sedout)[0]

        msgout = self.safe_command(['msgattrib', '--only-obsolete', file])[0]
        grepout = self.safe_command(['grep', '-E', '^#~ msgid *"'], msgout)[0]
        obsolete = self.safe_command(['wc', '-l'], grepout)[0]

        return int(trans), int(untrans), int(fuzzy), int(obsolete)

    def pre_l10n_report(self):
        # svn revert --recursive subversion/po
        cmd = ['svn', 'revert', '--recursive', 'subversion/po']
        stderr = self.safe_command(cmd)[1]
        if stderr:
          sys.stderr.write("\nError: %s\n" % stderr)
          sys.stderr.flush()
          sys.exit(0)

        # svn update
        cmd = ['svn', 'update']
        stderr = self.safe_command(cmd)[1]
        if stderr:
          sys.stderr.write("\nError: %s\n" % stderr)
          sys.stderr.flush()
          sys.exit(0)

        # tools/po/po-update.sh
        cmd = ['sh', 'tools/po/po-update.sh']
        self.safe_command(cmd)


def main():
    # Parse the command-line options and arguments.
    try:
        opts, args = getopt.gnu_getopt(sys.argv[1:], "hm:",
                                       ["help",
                                        "to-email-id=",
                                        ])
    except getopt.GetoptError, msg:
        usage_and_exit(msg)

    to_email_id = None
    for opt, arg in opts:
        if opt in ("-h", "--help"):
            usage_and_exit()
        elif opt in ("-m", "--to-email-id"):
            to_email_id = arg

    l10n = l10nReport()
    os.chdir("%s/../.." % os.path.dirname(os.path.abspath(sys.argv[0])))
    l10n.pre_l10n_report()
    [info_out, info_err] = l10n.safe_command(['svn', 'info'])
    if info_err:
        sys.stderr.write("\nError: %s\n" % info_err)
        sys.stderr.flush()
        sys.exit(0)

    po_dir = 'subversion/po'
    branch_name = l10n.match('URL:.*/asf/subversion/(\S+)', info_out)
    [info_out, info_err] = l10n.safe_command(['svnversion', po_dir])
    if info_err:
        sys.stderr.write("\nError: %s\n" % info_err)
        sys.stderr.flush()
        sys.exit(0)

    wc_version = re.sub('[MS]', '', info_out)
    title = "Translation status report for %s@r%s" % \
               (branch_name, wc_version)

    os.chdir(po_dir)
    files = sorted(os.listdir('.'))
    format_head = "%6s %7s %7s %7s %7s" % ("lang", "trans", "untrans",
                   "fuzzy", "obs")
    format_line = "--------------------------------------"
    print("\n%s\n%s\n%s" % (title, format_head, format_line))

    body = ""
    for file in files:
        lang = l10n.match('(.*).po$', file)
        if not lang:
            continue
        [trans, untrans, fuzzy, obsolete]  = l10n.get_msgattribs(file)
        po_format = "%6s %7d %7d %7d %7d" %\
                    (lang, trans, untrans, fuzzy, obsolete)
        body += "%s\n" % po_format
        print(po_format)

    if to_email_id:
        import smtplib

        server = smtplib.SMTP('localhost')
        email_from = "From: Subversion Translation Status <noreply@subversion.apache.org>"
        email_to = "To: %s" % to_email_id
        email_sub = "Subject: [l10n] Translation status report for %s r%s" \
                     % (branch_name, wc_version)
        x_headers = "\n".join([
          "X-Mailer: l10n-report.py r%ld" % _rev(),
          "Reply-To: %s" % LIST_ADDRESS,
          "Mail-Followup-To: %s" % LIST_ADDRESS,
          # http://www.iana.org/assignments/auto-submitted-keywords/auto-submitted-keywords.xhtml
          "Auto-Submitted: auto-generated",
        ]);

        msg = "\n".join((email_from, email_to, email_sub, x_headers,
                        title, format_head, format_line, body))

        server.sendmail(email_from, email_to, msg)
        print("The report is sent to '%s' email id." % to_email_id)
    else:
        print("\nYou have not passed '-m' option, so email is not sent.")

if __name__ == "__main__":
    main()
