#!/usr/bin/env python
#
# svn-backup-dumps.py -- Create dumpfiles to backup a subversion repository.
#
# ====================================================================
# Copyright (c) 2006-2009 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================
#
# This script creates dump files from a subversion repository.
# It is intended for use in cron jobs and post-commit hooks.
#
# The basic operation modes are:
#    1. Create a full dump (revisions 0 to HEAD).
#    2. Create incremental dumps containing at most N revisions.
#    3. Create incremental single revision dumps (for use in post-commit).
#
# All dump files are prefixed with the basename of the repository. All
# examples below assume that the repository '/srv/svn/repos/src' is
# dumped so all dumpfiles start with 'src'.
#
# Optional functionality:
#    4. Create gzipped dump files.
#    5. Create bzipped dump files.
#    6. Transfer the dumpfile to another host using ftp.
#    7. Transfer the dumpfile to another host using smb.
#
# See also 'svn-backup-dumps.py -h'.
#
#
# 1. Create a full dump (revisions 0 to HEAD).
#
#    svn-backup-dumps.py <repos> <dumpdir>
#
#    <repos>      Path to the repository.
#    <dumpdir>    Directory for storing the dump file.
#
#    This creates a dump file named 'src.000000-NNNNNN.svndmp.gz'
#    where NNNNNN is the revision number of HEAD.
#
# 2. Create incremental dumps containing at most N revisions.
#
#    svn-backup-dumps.py -c <count> <repos> <dumpdir>
#
#    <count>      Count of revisions per dump file.
#    <repos>      Path to the repository.
#    <dumpdir>    Directory for storing the dump file.
#
#    When started the first time with a count of 1000 and if HEAD is
#    at 2923 it creates the following files:
#
#    src.000000-000999.svndmp.gz
#    src.001000-001999.svndmp.gz
#    src.002000-002923.svndmp.gz
#
#    Say the next time HEAD is at 3045 it creates these two files:
#
#    src.002000-002999.svndmp.gz
#    src.003000-003045.svndmp.gz
#
#
# 3. Create incremental single revision dumps (for use in post-commit).
#
#    svn-backup-dumps.py -r <revnr> <repos> <dumpdir>
#
#    <revnr>      A revision number.
#    <repos>      Path to the repository.
#    <dumpdir>    Directory for storing the dump file.
#
#    This creates a dump file named 'src.NNNNNN.svndmp.gz' where
#    NNNNNN is the revision number of HEAD.
#
#
# 4. Create gzipped dump files.
#
#    svn-backup-dumps.py -z ...
#
#    ...          More options, see 1-3, 6, 7.
#
#
# 5. Create bzipped dump files.
#
#    svn-backup-dumps.py -b ...
#
#    ...          More options, see 1-3, 6, 7.
#
#
# 6. Transfer the dumpfile to another host using ftp.
#
#    svn-backup-dumps.py -t ftp:<host>:<user>:<password>:<path> ...
#
#    <host>       Name of the FTP host.
#    <user>       Username on the remote host.
#    <password>   Password for the user.
#    <path>       Subdirectory on the remote host.
#    ...          More options, see 1-5.
#
#    If <path> contains the string '%r' it is replaced by the
#    repository name (basename of the repository path).
#
#
# 7. Transfer the dumpfile to another host using smb.
#
#    svn-backup-dumps.py -t smb:<share>:<user>:<password>:<path> ...
#
#    <share>      Name of an SMB share in the form '//host/share'.
#    <user>       Username on the remote host.
#    <password>   Password for the user.
#    <path>       Subdirectory of the share.
#    ...          More options, see 1-5.
#
#    If <path> contains the string '%r' it is replaced by the
#    repository name (basename of the repository path).
#
#
#
# TODO:
#  - find out how to report smbclient errors
#  - improve documentation
#

__version = "0.5"

import sys
import os
if os.name != "nt":
    import fcntl
    import select
import gzip
import os.path
from optparse import OptionParser
from ftplib import FTP
from subprocess import Popen, PIPE

try:
    import bz2
    have_bz2 = True
except ImportError:
    have_bz2 = False


