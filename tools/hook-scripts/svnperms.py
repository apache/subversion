#!/usr/bin/env python

# $HeadURL$
# $LastChangedDate$
# $LastChangedBy$
# $LastChangedRevision$

import commands
import sys, os
import getopt
try:
    my_getopt = getopt.gnu_getopt
except AttributeError:
    my_getopt = getopt.getopt
import re

__author__ = "Gustavo Niemeyer <gustavo@niemeyer.net>"

class Error(Exception): pass

SECTION = re.compile(r'\[([^]]+?)(?:\s+extends\s+([^]]+))?\]')
OPTION = re.compile(r'(\S+)\s*=\s*(.*)$')

class Config:
    def __init__(self, filename):
        # Options are stored in __sections_list like this:
        # [(sectname, [(optname, optval), ...]), ...]
        self._sections_list = []
        self._sections_dict = {}
        self._read(filename)

    def _read(self, filename):
        # Use the same logic as in ConfigParser.__read()
        file = open(filename)
        cursectdict = None
        optname = None
        lineno = 0
        for line in file.xreadlines():
            lineno = lineno + 1
            if line.isspace() or line[0] == '#':
                continue
            if line[0].isspace() and cursectdict is not None and optname:
                value = line.strip()
                cursectdict[optname] = "%s %s" % (cursectdict[optname], value)
                cursectlist[-1][1] = "%s %s" % (cursectlist[-1][1], value)
            else:
                m = SECTION.match(line)
                if m:
                    sectname = m.group(1)
                    parentsectname = m.group(2)
                    if parentsectname is None:
                        # No parent section defined, so start a new section
                        cursectdict = self._sections_dict.setdefault \
                            (sectname, {})
                        cursectlist = []
                    else:
                        # Copy the parent section into the new section
                        parentsectdict = self._sections_dict.get \
                            (parentsectname, {})
                        cursectdict = self._sections_dict.setdefault \
                            (sectname, parentsectdict.copy())
                        cursectlist = self.walk(parentsectname)
                    self._sections_list.append((sectname, cursectlist))
                    optname = None
                elif cursectdict is None:
                    raise Error, "%s:%d: no section header" % \
                                 (filename, lineno)
                else:
                    m = OPTION.match(line)
                    if m:
                        optname, optval = m.groups()
                        optval = optval.strip()
                        cursectdict[optname] = optval
                        cursectlist.append([optname, optval])
                    else:
                        raise Error, "%s:%d: parsing error" % \
                                     (filename, lineno)

    def sections(self):
        return self._sections_dict.keys()

    def options(self, section):
        return self._sections_dict.get(section, {}).keys()

    def get(self, section, option, default=None):
        return self._sections_dict.get(option, default)

    def walk(self, section, option=None):
        ret = []
        for sectname, options in self._sections_list:
            if sectname == section:
                for optname, value in options:
                    if not option or optname == option:
                        ret.append((optname, value))
        return ret


class Permission:
    def __init__(self):
        self._group = {}
        self._permlist = []

    def parse_groups(self, groupsiter):
        for option, value in groupsiter:
            self._group[option] = value.split()

    def parse_perms(self, permsiter):
        for option, value in permsiter:
            # Paths never start with /, so remove it if provided
            if option[0] == "/":
                option = option[1:]
            pattern = re.compile("^%s$" % option)
            for entry in value.split():
                openpar, closepar = entry.find("("), entry.find(")")
                groupsusers = entry[:openpar].split(",")
                perms = entry[openpar+1:closepar].split(",")
                users = []
                for groupuser in groupsusers:
                    if groupuser[0] == "@":
                        try:
                            users.extend(self._group[groupuser[1:]])
                        except KeyError:
                            raise Error, "group '%s' not found" % \
                                         groupuser[1:]
                    else:
                        users.append(groupuser)
                self._permlist.append((pattern, users, perms))

    def get(self, user, path):
        ret = []
        for pattern, users, perms in self._permlist:
            if pattern.match(path) and (user in users or "*" in users):
                ret = perms
        return ret

class SVNLook:
    def __init__(self, repospath, txn=None, rev=None):
        self.repospath = repospath
        self.txn = txn
        self.rev = rev

    def _execcmd(self, *cmd, **kwargs):
        cmdstr = " ".join(cmd)
        status, output = commands.getstatusoutput(cmdstr)
        if status != 0:
            sys.stderr.write(cmdstr)
            sys.stderr.write("\n")
            sys.stderr.write(output)
            raise Error, "command failed: %s\n%s" % (cmdstr, output)
        return status, output

    def _execsvnlook(self, cmd, *args, **kwargs):
        execcmd_args = ["svnlook", cmd, self.repospath]
        self._add_txnrev(execcmd_args, kwargs)
        execcmd_args += args
        execcmd_kwargs = {}
        keywords = ["show", "noerror"]
        for key in keywords:
            if key in kwargs:
                execcmd_kwargs[key] = kwargs[key]
        return self._execcmd(*execcmd_args, **execcmd_kwargs)

    def _add_txnrev(self, cmd_args, received_kwargs):
        if "txn" in received_kwargs:
            txn = received_kwargs.get("txn")
            if txn is not None:
                cmd_args += ["-t", txn]
        elif self.txn is not None:
            cmd_args += ["-t", self.txn]
        if "rev" in received_kwargs:
            rev = received_kwargs.get("rev")
            if rev is not None:
                cmd_args += ["-r", rev]
        elif self.rev is not None:
            cmd_args += ["-r", self.rev]

    def changed(self, **kwargs):
        status, output = self._execsvnlook("changed", **kwargs)
        if status != 0:
            return None
        changes = []
        for line in output.splitlines():
            line = line.rstrip()
            if not line: continue
            entry = [None, None, None]
            changedata, changeprop, path = None, None, None
            if line[0] != "_":
                changedata = line[0]
            if line[1] != " ":
                changeprop = line[1]
            path = line[4:]
            changes.append((changedata, changeprop, path))
        return changes

    def author(self, **kwargs):
        status, output = self._execsvnlook("author", **kwargs)
        if status != 0:
            return None
        return output.strip()


