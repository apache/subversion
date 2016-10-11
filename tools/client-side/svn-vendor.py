#!/usr/bin/python3
# vim: set sw=4 expandtab :
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
# ====================================================================
#
##############################################################################
# svn-vendor.py
#
# Overview
# --------
#   Replacement for svn_load_dirs.pl (included as a 'contributed utility' in
#   Subversion sources). Main difference is some heuristics in detection of
#   the renames. Note that this script does not attempt to automate remote
#   SVN operations (check-out, check-in and tagging), so it is possible to
#   review the state of sources that are about to be checked in. Another
#   difference is an ability to save the detected renames, review/re-apply
#   them.
#
#   This script requires Python 3.3.x or higher. Sorry, I was too lazy
#   to write shell quoting routines that are already available in recent
#   Python versions.
#
# Using this script
# -----------------
#   First, it is necessary to check out the working copy from the URL that
#   will host the imported sources. E.g., if the versions of FOO are being
#   imported into svn://example.com/vendor/FOO/current:
#
#     svn co svn://example.com/vendor/FOO/current wc
#
#   Then, unpack the sources of the version to be imported:
#
#     tar xzf foo-1.1.tar.gz
#
#   Examples below assume the command above created a `foo-1.1' directory.
#   After that, there are three different modes of operation:
#
#   1. Fully automatic
#
#     svn-vendor.py --auto wc foo-1.1
#     svn st wc
#     svn ci wc
#
#   In this mode, the script fully relies on its heuristics in detection of
#   renames. In many cases, it "just works". There can be spurious moves
#   detected in this mode, though. For example, consider a deleted header
#   that consists of 50 lines of GPL text,  1 line of copyright, and
#   3 lines of declarations, and a similar unrelated header in the imported
#   sources. From the script's point of view, the files are nearly identical
#   (4 lines removed, 4 lines added, 50 lines unchanged).
#
#   After the script completes, examine the working copy by doing 'svn diff'
#   and/or 'svn status', paying particular attention to renames. If all the
#   moves are detected correctly, check in the changes in the working copy.
#
#   2. Semi-automatic
#
#     svn-vendor.py --detect moves-foo-1.1.txt wc foo-1.1
#     vi moves-foo-1.1.txt
#     svn-vendor.py --apply moves-foo-1.1.txt wc foo-1.1
#     svn ci wc
#
#   If the fully automatic mode mis-detected some spurious moves, or did not
#   detect some renames you want to be performed, it is still possible to
#   leverage what the script has detected automatically. First command above
#   does the automatic detection, just as it does in fully automatic mode,
#   but stops short of performing any modification of the working copy.
#   The list of detected copies and renames is saved into a text file,
#   `moves-foo-1.1.txt'.
#
#   That file can be inspected after the script finishes. Spurious moves can
#   be deleted from the file, and new copies/renames can be added. Then the
#   changes can be applied to the working copy.
#
#   3. Manual
#
#     svn-vendor.py wc foo-1.1
#     (svn-vendor) detect
#     (svn-vendor) move x.c y.c
#     (svn-vendor) move include/1.h include/2.h
#     (svn-vendor) copy include/3.h include/3-copy.h
#     (svn-vendor) lsprep
#     (svn-vendor) save /tmp/renames-to-be-applied.txt
#     (svn-vendor) apply
#
#   If the automatic detection does not help, it is possible to do the renames
#   manually (similarly to svn_load_dirs.pl). Use the 'help' command to get
#   the list of supported commands and their description. Feel free to play
#   around - since the script does not perform any remote SVN operation,
#   there is no chance to commit the changes accidentally.
#
# Notes
# -----
#   I. The time for rename detection O(Fs*Fd) + O(Ds*Dd), where Fs is
#   the number of files removed from current directory, Fd is number of files
#   added in imported sources, and Ds/Dd is the same for directories. That is,
#   the running time may become an issue if the numbers of added/removed files
#   go into a few thousands (e.g. if updating Linux kernel 2.6.35 to 3.10).
#   As a workaround, import interim releases first so that the number of
#   renames remains sane at each step. That makes reviewing the renames
#   performed by the script much easier.
#
#   Enjoy!
#
##############################################################################

import argparse
import cmd
import difflib
import filecmp
import os
import readline
import shlex
import shutil
import subprocess
import sys

def name_similarity(n1, n2):
    '''
    Function to be used as a key for sorting dirs/files by name matching
    '''
    sm = difflib.SequenceMatcher(a=n1, b=n2)
    return 1.0 - sm.ratio()


def filename_sort_key(s):
    '''
    Function to sort filenames so that parent directory is always followed
    by its children. Without it, [ "/a", "/a-b", "/a/b", "/a-b/c" ] would
    not be sorted correctly.
    '''
    return s.replace('/', '\001')


def descendant_or_self(path, ancestor):
    '''
    Check if path is somewhere in hierarchy under ancestor.
    '''
    return path == ancestor or path.startswith(ancestor + os.sep)

def path_rebase(path, old_base, new_base):
    '''
    Return a path name that has the same relative path to new_base as path
    had to old_base. Assumes path is a descendant of old_base.
    '''
    if path == old_base:
        return new_base
    return os.path.normpath(os.path.join(new_base,
        os.path.relpath(path, old_base)))