class SvnBackupOutput:

    def __init__(self, abspath, filename):
        self.__filename = filename
        self.__absfilename = os.path.join(abspath, filename)

    def open(self):
        pass

    def write(self, data):
        pass

    def close(self):
        pass

    def get_filename(self):
        return self.__filename

    def get_absfilename(self):
        return self.__absfilename


class SvnBackupOutputPlain(SvnBackupOutput):

    def __init__(self, abspath, filename):
        SvnBackupOutput.__init__(self, abspath, filename)

    def open(self):
        self.__ofd = open(self.get_absfilename(), "wb")

    def write(self, data):
        self.__ofd.write(data)

    def close(self):
        self.__ofd.close()


class SvnBackupOutputGzip(SvnBackupOutput):

    def __init__(self, abspath, filename):
        SvnBackupOutput.__init__(self, abspath, filename + ".gz")

    def open(self):
        self.__compressor = gzip.GzipFile(filename=self.get_absfilename(),
                mode="wb")

    def write(self, data):
        self.__compressor.write(data)

    def close(self):
        self.__compressor.flush()
        self.__compressor.close()


class SvnBackupOutputBzip2(SvnBackupOutput):

    def __init__(self, abspath, filename):
        SvnBackupOutput.__init__(self, abspath, filename + ".bz2")

    def open(self):
        self.__compressor = bz2.BZ2Compressor()
        self.__ofd = open(self.get_absfilename(), "wb")

    def write(self, data):
        self.__ofd.write(self.__compressor.compress(data))

    def close(self):
        self.__ofd.write(self.__compressor.flush())
        self.__ofd.close()


class SvnBackupException(Exception):

    def __init__(self, errortext):
        self.errortext = errortext

    def __str__(self):
        return self.errortext