def check_perms(filename, section, repos, txn=None, rev=None, author=None):
    svnlook = SVNLook(repos, txn=txn, rev=rev)
    if author is None:
        author = svnlook.author()
    changes = svnlook.changed()
    try:
        config = Config(filename)
    except IOError:
        raise Error, "can't read config file "+filename
    if not section in config.sections():
        raise Error, "section '%s' not found in config file" % section
    perm = Permission()
    perm.parse_groups(config.walk("groups"))
    perm.parse_groups(config.walk(section+" groups"))
    perm.parse_perms(config.walk(section))
    permerrors = []
    for changedata, changeprop, path in changes:
        pathperms = perm.get(author, path)
        if changedata == "A" and "add" not in pathperms:
            permerrors.append("you can't add "+path)
        elif changedata == "U" and "update" not in pathperms:
            permerrors.append("you can't update "+path)
        elif changedata == "D" and "remove" not in pathperms:
            permerrors.append("you can't remove "+path)
        elif changeprop == "U" and "update" not in pathperms:
            permerrors.append("you can't update properties of "+path)
        #else:
        #    print "cdata=%s cprop=%s path=%s perms=%s" % \
        #          (str(changedata), str(changeprop), path, str(pathperms))
    if permerrors:
        permerrors.insert(0, "you don't have enough permissions for "
                             "this transaction:")
        raise Error, "\n".join(permerrors)


# Command:

USAGE = """\
Usage: svnperms.py OPTIONS

Options:
    -r PATH    Use repository at PATH to check transactions
    -t TXN     Query transaction TXN for commit information
    -f PATH    Use PATH as configuration file (default is repository
               path + /conf/svnperms.conf)
    -s NAME    Use section NAME as permission section (default is
               repository name, extracted from repository path)
    -R REV     Query revision REV for commit information (for tests)
    -A AUTHOR  Check commit as if AUTHOR had commited it (for tests)
    -h         Show this message
"""

class MissingArgumentsException(Exception):
    "Thrown when required arguments are missing."
    pass

def parse_options():
    try:
        opts, args = my_getopt(sys.argv[1:], "f:s:r:t:R:A:h", ["help"])
    except getopt.GetoptError, e:
        raise Error, e.msg
    class Options: pass
    obj = Options()
    obj.filename = None
    obj.section = None
    obj.repository = None
    obj.transaction = None
    obj.revision = None
    obj.author = None
    for opt, val in opts:
        if opt == "-f":
            obj.filename = val
        elif opt == "-s":
            obj.section = val
        elif opt == "-r":
            obj.repository = val
        elif opt == "-t":
            obj.transaction = val
        elif opt == "-R":
            obj.revision = val
        elif opt == "-A":
            obj.author = val
        elif opt in ["-h", "--help"]:
            sys.stdout.write(USAGE)
            sys.exit(0)
    missingopts = []
    if not obj.repository:
        missingopts.append("repository")
    if not (obj.transaction or obj.revision):
        missingopts.append("either transaction or a revision")
    if missingopts:
        raise MissingArgumentsException, \
              "missing required option(s): " + ", ".join(missingopts)
    obj.repository = os.path.abspath(obj.repository)
    if obj.filename is None:
        obj.filename = os.path.join(obj.repository, "conf", "svnperms.conf")
    if obj.section is None:
        obj.section = os.path.basename(obj.repository)
    if not (os.path.isdir(obj.repository) and
            os.path.isdir(os.path.join(obj.repository, "db")) and
            os.path.isdir(os.path.join(obj.repository, "hooks")) and
            os.path.isfile(os.path.join(obj.repository, "format"))):
        raise Error, "path '%s' doesn't look like a repository" % \
                     obj.repository

    return obj

def main():
    try:
        opts = parse_options()
        check_perms(opts.filename, opts.section,
                    opts.repository, opts.transaction, opts.revision,
                    opts.author)
    except MissingArgumentsException, e:
        sys.stderr.write("%s\n" % str(e))
        sys.stderr.write(USAGE)
        sys.exit(1)
    except Error, e:
        sys.stderr.write("error: %s\n" % str(e))
        sys.exit(1)

if __name__ == "__main__":
    main()

# vim:et:ts=4:sw=4