def for_all_parents(path, func):
    '''
    Invoke func for each parent path.
    '''
    d = os.path.dirname(path)
    while d != "":
        func(d)
        d = os.path.dirname(d)

class InvalidUsageException(Exception):
    '''
    Raised if command line arguments are invalid
    '''
    def __init__(self, cmd, msg):
        Exception.__init__(self, msg)
        self.cmd = cmd


class NotImplementedException(Exception):
    '''
    Raised if some code path is not implemented
    '''
    pass


# Indexes into FSO.state
S_WC = 0
S_IM = 1

class FSO(object):
    '''
    File system object (file/dir either in imported dir or in WC)
    '''
    def __init__(self):
        self.wc_path = None
        self.state = [ "-", "-" ] # '-': absent, 'F': file, 'D': dir, 'L': symlink

    def status(self):
        return "[%s%s]" % (self.state[S_WC], self.state[S_IM])

    def orig_reference(self, curpath):
        if self.wc_path and self.wc_path != curpath:
            return " (original: %s)" % shlex.quote(self.wc_path)
        return ""


class FSOCollection(dict):
    '''
    Collection of FSOs
    '''
    def print(self):
        print(" / Status in working copy (-:absent, F:file, D:dir, L:link)")
        print(" |/ Status in imported sources (-:absent, F:file, D:dir, L:link)")
        for k in sorted(self.keys(), key=filename_sort_key):
            e = self[k]
            print("%s %s%s" % (e.status(), shlex.quote(k),
                e.orig_reference(k)))

    def get(self, path):
        'Get existing FSO or create a new one'
        if path in self:
            return self[path]
        e = FSO()
        self[path] = e
        return e

    def add(self, path, where, kind):
        'Adding entries during initial scan'
        path = os.path.normpath(path)
        e = self.get(path)
        e.state[where] = kind
        if where == S_WC:
            e.wc_path = path

    def wc_copy(self, src, dst):
        'Handle move in a working copy'
        keys = list(self.keys())
        for k in keys:
            if descendant_or_self(k, src):
                esrc = self[k]
                if esrc.state[S_WC] == "-":
                    continue
                kn = path_rebase(k, src, dst)
                edst = self.get(kn)
                if edst.state[S_WC] != "-":
                    # Copying into existing destination.
                    # Caller should've checked this.
                    raise NotImplementedException
                edst.wc_path = esrc.wc_path
                edst.state[S_WC] = esrc.state[S_WC]

    def wc_remove(self, path):
        'Handle removal in a working copy'
        keys = list(self.keys())
        for k in keys:
            if descendant_or_self(k, path):
                self[k].state[S_WC] = "-"


class ConfigOpt(object):
    'Helper class - single option (string)'
    def __init__(self, value, helpmsg):
        self.value = value
        self.helpmsg = helpmsg

    def set(self, new_value):
        self.value = new_value

    def __str__(self):
        return "<none>" if self.value is None else "`%s'" % self.value


class ConfigOptInt(ConfigOpt):
    'Helper class - single option (integer)'
    def set(self, new_value):
        try:
            self.value = int(new_value)
        except ValueError:
            raise InvalidUsageException(None, "Value must be integer")

    def __str__(self):
        return "%d" % self.value


class Config(dict):
    '''
    Store configuration options.
    '''
    def add_option(self, name, cfgopt):
        self[name] = cfgopt

    def set(self, name, value):
        if name not in self:
            raise InvalidUsageException(None,
                    "Unknown config variable '%s'" % name)
        self[name].set(value)

    def get(self, name):
        if name not in self:
            raise NotImplementedException()
        return self[name].value

    def print(self):
        for k in sorted(self):
            o = self[k]
            for s in o.helpmsg.split('\n'):
                print("# %s" % s)
            print("%-20s: %s" % (k, str(o)))
            print("")