class SvnBackup:

    def __init__(self, options, args):
        # need 3 args: progname, reposname, dumpdir
        if len(args) != 3:
            if len(args) < 3:
                raise SvnBackupException("too few arguments, specify repospath and dumpdir.")
            else:
                raise SvnBackupException("too many arguments, specify repospath and dumpdir only.")
        self.__repospath = args[1]
        self.__dumpdir = args[2]
        # check repospath
        rpathparts = os.path.split(self.__repospath)
        if len(rpathparts[1]) == 0:
            # repospath without trailing slash
            self.__repospath = rpathparts[0]
        if not os.path.exists(self.__repospath):
            raise SvnBackupException("repos '%s' does not exist." % self.__repospath)
        if not os.path.isdir(self.__repospath):
            raise SvnBackupException("repos '%s' is not a directory." % self.__repospath)
        for subdir in [ "db", "conf", "hooks" ]:
            dir = os.path.join(self.__repospath, "db")
            if not os.path.isdir(dir):
                raise SvnBackupException("repos '%s' is not a repository." % self.__repospath)
        rpathparts = os.path.split(self.__repospath)
        self.__reposname = rpathparts[1]
        if self.__reposname in [ "", ".", ".." ]:
            raise SvnBackupException("couldn't extract repos name from '%s'." % self.__repospath)
        # check dumpdir
        if not os.path.exists(self.__dumpdir):
            raise SvnBackupException("dumpdir '%s' does not exist." % self.__dumpdir)
        elif not os.path.isdir(self.__dumpdir):
            raise SvnBackupException("dumpdir '%s' is not a directory." % self.__dumpdir)
        # set options
        self.__rev_nr = options.rev
        self.__count = options.cnt
        self.__quiet = options.quiet
        self.__deltas = options.deltas
        self.__zip = options.zip
        self.__overwrite = False
        self.__overwrite_all = False
        if options.overwrite > 0:
            self.__overwrite = True
        if options.overwrite > 1:
            self.__overwrite_all = True
        self.__transfer = None
        if options.transfer != None:
            self.__transfer = options.transfer.split(":")
            if len(self.__transfer) != 5:
                if len(self.__transfer) < 5:
                    raise SvnBackupException("too few fields for transfer '%s'." % self.__transfer)
                else:
                    raise SvnBackupException("too many fields for transfer '%s'." % self.__transfer)
            if self.__transfer[0] not in [ "ftp", "smb" ]:
                raise SvnBackupException("unknown transfer method '%s'." % self.__transfer[0])

    def set_nonblock(self, fileobj):
        fd = fileobj.fileno()
        n = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, n|os.O_NONBLOCK)

    def exec_cmd(self, cmd, output=None, printerr=False):
        if os.name == "nt":
            return self.exec_cmd_nt(cmd, output, printerr)
        else:
            return self.exec_cmd_unix(cmd, output, printerr)

    def exec_cmd_unix(self, cmd, output=None, printerr=False):
        try:
            proc = Popen(cmd, stdout=PIPE, stderr=PIPE, shell=False)
        except:
            return (256, "", "Popen failed (%s ...):\n  %s" % (cmd[0],
                    str(sys.exc_info()[1])))
        stdout = proc.stdout
        stderr = proc.stderr
        self.set_nonblock(stdout)
        self.set_nonblock(stderr)
        readfds = [ stdout, stderr ]
        selres = select.select(readfds, [], [])
        bufout = ""
        buferr = ""
        while len(selres[0]) > 0:
            for fd in selres[0]:
                buf = fd.read(16384)
                if len(buf) == 0:
                    readfds.remove(fd)
                elif fd == stdout:
                    if output:
                        output.write(buf)
                    else:
                        bufout += buf
                else:
                    if printerr:
                        sys.stdout.write("%s " % buf)
                    else:
                        buferr += buf
            if len(readfds) == 0:
                break
            selres = select.select(readfds, [], [])
        rc = proc.wait()
        if printerr:
            print("")
        return (rc, bufout, buferr)

    def exec_cmd_nt(self, cmd, output=None, printerr=False):
        try:
            proc = Popen(cmd, stdout=PIPE, stderr=None, shell=False)
        except:
            return (256, "", "Popen failed (%s ...):\n  %s" % (cmd[0],
                    str(sys.exc_info()[1])))
        stdout = proc.stdout
        bufout = ""
        buferr = ""
        buf = stdout.read(16384)
        while len(buf) > 0:
            if output:
                output.write(buf)
            else:
                bufout += buf
            buf = stdout.read(16384)
        rc = proc.wait()
        return (rc, bufout, buferr)

    def get_head_rev(self):
        cmd = [ "svnlook", "youngest", self.__repospath ]
        r = self.exec_cmd(cmd)
        if r[0] == 0 and len(r[2]) == 0:
            return int(r[1].strip())
        else:
            print(r[2])
        return -1

    def transfer_ftp(self, absfilename, filename):
        rc = False
        try:
            host = self.__transfer[1]
            user = self.__transfer[2]
            passwd = self.__transfer[3]
            destdir = self.__transfer[4].replace("%r", self.__reposname)
            ftp = FTP(host, user, passwd)
            ftp.cwd(destdir)
            ifd = open(absfilename, "rb")
            ftp.storbinary("STOR %s" % filename, ifd)
            ftp.quit()
            rc = len(ifd.read(1)) == 0
            ifd.close()
        except Exception, e:
            raise SvnBackupException("ftp transfer failed:\n  file:  '%s'\n  error: %s" % \
                    (absfilename, str(e)))
        return rc

    def transfer_smb(self, absfilename, filename):
        share = self.__transfer[1]
        user = self.__transfer[2]
        passwd = self.__transfer[3]
        if passwd == "":
            passwd = "-N"
        destdir = self.__transfer[4].replace("%r", self.__reposname)
        cmd = ("smbclient", share, "-U", user, passwd, "-D", destdir,
                "-c", "put %s %s" % (absfilename, filename))
        r = self.exec_cmd(cmd)
        rc = r[0] == 0
        if not rc:
            print(r[2])
        return rc

    def transfer(self, absfilename, filename):
        if self.__transfer == None:
            return
        elif self.__transfer[0] == "ftp":
            self.transfer_ftp(absfilename, filename)
        elif self.__transfer[0] == "smb":
            self.transfer_smb(absfilename, filename)
        else:
            print("unknown transfer method '%s'." % self.__transfer[0])

    def create_dump(self, checkonly, overwrite, fromrev, torev=None):
        revparam = "%d" % fromrev
        r = "%06d" % fromrev
        if torev != None:
            revparam += ":%d" % torev
            r += "-%06d" % torev
        filename = "%s.%s.svndmp" % (self.__reposname, r)
        output = None
        if self.__zip:
            if self.__zip == "gzip":
                output = SvnBackupOutputGzip(self.__dumpdir, filename)
            else:
                output = SvnBackupOutputBzip2(self.__dumpdir, filename)
        else:
            output = SvnBackupOutputPlain(self.__dumpdir, filename)
        absfilename = output.get_absfilename()
        realfilename = output.get_filename()
        if checkonly:
            return os.path.exists(absfilename)
        elif os.path.exists(absfilename):
            if overwrite:
                print("overwriting " + absfilename)
            else:
                print("%s already exists." % absfilename)
                return True
        else:
            print("writing " + absfilename)
        cmd = [ "svnadmin", "dump",
                "--incremental", "-r", revparam, self.__repospath ]
        if self.__quiet:
            cmd[2:2] = [ "-q" ]
        if self.__deltas:
            cmd[2:2] = [ "--deltas" ]
        output.open()
        r = self.exec_cmd(cmd, output, True)
        output.close()
        rc = r[0] == 0
        if rc:
            self.transfer(absfilename, realfilename)
        return rc

    def export_single_rev(self):
        return self.create_dump(False, self.__overwrite, self.__rev_nr)

    def export(self):
        headrev = self.get_head_rev()
        if headrev == -1:
            return False
        if self.__count is None:
            return self.create_dump(False, self.__overwrite, 0, headrev)
        baserev = headrev - (headrev % self.__count)
        rc = True
        cnt = self.__count
        fromrev = baserev - cnt
        torev = baserev - 1
        while fromrev >= 0 and rc:
            if self.__overwrite_all or \
                not self.create_dump(True, False, fromrev, torev):
                rc = self.create_dump(False, self.__overwrite_all,
                            fromrev, torev)
                fromrev -= cnt
                torev -= cnt
            else:
                fromrev = -1
        if rc:
            rc = self.create_dump(False, self.__overwrite, baserev, headrev)
        return rc

    def execute(self):
        if self.__rev_nr != None:
            return self.export_single_rev()
        else:
            return self.export()


