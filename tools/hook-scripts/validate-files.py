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

"""Subversion pre-commit hook script that runs user configured commands
to validate files in the commit and reject the commit if the commands
exit with a non-zero exit code.  The script expects a validate-files.conf
file placed in the conf dir under the repo the commit is for."""

import sys
import os
import subprocess
import fnmatch

# Deal with the rename of ConfigParser to configparser in Python3
try:
    # Python >= 3.0
    import configparser
except ImportError:
    # Python < 3.0
    import ConfigParser as configparser

class Config(configparser.SafeConfigParser):
    """Superclass of SafeConfigParser with some customizations
    for this script"""
    def optionxform(self, option):
        """Redefine optionxform so option names are case sensitive"""
        return option

    def getlist(self, section, option):
        """Returns value of option as a list using whitespace to
        split entries"""
        value = self.get(section, option)
        if value:
            return value.split()
        else:
            return None

    def get_matching_rules(self, repo):
        """Return list of unique rules names that apply to a given repo"""
        rules = {}
        for option in self.options('repositories'):
            if fnmatch.fnmatch(repo, option):
                for rule in self.getlist('repositories', option):
                    rules[rule] = True
        return rules.keys()

    def get_rule_section_name(self, rule):
        """Given a rule name provide the section name it is defined in."""
        return 'rule:%s' % (rule)

class Commands:
    """Class to handle logic of running commands"""
    def __init__(self, config):
        self.config = config

    def svnlook_changed(self, repo, txn):
        """Provide list of files changed in txn of repo"""
        svnlook = self.config.get('DEFAULT', 'svnlook')
        cmd = "'%s' changed -t '%s' '%s'" % (svnlook, txn, repo)
        p = subprocess.Popen(cmd, shell=True,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        changed = []
        while True:
            line = p.stdout.readline()
            if not line:
                break
            line = line.decode().strip()
            text_mod = line[0:1]
            # Only if the contents of the file changed (by addition or update)
            # directories always end in / in the svnlook changed output
            if line[-1] != "/" and (text_mod == "A" or text_mod == "U"):
                changed.append(line[4:])

        # wait on the command to finish so we can get the
        # returncode/stderr output
        data = p.communicate()
        if p.returncode != 0:
            sys.stderr.write(data[1].decode())
            sys.exit(2)

        return changed

    def user_command(self, section, repo, txn, fn):
        """ Run the command defined for a given section.
        Replaces $REPO, $TXN and $FILE with the repo, txn and fn arguments
        in the defined command.

        Returns a tuple of the exit code and the stderr output of the command"""
        cmd = self.config.get(section, 'command')
        cmd_env = os.environ.copy()
        cmd_env['REPO'] = repo
        cmd_env['TXN'] = txn
        cmd_env['FILE'] = fn
        p = subprocess.Popen(cmd, shell=True, env=cmd_env, stderr=subprocess.PIPE)
        data = p.communicate()
        return (p.returncode, data[1].decode())

def main(repo, txn):
    exitcode = 0
    config = Config()
    config.read(os.path.join(repo, 'conf', 'validate-files.conf'))
    commands = Commands(config)

    rules = config.get_matching_rules(repo)

    # no matching rules so nothing to do
    if len(rules) == 0:
        sys.exit(0)

    changed = commands.svnlook_changed(repo, txn)
    # this shouldn't ever happen
    if len(changed) == 0:
        sys.exit(0)

    for rule in rules:
        section = config.get_rule_section_name(rule)
        pattern = config.get(section, 'pattern')

        # skip leading slashes if present in the pattern
        if pattern[0] == '/': pattern = pattern[1:]

        for fn in fnmatch.filter(changed, pattern):
            (returncode, err_mesg) = commands.user_command(section, repo,
                                                           txn, fn)
            if returncode != 0:
                sys.stderr.write(
                    "\nError validating file '%s' with rule '%s' " \
                    "(exit code %d):\n" % (fn, rule, returncode))
                sys.stderr.write(err_mesg)
                exitcode = 1

    return exitcode

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.stderr.write("invalid args\n")
        sys.exit(0)

    try:
        sys.exit(main(sys.argv[1], sys.argv[2]))
    except configparser.Error as e:
        sys.stderr.write("Error with the validate-files.conf: %s\n" % e)
        sys.exit(2)