class SvnVndImport(cmd.Cmd):
    '''
    Main driving class.
    '''
    intro = "Welcome to SVN vendor import helper. " + \
            "Type help or ? to list commands.\n"
    prompt = "(svn-vendor) "
    prepare_ops = []

    def __init__(self, wcdir, importdir, svninfo):
        cmd.Cmd.__init__(self)
        self.wcdir = wcdir
        self.importdir = importdir
        self.svninfo = svninfo
        self.config = Config()
        self.config.add_option('symlink-handling',
                ConfigOpt("as-is", "How symbolic links are handled;\n" +
                    "  'dereference' treats as normal files/dirs (and " +
                    "ignores dangling links);\n" +
                    "  'as-is' imports as symlinks"))
        self.config.add_option('exec-permission',
                ConfigOpt("preserve", "How 'executable' permission bits " +
                    "are handled;\n" +
                    "  'preserve' sets svn:executable property as in " +
                    "imported sources;\n" +
                    "  'clear' removes svn:executable on all new files " +
                    "(but keeps it intact on existing files)."))
        self.config.add_option('save-diff-copied',
                ConfigOpt(None, "Save 'svn diff' output on the " +
                    "moved/copied files and directories to this " +
                    "file as part of 'apply'"))
        self.config.add_option('dir-similarity',
                ConfigOptInt(600, "Similarity between dirs to assume " +
                    "a copy/move [0..1000]"))
        self.config.add_option('file-similarity',
                ConfigOptInt(600, "Similarity between files to assume a " +
                    "copy/move [0..1000]"))
        self.config.add_option('file-min-lines',
                ConfigOptInt(10, "Minimal number of lines in a file for " +
                    "meaningful comparison"))
        self.config.add_option('verbose',
                ConfigOptInt(3, "Verbosity of the output [0..5]"))
        try:
            self.termwidth = os.get_terminal_size()[0]
        except OSError:
            # Not running in a terminal - probably redirected to file
            self.termwidth = 150 # arbitrary number

    def info(self, level, msg):
        'Print message with specified verbosity'
        if level <= self.config.get('verbose'):
            print(msg, flush=True)

    def scan(self):
        self.items = FSOCollection()
        self.info(1, "Scanning working copy directory...")
        self.get_lists(self.wcdir, S_WC, False)
        self.info(1, "Scanning imported directory...")
        self.get_lists(self.importdir, S_IM,
                self.config.get('symlink-handling') == "dereference")

    def get_lists(self, top, where, deref):
        for d, dn, fn in os.walk(top, followlinks=deref):
            dr = os.path.relpath(d, top)
            # If under .svn directory at the top (SVN 1.7+) or has .svn
            # in the path (older SVN), ignore
            if descendant_or_self(dr, '.svn') or \
                    os.path.basename(dr) == '.svn' or \
                    (os.sep + '.svn' + os.sep) in dr:
                continue
            if dr != '.':
                self.items.add(dr, where, "D")
            dnn = [] # List where we'll descend
            for f in fn + dn:
                fr = os.path.normpath(os.path.join(dr, f))
                frp = os.path.join(d, f)
                if os.path.islink(frp):
                    if deref:
                        # Dereferencing:
                        # - check for dangling/absolute/out-of-tree symlinks and abort
                        rl = os.readlink(frp)
                        if not os.path.exists(frp):
                            self.info(1, "WARN: Ignoring dangling symlink %s -> %s" % (fr, rl))
                            continue
                        if os.path.isabs(rl):
                            self.info(1, "WARN: Ignoring absolute symlink %s -> %s" % (fr, rl))
                            continue
                        tgt = os.path.normpath(os.path.join(dr, rl))
                        if tgt == ".." or tgt.startswith(".." + os.sep):
                            self.info(1, "WARN: Ignoring out-of-wc symlink %s -> %s" % (fr, rl))
                            continue
                    else:
                        # Importing symlinks as-is, no need to check.
                        self.items.add(fr, where, "L")
                        continue
                # If we get here, treat symlinks to files as regular files, and add directories
                # to the list of traversed subdirs
                if os.path.isfile(frp):
                    self.items.add(fr, where, "F")
                if os.path.isdir(frp):
                    dnn.append(f)
            dn[:] = dnn

    def onecmd(self, str):
        'Override for checking number of arguments'
        try:
            return cmd.Cmd.onecmd(self, str)
        except InvalidUsageException as e:
            if e.cmd is not None:
                print("!!! Invalid usage of `%s' command: %s" % (e.cmd, e))
                print("")
                self.onecmd("help " + e.cmd)
            else:
                print("!!! %s" % e)

    def parse_args(self, line, nargs, cmd):
        'Parse arguments for a command'
        args = shlex.split(line)
        if len(args) != nargs:
            raise InvalidUsageException(cmd, "expect %d arguments" % nargs)
        return args

    def run_svn(self, args_fixed, args_split=[]):
        'Run SVN command(s), potentially splitting long argument lists'
        rv = True
        pos = 0
        atatime = 100
        output = ""
        # svn treats '@' specially (peg revision); if there's such character in a
        # file name - append an empty peg revision
        args_fixed = list(map(lambda x: x + "@" if x.find("@") != -1 else x, args_fixed))
        args_split = list(map(lambda x: x + "@" if x.find("@") != -1 else x, args_split))
        while pos < len(args_split) or (pos == 0 and len(args_split) == 0):
            svnargs = ['svn'] + args_fixed + args_split[pos : pos + atatime]
            pos += atatime
            self.info(5, "Running: " + " ".join(map(shlex.quote, svnargs)))
            p = subprocess.Popen(args=svnargs, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, cwd=self.wcdir)
            so, se = p.communicate()
            if p.returncode != 0:
                print("`%s' exited with %d status:" %
                        (" ".join(map(shlex.quote, svnargs)), p.returncode))
                print(se.decode())
                rv = False
            else:
                output += so.decode()
        return rv, output

    def copy_or_move(self, op, src, dst):
        'Handle copy or move operation'
        if src not in self.items or self.items[src].state[S_WC] == "-":
            raise InvalidUsageException(None,
                    "Nothing known about `%s'" % src)
        if dst in self.items and self.items[dst].state[S_WC] != "-":
            raise InvalidUsageException(None,
                    "Destination path `%s' already exists" % dst)
        # Check that we're not creating dst under a file (not a dir)
        new_dirs = []
        def check_parent(d):
            if d not in self.items or self.items[d].state[S_WC] == "-":
                new_dirs.append(d)
            elif self.items[d].state[S_WC] == "F":
                raise InvalidUsageException(None,
                        "Destination path `%s' created under `%s' " +
                        "which is a file" % (dst, d))
        for_all_parents(dst, check_parent)
        # All ok, record new directories that may be created
        for d in new_dirs:
            self.items.get(d).state[S_WC] = "D"
        # Record the operation and update the FSO collection
        self.prepare_ops.append((op, src, dst))
        self.items.wc_copy(src, dst)
        if op == "mv":
            self.items.wc_remove(src)

    def remove(self, path):
        if path not in self.items or self.items[path].state[S_WC] == "-":
            raise InvalidUsageException(None,
                    "Nothing known about `%s'" % path)
        self.prepare_ops.append(("rm", path))
        self.items.wc_remove(path)

    def similarity_file(self, src, dst, threshold, lst_removal):
        'Compare two files, return similarity ratio on 0..1000 scale'
        if self.items[src].state[S_WC] != "F":
            return 0
        # Source is in working copy
        fn1 = os.path.join(self.wcdir, self.items[src].wc_path)
        # Destination is in imported dir
        fn2 = os.path.join(self.importdir, dst)
        minlines = self.config.get('file-min-lines')
        try:
            f1 = open(fn1, 'r')
            l1 = f1.readlines()
            f1.close()
            if len(l1) < minlines:
                return 0
            f2 = open(fn2, 'r')
            l2 = f2.readlines()
            f2.close()
            if len(l2) < minlines:
                return 0
            sm = difflib.SequenceMatcher(a=l1, b=l2)
            return int(1000 * sm.quick_ratio())
        except UnicodeDecodeError:
            # Oops, file seems to be binary. Fall back to comparing whole
            # file contents.
            if filecmp.cmp(fn1, fn2, shallow=False):
                return 1000
            return 0

    def _similarity_dir(self, src, dst, get_file_similarity, lst_removal):
        'Iterate over FSOs, using callback to compare file entries'
        common = 0
        total = 0
        for xsrc in self.items:
            if xsrc.startswith(src + os.sep):
                esrc = self.items[xsrc]
                if esrc.state[S_WC] == "-":
                    # Source not in WC - ignore for similarity calculation
                    continue
                skip = False
                if lst_removal is not None:
                    for i in lst_removal:
                        if descendant_or_self(xsrc, i):
                            skip = True
                if skip:
                    # Moved to another place, do not consider in score
                    continue
                total += 1000
                xdst = path_rebase(xsrc, src, dst)
                if xdst not in self.items:
                    # Destination not in imported sources - non-similar item
                    continue
                edst = self.items[xdst]
                if edst.state[S_IM] == esrc.state[S_WC]:
                    if esrc.state[S_WC] == "D":
                        common += 1000
                    else:
                        common += get_file_similarity(xsrc, xdst)
        if total == 0:
            # No files/subdirs in source directory - avoid copying empty dirs
            return 0
        return 1000 * common / total

    def similarity_dir(self, src, dst, threshold, lst_removal):
        '''
        Compare two dirs recursively, return similarity ratio on
        0..1000 scale.
        '''
        common = 0
        total = 0
        # Quickly estimate upper boundary by comparing file names. Only
        # concern ourselves with files in source directory. I.e., if
        # files were added after the move in the destination directory,
        # it's ok. If most of the files from the source directory were
        # removed, the directory is not considered similar - instead,
        # file move detection would move files one by one.
        upper = self._similarity_dir(src, dst, lambda s, d: 1000, lst_removal)
        if upper <= threshold:
            # Even the best estimate is worse than current cut-off
            return 0
        # Okay, looks roughly similar. Now redo the above procedure, but also
        # compare the file content.
        return self._similarity_dir(src, dst,
                lambda s, d: self.similarity_file(s, d, 0, lst_removal),
                lst_removal)

    def similar(self, src, dst, threshold=0, lst_removal=None):
        'Compare two FSOs, source in WC and destination in imported dir'
        if src not in self.items:
            print("Source `%s' not in the working copy" % src)
            return
        xsrc = self.items[src]
        if xsrc.state[S_WC] == "-":
            print("Source `%s' not in the working copy" % src)
            return
        if dst not in self.items:
            print("Destination `%s' not in imported sources" % dst)
            return
        xdst = self.items[dst]
        if xdst.state[S_IM] == "-":
            print("Destination `%s' not in imported sources" % dst)
            return
        if xsrc.state[S_WC] != xdst.state[S_IM]:
            # Different kinds - definitely not the same object
            return 0
        if xsrc.state[S_WC] == "L" or xdst.state[S_IM] == "L":
            # Symlinks are not considered the same object (same target in
            # different dirs refers to different objects).
            return 0
        if xsrc.state[S_WC] == "D":
            return self.similarity_dir(src, dst, threshold, lst_removal)
        else:
            return self.similarity_file(src, dst, threshold, lst_removal)

    def handle_op(self, op_tuple):
        'Handle one SVN operation, recorded as a tuple'
        def x_mv(src, dst):
            self.info(2, "  Move `%s' to `%s'" % (src, dst))
            self.copy_or_move("mv", src, dst)
        def x_cp(src, dst):
            self.info(2, "  Copy `%s' to `%s'" % (src, dst))
            self.copy_or_move("cp", src, dst)
        def x_rm(path):
            self.info(2, "  Remove `%s'" % path)
            self.remove(path)
        known_ops = {
                # key: (nargs, handler)
                'cp' : (3, x_cp),
                'mv' : (3, x_mv),
                'rm' : (2, x_rm),
                }
        if len(op_tuple) == 0:
            raise InvalidUsageException
        op = op_tuple[0]
        if op not in known_ops:
            return False
        nargs, func = known_ops[op]
        if nargs != len(op_tuple):
            return False
        func(*op_tuple[1:])
        return True

    def detect(self, thresholds):
        'Helper for finding copy/move destinations'
        ilst = []
        wlst = {}
        ilst_map = {}
        for p in self.items:
            e = self.items[p]
            if e.state[S_WC] != "-" and e.state[S_IM] == "-":
                wlst[p] = [] # wlst hash stores copy destinations
            elif e.state[S_WC] == "-" and e.state[S_IM] != "-":
                # ilst just lists destination paths as tuples with node kind
                ilst.append((e.state[S_IM], p))
        iteration = 0
        # Do not apply operations immediately - we'll need to post-process
        # them to account for files/dirs moved inside a moved parent dir.
        ops = []
        to_be_removed = []
        def get_renamed_name(path, rename_ops):
            '''
            Check if path was renamed/removed in the recorded operations,
            return new name.
            '''
            for op_tuple in rename_ops:
                # Since copies do not remove the source file, ignore them.
                # We push no 'rm' ops in this function
                if op_tuple[0] == "mv":
                    src = op_tuple[1]
                    dst = op_tuple[2]
                    if descendant_or_self(path, src):
                        path = path_rebase(path, src, dst)
            return path

        while len(wlst):
            iteration += 1
            self.info(2, ("Iteration %d: Possible sources: %d, " +
                    "possible destinations: %d") %
                    (iteration, len(wlst), len(ilst)))
            ndst = len(ilst)
            for idx, (nk, dst) in enumerate(sorted(ilst,
                    key=lambda s: filename_sort_key(s[1]))):
                class SkipDestFile(Exception):
                    pass
                # Check if moved as a part of a parent directory.
                def check_moved_parent(xdst):
                    if xdst in ilst_map:
                        src = path_rebase(dst, xdst, ilst_map[xdst])
                        # Did it exist in copied directory?
                        if src in self.items and \
                                self.items[src].state[S_WC] == nk:
                            sim = self.similar(src, dst, thresholds[nk],
                                    to_be_removed)
                            if sim > thresholds[nk]:
                                self.info(2, ("  [%04d/%04d] Skipping `%s' " +
                                        "(copied as part of `%s')") %
                                        (idx, ndst, dst, xdst))
                                raise SkipDestFile
                            # Copied, not similar - search for other sources
                            raise StopIteration
                try:
                    for_all_parents(dst, check_moved_parent)
                except SkipDestFile:
                    continue
                except StopIteration:
                    pass
                self.info(2, ("  [%04d/%04d] Looking for possible source " +
                        "for `%s'") % (idx, ndst, dst))
                bestsrc = None
                # Won't even consider those lower than threshold
                bestsim = thresholds[nk]
                for src in sorted(wlst.keys(),
                        key=lambda x: name_similarity(x, dst)):
                    sim = self.similar(src, dst, bestsim, to_be_removed)
                    if sim > bestsim:
                        self.info(3, "    [similarity %4d] %s" % (sim, src))
                        bestsim = sim
                        bestsrc = src
                    if bestsim == 1000:
                        # No chance we're finding anything better
                        break
                if bestsrc is not None:
                    wlst[bestsrc].append(dst)
                    ilst_map[dst] = bestsrc

            # Discovered all copies/moves, now record them.
            new_wlst = {}
            for src in sorted(wlst.keys(), key=filename_sort_key):
                dlist = wlst[src]
                if len(dlist) == 0:
                    continue
                if len(dlist) == 1:
                    ops.append(("mv", src, dlist[0]))
                    to_be_removed.append(src)
                else:
                    # We don't remove the source here, it will be done when
                    # the changes are applied (it will remove all the WC files
                    # not found in imported sources). Avoiding removal here
                    # simplifies operation sorting below, since we would not
                    # be concerned with source file/dir disappearing before
                    # it is copied to its destination.
                    to_be_removed.append(src)
                    for d in dlist:
                        ops.append(("cp", src, d))
                # If we copied something - recheck parent source directories.
                # Since some source file/dir was scheduled to be removed,
                # this may have increased the similarity to some destination.
                def recheck_parent(x):
                    if x in wlst and len(wlst) == 0:
                        new_wlst[x] = []
                for_all_parents(src, recheck_parent)

            # At this point, if we're going to have the next iteration, we
            # are only concerned about directories (by the way new_wlst is
            # created above). So, filter out all files from ilst as well.
            wlst = new_wlst
            ilst = list(filter(lambda t: t[0] == 'D', ilst))

        # Finished collecting the operations - now can post-process and
        # apply them. First, sort copies/moves by destination (so that
        # parent directories are created before files/subdirs are
        # copied/renamed inside)
        ops = sorted(ops, key=lambda op: filename_sort_key(op[2]))
        for i, op_tuple in enumerate(ops):
            # For each operation, go over its precedents to see if the source
            # has been renamed. If it is, find out new name.
            op = op_tuple[0]
            src = get_renamed_name(op_tuple[1], reversed(ops[:i]))
            if src != op_tuple[2]:
                # Unless it became the same file after renames
                try:
                    # Try to remove the destination, if it existed
                    self.remove(op_tuple[2])
                except InvalidUsageException:
                    # Okay, it didn't exist
                    pass
                self.handle_op((op, src, op_tuple[2]))

    def do_detect(self, arg):
        '''
        detect : auto-detect possible moves (where source/destination name
                 is unique). If not all moves are applicable, save move list,
                 edit and load.
        '''
        self.parse_args(arg, 0, "detect")
        # Configurable for file/dirs; symlinks are never similar.
        self.detect({ "D": self.config.get('dir-similarity'),
            "F": self.config.get('file-similarity'),
            "L": 1001 })

    def do_apply(self, arg):
        '''
        apply : Perform copies/renames; then copy imported sources into
                the working copy. Modifies working copy. Exits after
                completion.
        '''
        self.info(1, "Copying imported sources into working copy...")
        # Perform the recorded copies/moves/removals
        self.info(2, "  Preparatory operations (copies/renames/removals)")
        to_be_diffed = []
        for o in self.prepare_ops:
            op = o[0]
            if op == "mv":
                self.run_svn(["mv", "--parents", o[1], o[2]])
                to_be_diffed.append(o[2])
            elif op == "cp":
                self.run_svn(["cp", "--parents", o[1], o[2]])
                to_be_diffed.append(o[2])
            elif op == "rm":
                # --force, as the removed path is likely created as a result
                # of previous copy/rename
                self.run_svn(["rm", "--force", o[1]])
        dirs_added = []
        dirs_removed = []
        files_added = []
        files_removed = []
        files_set_exec = []
        files_clear_exec = []

        self.info(2, "  Creating dirs and copying files...")
        def copyfile_helper(i, nk_wc):
            '''Helper: copy a file and optionally, transfer permissions.'''
            f = os.path.join(self.importdir, i)
            t = os.path.join(self.wcdir, i)
            shutil.copyfile(f, t)
            # If exec-permission is 'clear', we don't need to do anything:
            # shutil.copyfile will create the file as non-executable.
            if self.config.get('exec-permission') == 'preserve':
                # If the file is new, just copying the mode is enough:
                # svn will set the svn:executable upon adding it.
                if nk_wc == "F":
                    # Existing file, check what the setting shall be
                    if os.access(f, os.X_OK) and not os.access(t, os.X_OK):
                        files_set_exec.append(i)
                    elif not os.access(f, os.X_OK) and os.access(t, os.X_OK):
                        files_clear_exec.append(i)
                shutil.copymode(f, t)

        for i in sorted(self.items.keys()):
            e = self.items[i]
            nk_wc = e.state[S_WC]
            nk_im = e.state[S_IM]
            flg = None
            if nk_wc == "-":
                # Absent in working copy
                if nk_im == "D":
                    # Directory added
                    os.mkdir(os.path.join(self.wcdir, i))
                    dirs_added.append(i)
                    flg = "(added dir)"
                elif nk_im == "F":
                    # New file added
                    copyfile_helper(i, nk_wc);
                    files_added.append(i)
                    flg = "(added file)"
                elif nk_im == "L":
                    tim = os.readlink(os.path.join(self.importdir, i))
                    os.symlink(tim, os.path.join(self.wcdir, i))
                    files_added.append(i)
                    flg = "(added symlink)"
                else:
                    # Not in imported sources, not in WC (moved
                    # away/removed) - nothing to do
                    pass
            elif nk_wc == "L":
                # Symbolic link in a working copy
                if nk_im == "L":
                    # Symbolic link in both. If the same target, do nothing. Otherwise,
                    # replace.
                    twc = os.readlink(os.path.join(self.wcdir, i))
                    tim = os.readlink(os.path.join(self.importdir, i))
                    if tim != twc:
                        self.run_svn(["rm", "--force", i])
                        os.symlink(tim, os.path.join(self.wcdir, i))
                        files_added.append(i)
                        flg = "(replaced symlink)"
                elif nk_im == "D":
                    # Was a symlink, now a directory. Replace.
                    self.run_svn(["rm", "--force", i])
                    os.mkdir(os.path.join(self.wcdir, i))
                    dirs_added.append(i)
                    flg = "(replaced symlink with dir)"
                elif nk_im == "F":
                    # Symlink replaced with file.
                    self.run_svn(["rm", "--force", i])
                    copyfile_helper(i, nk_wc);
                    files_added.append(i)
                    flg = "(replaced symlink with file)"
                else:
                    # Was a symlink, removed
                    files_removed.append(i)
                    flg = "(removed symlink)"
            elif nk_wc == "F":
                # File in a working copy
                if nk_im == "D":
                    # File replaced with a directory. See comment above.
                    self.run_svn(["rm", "--force", i])
                    os.mkdir(os.path.join(self.wcdir, i))
                    dirs_added.append(i)
                    flg = "(replaced file with dir)"
                elif nk_im == "F":
                    # Was a file, is a file - just copy contents
                    copyfile_helper(i, nk_wc);
                    flg = "(copied)"
                elif nk_im == "L":
                    # Was a file, now a symlink. Replace.
                    self.run_svn(["rm", "--force", i])
                    tim = os.readlink(os.path.join(self.importdir, i))
                    os.symlink(tim, os.path.join(self.wcdir, i))
                    files_added.append(i)
                    flg = "(replaced file with symlink)"
                else:
                    # Was a file, removed
                    files_removed.append(i)
                    flg = "(removed file)"
            elif nk_wc == "D":
                # Directory in a working copy
                if nk_im == "D":
                    # Was a directory, is a directory - nothing to do
                    pass
                elif nk_im == "F":
                    # Directory replaced with file. Need to remove dir
                    # immediately, as bulk removals/additions assume new files
                    # and dirs already in place. Also, removing a directory
                    # removes all its descendants - mark them as removed.
                    self.run_svn(["rm", "--force", i])
                    self.items.wc_remove(i)
                    copyfile_helper(i, nk_wc);
                    files_added.append(i)
                    flg = "(replaced dir with file)"
                elif nk_im == "L":
                    # Was a directory, now a symlink. Replace.
                    self.run_svn(["rm", "--force", i])
                    self.items.wc_remove(i)
                    tim = os.readlink(os.path.join(self.importdir, i))
                    os.symlink(tim, os.path.join(self.wcdir, i))
                    files_added.append(i)
                    flg = "(replaced dir with symlink)"
                else:
                    # Directory removed
                    dirs_removed.append(i)
                    flg = "(removed dir)"
            if flg is not None:
                self.info(4, "    %s %s %s" % (e.status(), i, flg))
        # Filter files/directories removed as a part of parent directory
        files_removed = list(filter(lambda x: os.path.dirname(x) not in
                dirs_removed, files_removed))
        dirs_removed = list(filter(lambda x: os.path.dirname(x) not in
            dirs_removed, dirs_removed))
        files_added = list(filter(lambda x: os.path.dirname(x) not in
            dirs_added, files_added))
        dirs_added = list(filter(lambda x: os.path.dirname(x) not in
            dirs_added, dirs_added))
        self.info(2, "  Running SVN add/rm/propset/propdel commands");
        if len(dirs_added):
            self.run_svn(["add"], dirs_added)
        if len(files_added):
            self.run_svn(["add"], files_added)
        if len(dirs_removed):
            self.run_svn(["rm"], dirs_removed)
        if len(files_removed):
            self.run_svn(["rm"], files_removed)
        if len(files_set_exec):
            self.run_svn(["propset", "svn:executable", "*"], files_set_exec)
        if len(files_clear_exec):
            self.run_svn(["propdel", "svn:executable"], files_clear_exec)
        # Save the diff for the copied/moved items
        diff_save = self.config.get('save-diff-copied')
        if diff_save is not None:
            self.info(2, "  Saving 'svn diff' on copied files/dirs to `%s'" %
                    diff_save)
            to_be_diffed = list(filter(lambda x: os.path.dirname(x) not in
                to_be_diffed, to_be_diffed))
            if len(to_be_diffed):
                try:
                    rv, out = self.run_svn(["diff"], to_be_diffed)
                except UnicodeDecodeError:
                    # Some binary files not marked with appropriate MIME type,
                    # or broken text files
                    rv, out = (True, "WARNING: diff contained binary files\n")
            else:
                rv, out = (True, "")
            if rv:
                f = open(diff_save, "w")
                f.write(out)
                f.close()
        # Exiting, as the resulting working copy can no longer be used
        # for move analysis
        self.info(1, "Done. Exiting; please examine the working copy " +
                "and commit.")
        return True

    def do_similarity(self, arg):
        '''
        similarity SRD DST : estimate whether SRC could be potential source
                             for DST (0=no match, 1000=perfect match)
        '''
        src, dst = self.parse_args(arg, 2, "similarity")
        sim = self.similar(src, dst)
        if sim is not None:
            print("Similarity between source `%s' and destination `%s': %4d" %
                    (src, dst, sim))

    def do_set(self, arg):
        '''
        set         : display current settings
        set CFG VAL : set a config variable
        '''
        if arg.strip() == '':
            self.config.print()
        else:
            cfg, val = self.parse_args(arg, 2, "set")
            self.config.set(cfg, val)

    def do_move(self, arg):
        '''
        move SRC DST : Perform a move from source to destination
        '''
        src, dst = self.parse_args(arg, 2, "move")
        self.copy_or_move("mv", src, dst)

    def do_copy(self, arg):
        '''
        copy SRC DST : Perform a copy from source to destination
        '''
        src, dst = self.parse_args(arg, 2, "copy")
        self.copy_or_move("cp", src, dst)

    def do_remove(self, arg):
        '''
        remove PATH : Remove a path
        '''
        path = self.parse_args(arg, 1, "remove")[0]
        self.copy_or_move("rm", path)

    def do_lsprep(self, arg):
        '''
        lsprep : List the currently recorded moves/copies/removals
        '''
        self.parse_args(arg, 0, "lsprep")
        colsz = int((self.termwidth - 14) / 2)
        if len(self.prepare_ops):
            print("Currently recorded preparatory operations:")
            print("")
            print("%5s  %s  %-*s  %-*s" %
                    ("#", "Op", colsz, "Source", colsz, "Destination"))
            for id, o in enumerate(self.prepare_ops):
                if id % 10 == 0:
                    print("%5s  %s  %*s  %*s" %
                            ("-"*5, "--", colsz, "-"*colsz, colsz, "-"*colsz))
                if len(o) == 3:
                    print("%5d  %s  %-*s  %-*s" %
                            (id, o[0], colsz, o[1], colsz, o[2]))
                else:
                    print("%5d  %s  %-*s" % (id, o[0], colsz, o[1]))
            print("")
        else:
            print("No copies/moves/removals recorded")
            print("")

    def do_save(self, arg):
        '''
        save FILENAME : Save current preparation operations to a file
        '''
        fn = self.parse_args(arg, 1, "save")[0]
        f = open(fn, 'w')
        longestname = 0
        for o in self.prepare_ops:
            if len(o[1]) > longestname:
                longestname = len(o[1])
            if len(o) == 3 and len(o[2]) > longestname:
                longestname = len(o[2])
        for o in self.prepare_ops:
            if len(o) == 2:
                f.write("svn %s %-*s\n" %
                        (o[0], longestname, shlex.quote(o[1])))
            else:
                f.write("svn %s %-*s %-*s\n" %
                        (o[0], longestname, shlex.quote(o[1]),
                            longestname, shlex.quote(o[2])))
            pass
        f.close()

    def do_load(self, arg):
        '''
        load FILENAME : Load/append preparation operations from a file
        '''
        fn = self.parse_args(arg, 1, "load")[0]
        self.info(1, "Performing operations from `%s'" % fn)
        f = open(fn, 'r')
        for l in f.readlines():
            if l[0] == '#':
                continue
            args = shlex.split(l)
            try:
                if len(args) < 2 or args[0] != 'svn':
                    raise InvalidUsageException(None, "")
                self.handle_op(args[1:])
            except InvalidUsageException as e:
                # Rethrow
                raise InvalidUsageException(None,
                        "Invalid line in file: %s(%s)" % (l, e))
        f.close()

    def do_svninfo(self, arg):
        '''
        svninfo : Display SVN info on the working copy (debug)
        '''
        self.parse_args(arg, 0, "svninfo")
        print(str(self.svninfo))

    def do_printlst(self, arg):
        '''
        printlst WHAT : Print list of files; WHAT is one of {dir,file} (debug)
        '''
        self.parse_args(arg, 0, "printlst")
        self.items.print()

    def do_help(self, arg):
        '''
        help [COMMAND] : Print the help message
        '''
        cmd.Cmd.do_help(self, arg)

    def do_EOF(self, arg):
        '''
        Quit the script
        '''
        return True

    def do_quit(self, arg):
        '''
        quit : Quit the script
        '''
        return True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
            description="Prepare a working copy for SVN vendor import.")
    parser.add_argument('wcdir',
            help="Path to working copy (destination of import)")
    parser.add_argument('importdir',
            help="Path to imported sources (source of import)")
    grp = parser.add_mutually_exclusive_group()
    grp.add_argument('--auto', action='store_true',
            help="Automatic mode: detect moves, apply them and copy sources")
    grp.add_argument('--detect', metavar='FILE',
            help="Semi-automatic mode: detect moves and save them to FILE")
    grp.add_argument('--apply', metavar='FILE',
            help="Semi-automatic mode: apply the moves from FILE " +
            "and copy the sources")
    parser.add_argument('--save', metavar='FILE',
            help="Automatic mode: save moves to FILE after detection, " +
            "then proceed to apply the changes")
    parser.add_argument('--config', metavar=('OPT','VALUE'), action='append',
            nargs=2, help="Set configuration option OPT to VALUE")
    args = parser.parse_args()
    p = subprocess.Popen(args=['svn', 'info', args.wcdir],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    so, se = p.communicate()
    if p.returncode != 0:
        print("%s: does not appear to be SVN working copy." % args.wcdir)
        print("`svn info' exited with status %d and returned:" % p.returncode)
        print("")
        print(se.decode())
        sys.exit(1)
    imp = SvnVndImport(args.wcdir, args.importdir, so.decode())
    if args.config:
        try:
            for o, v in args.config:
                imp.config.set(o, v)
        except InvalidUsageException as e:
            parser.error(e)
    imp.scan()
    if args.auto:
        imp.onecmd("detect")
        if args.save:
            imp.onecmd("save " + shlex.quote(args.save))
        imp.onecmd("apply")
    elif args.detect:
        imp.onecmd("detect")
        imp.onecmd("save " + shlex.quote(args.detect))
    elif args.apply:
        imp.onecmd("load " + shlex.quote(args.apply))
        imp.onecmd("apply")
    else:
        imp.cmdloop()