if __name__ == "__main__":
    usage = "usage: svn-backup-dumps.py [options] repospath dumpdir"
    parser = OptionParser(usage=usage, version="%prog "+__version)
    if have_bz2:
        parser.add_option("-b",
                       action="store_const", const="bzip2",
                       dest="zip", default=None,
                       help="compress the dump using bzip2.")
    parser.add_option("--deltas",
                       action="store_true",
                       dest="deltas", default=False,
                       help="pass --deltas to svnadmin dump.")
    parser.add_option("-c",
                       action="store", type="int",
                       dest="cnt", default=None,
                       help="count of revisions per dumpfile.")
    parser.add_option("-o",
                       action="store_const", const=1,
                       dest="overwrite", default=0,
                       help="overwrite files.")
    parser.add_option("-O",
                       action="store_const", const=2,
                       dest="overwrite", default=0,
                       help="overwrite all files.")
    parser.add_option("-q",
                       action="store_true",
                       dest="quiet", default=False,
                       help="quiet.")
    parser.add_option("-r",
                       action="store", type="int",
                       dest="rev", default=None,
                       help="revision number for single rev dump.")
    parser.add_option("-t",
                       action="store", type="string",
                       dest="transfer", default=None,
                       help="transfer dumps to another machine "+
                            "(s.a. --help-transfer).")
    parser.add_option("-z",
                       action="store_const", const="gzip",
                       dest="zip",
                       help="compress the dump using gzip.")
    parser.add_option("--help-transfer",
                       action="store_true",
                       dest="help_transfer", default=False,
                       help="shows detailed help for the transfer option.")
    (options, args) = parser.parse_args(sys.argv)
    if options.help_transfer:
        print("Transfer help:")
        print("")
        print("  FTP:")
        print("    -t ftp:<host>:<user>:<password>:<dest-path>")
        print("")
        print("  SMB (using smbclient):")
        print("    -t smb:<share>:<user>:<password>:<dest-path>")
        print("")
        sys.exit(0)
    rc = False
    try:
        backup = SvnBackup(options, args)
        rc = backup.execute()
    except SvnBackupException, e:
        print("svn-backup-dumps.py: %s" % e)
    if rc:
        print("Everything OK.")
        sys.exit(0)
    else:
        print("An error occured!")
        sys.exit(1)

# vim:et:ts=4:sw=4
