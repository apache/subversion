#!/usr/bin/env python
# -*-mode: python; coding: utf-8 -*-
#
# Inspired from svn-import.py by astrand@cendio.se (ref :
# http://svn.haxx.se/users/archive-2006-10/0857.shtml)
#
# svn-merge-vendor.py (v1.0.1) - Import a new release, such as a vendor drop.
#
# The "Vendor branches" chapter of "Version Control with Subversion"
# describes how to do a new vendor drop with:
#
# >The goal here is to make our current directory contain only the
# >libcomplex 1.1 code, and to ensure that all that code is under version
# >control. Oh, and we want to do this with as little version control
# >history disturbance as possible.
#
# This utility tries to take you to this goal - automatically. Files
# new in this release is added to version control, and files removed
# in this new release are removed from version control.  It will
# detect the moved files by looking in the svn log to find the
# "copied-from" path !
#
# Compared to svn_load_dirs.pl, this utility:
#
# * DETECTS THE MOVED FILES !!
# * Does not hard-code commit messages
# * Allows you to fine-tune the import before commit, which
#   allows you to turn adds+deletes into moves.
#
# TODO :
#   * support --username and --password
#
# This tool is provided under GPL license.  Please read
# http://www.gnu.org/licenses/gpl.html for the original text.
#
# $HeadURL$
# $LastChangedRevision$
# $LastChangedDate$
# $LastChangedBy$

import os
import re
import tempfile
import atexit
import subprocess
import shutil
import sys
import getopt
import logging
import string
from StringIO import StringIO
# lxml module can be found here : http://codespeak.net/lxml/
from lxml import etree
import types

prog_name = os.path.basename(sys.argv[0])
orig_svn_subroot = None
base_copied_paths = []
r_from = None
r_to = None
log_tree = None
entries_to_treat = []
entries_to_delete = []
added_paths = []
logger = None

def del_temp_tree(tmpdir):
    """Delete tree, standring in the root"""
    global logger
    logger.info("Deleting tmpdir "+tmpdir)
    os.chdir("/")
    try:
        shutil.rmtree(tmpdir)
    except OSError:
        print logger.warn("Couldn't delete tmpdir %s. Don't forget to remove it manually." % (tmpdir))


def checkout(url, revision=None):
    """Checks out the given URL at the given revision, using HEAD if not defined. Returns the working copy directory"""
    global logger
    # Create a temp dir to hold our working copy
    wc_dir = tempfile.mkdtemp(prefix=prog_name)
    atexit.register(del_temp_tree, wc_dir)

    if (revision):
        url += "@"+revision

    # Check out
    logger.info("Checking out "+url+" to "+wc_dir)
    returncode = call_cmd(["svn", "checkout", url, wc_dir])

    if (returncode == 1):
        return None
    else:
        return wc_dir

def merge(wc_dir, revision_from, revision_to):
    """Merges repo_url from revision revision_from to revision revision_to into wc_dir"""
    global logger
    logger.info("Merging between revisions %s and %s into %s" % (revision_from, revision_to, wc_dir))
    os.chdir(wc_dir)
    return call_cmd(["svn", "merge", "-r", revision_from+":"+revision_to, wc_dir])

def treat_status(wc_dir_orig, wc_dir):
    """Copies modification from official vendor branch to wc"""
    global logger
    logger.info("Copying modification from official vendor branch %s to wc %s" % (wc_dir_orig, wc_dir))
    os.chdir(wc_dir_orig)
    status_tree = call_cmd_xml_tree_out(["svn", "status", "--xml"])
    global entries_to_treat, entries_to_delete
    entries_to_treat = status_tree.xpath("/status/target/entry")
    entries_to_delete = []

    while len(entries_to_treat) > 0:
        entry = entries_to_treat.pop(0)
        entry_type = get_entry_type(entry)
        file = get_entry_path(entry)
        if entry_type == 'added':
            if is_entry_copied(entry):
                check_exit(copy(wc_dir_orig, wc_dir, file), "Error during copy")
            else:
                check_exit(add(wc_dir_orig, wc_dir, file), "Error during add")
        elif entry_type == 'deleted':
            entries_to_delete.append(entry)
        elif entry_type == 'modified' or entry_type == 'replaced':
            check_exit(update(wc_dir_orig, wc_dir, file), "Error during update")
        elif entry_type == 'normal':
            logger.info("File %s has a 'normal' state (unchanged). Ignoring." % (file))
        else:
            logger.error("Status not understood : '%s' not supported (file : %s)" % (entry_type, file))

    # We then treat the left deletions
    for entry in entries_to_delete:
        check_exit(delete(wc_dir_orig, wc_dir, get_entry_path(entry)), "Error during delete")

    return 0

def get_entry_type(entry):
    return get_xml_text_content(entry, "wc-status/@item")

def get_entry_path(entry):
    return get_xml_text_content(entry, "@path")

def is_entry_copied(entry):
    return get_xml_text_content(entry, "wc-status/@copied") == 'true'

def copy(wc_dir_orig, wc_dir, file):
    global logger
    logger.info("A+ %s" % (file))

    # Retrieving the original URL
    os.chdir(wc_dir_orig)
    info_tree = call_cmd_xml_tree_out(["svn", "info", "--xml", os.path.join(wc_dir_orig, file)])
    url = get_xml_text_content(info_tree, "/info/entry/url")

    # Detecting original svn root
    global orig_svn_subroot
    if not orig_svn_subroot:
        orig_svn_root = get_xml_text_content(info_tree, "/info/entry/repository/root")
        #print >>sys.stderr, "url : %s" % (url)
        sub_url = url.split(orig_svn_root)[-1]
        sub_url = os.path.normpath(sub_url)
        #print >>sys.stderr, "sub_url : %s" % (sub_url)
        if sub_url.startswith(os.path.sep):
            sub_url = sub_url[1:]

        orig_svn_subroot = '/'+sub_url.split(file)[0].replace(os.path.sep, '/')
        #print >>sys.stderr, "orig_svn_subroot : %s" % (orig_svn_subroot)

    global log_tree
    if not log_tree:
        # Detecting original file copy path
        os.chdir(wc_dir_orig)
        orig_svn_root_subroot = get_xml_text_content(info_tree, "/info/entry/repository/root") + orig_svn_subroot
        real_from = str(int(r_from)+1)
        logger.info("Retrieving log of the original trunk %s between revisions %s and %s ..." % (orig_svn_root_subroot, real_from, r_to))
        log_tree = call_cmd_xml_tree_out(["svn", "log", "--xml", "-v", "-r", "%s:%s" % (real_from, r_to), orig_svn_root_subroot])

    # Detecting the path of the original moved or copied file
    orig_url_file = orig_svn_subroot+file.replace(os.path.sep, '/')
    orig_url_file_old = None
    #print >>sys.stderr, "  orig_url_file : %s" % (orig_url_file)
    while orig_url_file:
        orig_url_file_old = orig_url_file
        orig_url_file = get_xml_text_content(log_tree, "//path[(@action='R' or @action='A') and text()='%s']/@copyfrom-path" % (orig_url_file))
        logger.debug("orig_url_file : %s" % (orig_url_file))
    orig_url_file = orig_url_file_old

    # Getting the relative url for the original url file
    if orig_url_file:
        orig_file = convert_relative_url_to_path(orig_url_file)
    else:
        orig_file = None
    global base_copied_paths, added_paths
    # If there is no "moved origin" for that file, or the origin doesn't exist in the working directory, or the origin is the same as the given file, or the origin is an added file
    if not orig_url_file or (orig_file and (not os.path.exists(os.path.join(wc_dir, orig_file)) or orig_file == file or orig_file in added_paths)):
        # Check if the file is within a recently copied path
        for path in base_copied_paths:
            if file.startswith(path):
                logger.warn("The path %s to add is a sub-path of recently copied %s. Ignoring the A+." % (file, path))
                return 0
        # Simple add the file
        logger.warn("Log paths for the file %s don't correspond with any file in the wc. Will do a simple A." % (file))
        return add(wc_dir_orig, wc_dir, file)

    # We catch the relative URL for the original file
    orig_file = convert_relative_url_to_path(orig_url_file)

    # Detect if it's a move
    cmd = 'copy'
    global entries_to_treat, entries_to_delete
    if search_and_remove_delete_entry(entries_to_treat, orig_file) or search_and_remove_delete_entry(entries_to_delete, orig_file):
        # It's a move, removing the delete, and treating it as a move
        cmd = 'move'

    logger.info("%s from %s" % (cmd, orig_url_file))
    returncode = call_cmd(["svn", cmd, os.path.join(wc_dir, orig_file), os.path.join(wc_dir, file)])
    if returncode == 0:
        if os.path.isdir(os.path.join(wc_dir, orig_file)):
            base_copied_paths.append(file)
        else:
            # Copy the last version of the file from the original repository
            shutil.copy(os.path.join(wc_dir_orig, file), os.path.join(wc_dir, file))
    return returncode

def search_and_remove_delete_entry(entries, orig_file):
    for entry in entries:
        if get_entry_type(entry) == 'deleted' and get_entry_path(entry) == orig_file:
            entries.remove(entry)
            return True
    return False

def convert_relative_url_to_path(url):
    global orig_svn_subroot
    return os.path.normpath(url.split(orig_svn_subroot)[-1])

def new_added_path(returncode, file):
    if not is_returncode_bad(returncode):
        global added_paths
        added_paths.append(file)

def add(wc_dir_orig, wc_dir, file):
    global logger
    logger.info("A  %s" % (file))
    if os.path.exists(os.path.join(wc_dir, file)):
        logger.warn("Target file %s already exists. Will do a simple M" % (file))
        return update(wc_dir_orig, wc_dir, file)
    os.chdir(wc_dir)
    if os.path.isdir(os.path.join(wc_dir_orig, file)):
        returncode = call_cmd(["svn", "mkdir", file])
        new_added_path(returncode, file)
        return returncode
    else:
        shutil.copy(os.path.join(wc_dir_orig, file), os.path.join(wc_dir, file))
        returncode = call_cmd(["svn", "add", file])
        new_added_path(returncode, file)
        return returncode

def delete(wc_dir_orig, wc_dir, file):
    global logger
    logger.info("D  %s" % (file))
    os.chdir(wc_dir)
    if not os.path.exists(file):
        logger.warn("File %s doesn't exist. Ignoring D." % (file))
        return 0
    return call_cmd(["svn", "delete", file])

def update(wc_dir_orig, wc_dir, file):
    global logger
    logger.info("M  %s" % (file))
    if os.path.isdir(os.path.join(wc_dir_orig, file)):
        logger.warn("%s is a directory. Ignoring M." % (file))
        return 0
    shutil.copy(os.path.join(wc_dir_orig, file), os.path.join(wc_dir, file))
    return 0

def fine_tune(wc_dir):
    """Gives the user a chance to fine-tune"""
    alert(["If you want to fine-tune import, do so in working copy located at : %s" % (wc_dir),
        "When done, press Enter to commit, or Ctrl-C to abort."])

def alert(messages):
    """Wait the user to <ENTER> or abort the program"""
    for message in messages:
        print >> sys.stderr, message
    try:
        return sys.stdin.readline()
    except KeyboardInterrupt:
        sys.exit(0)

def commit(wc_dir, message):
    """Commits the wc_dir"""
    os.chdir(wc_dir)
    cmd = ["svn", "commit"]
    if (message):
        cmd += ["-m", message]
    return call_cmd(cmd)

def tag_wc(repo_url, current, tag, message):
    """Tags the wc_dir"""
    cmd = ["svn", "copy"]
    if (message):
        cmd += ["-m", message]
    return call_cmd(cmd + [repo_url+"/"+current, repo_url+"/"+tag])

def call_cmd(cmd):
    global logger
    logger.debug(string.join(cmd, ' '))
    return subprocess.call(cmd, stdout=DEVNULL, stderr=sys.stderr)#subprocess.STDOUT)

def call_cmd_out(cmd):
    global logger
    logger.debug(string.join(cmd, ' '))
    return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=sys.stderr).stdout

def call_cmd_str_out(cmd):
    out = call_cmd_out(cmd)
    str_out = ""
    for line in out.readlines():
        str_out += line
    out.close()
    return str_out

def call_cmd_xml_tree_out(cmd):
    return etree.parse(StringIO(call_cmd_str_out(cmd)))

def get_xml_text_content(xml_doc, xpath):
    result_nodes = xml_doc.xpath(xpath)
    if result_nodes:
        if type(result_nodes[0]) == types.StringType:
            return result_nodes[0]
        else:
            return result_nodes[0].text
    else:
        return None

def usage(error = None):
    """Print usage message and exit"""
    print >>sys.stderr, """%s: Merges the difference between two revisions of the original repository of the vendor, to the vendor branch
usage: %s [options] REPO_URL CURRENT_PATH ORIGINAL_REPO_URL -r N:M

  - REPO_URL : repository URL for the vendor branch (i.e: http://svn.example.com/repos/vendor/libcomplex)
  - CURRENT_PATH : relative path to the current folder (i.e: current)
  - ORIGINAL_REPO_URL : original base repository URL
  - N:M : from revision N to revision M

  This command executes these steps:

  1. Check out directory specified by ORIGINAL_REPO_URL@N in a temporary directory.(1)
  2. Merges changes to revision M.(1)
  3. Check out directory specified by REPO_URL in a second temporary directory.(2)
  4. Treat the merge by "svn status" on the working copy of ORIGINAL_REPO_URL. If the history is kept ('+' when svn st), do a move instead of a delete / add.
  5. Allow user to fine-tune import.
  6. Commit.
  7. Optionally tag new release.
  8. Delete the temporary directories.

  (1) : if -c wasn't passed
  (2) : if -w wasn't passed

Valid options:
  -r [--revision] N:M      : specify revisions N to M
  -h [--help]              : show this usage
  -t [--tag] arg           : copy new release to directory ARG, relative to REPO_URL,
                             using automatic commit message. Example:
                             -t ../0.42
  --non-interactive        : do no interactive prompting, do not allow manual fine-tune
  -m [--message] arg       : specify commit message ARG
  -v [--verbose]           : verbose mode
  -c [--merged-vendor] arg : working copy path of the original already merged vendor trunk (skips the steps 1. and 2.)
  -w [--current-wc] arg    : working copy path of the current checked out trunk of the vendor branch (skips the step 3.)
    """ % ((prog_name,) * 2)

    if error:
        print >>sys.stder, "", "Current error : "+error

    sys.exit(1)

def main():
    tag = None
    message = None
    interactive = 1
    revision_to_parse = None
    merged_vendor = None
    wc_dir = None

    # Initializing logger
    global logger
    logger = logging.getLogger('svn-merge-vendor')
    hdlr = logging.StreamHandler(sys.stderr)
    formatter = logging.Formatter('%(levelname)-8s %(message)s')
    hdlr.setFormatter(formatter)
    logger.addHandler(hdlr)
    logger.setLevel(logging.INFO)

    try:
        opts, args = getopt.gnu_getopt(sys.argv[1:], "ht:m:vr:c:w:",
                                       ["help", "tag", "message", "non-interactive", "verbose", "revision", "merged-vendor", "current-wc"])
    except getopt.GetoptError:
        # print help information and exit:
        usage()

    for o, a in opts:
        if o in ("-h", "--help"):
            usage()
        if o in ("-t", "--tag"):
            tag = a
        if o in ("-m", "--message"):
            message = a
        if o in ("--non-interactive"):
            interactive = 0
        if o in ("-v", "--verbose"):
            logger.setLevel(logging.DEBUG)
        if o in ("-r", "--revision"):
            revision_to_parse = a
        if o in ("-c", "--merged-vendor"):
            merged_vendor = a
        if o in ("-w", "--current-wc"):
            wc_dir = a

    if len(args) != 3:
        usage()

    repo_url, current_path, orig_repo_url = args[0:3]

    if (not revision_to_parse):
        usage("the revision numbers are mendatory")
    global r_from, r_to
    r_from, r_to = re.match("(\d+):(\d+)", revision_to_parse).groups()

    if not r_from or not r_to:
        usage("the revision numbers are mendatory")

    try:
        r_from_int = int(r_from)
        r_to_int = int(r_to)
    except ValueError:
        usage("the revision parameter is not a number")

    if r_from_int >= r_to_int:
        usage("the 'from revision' must be inferior to the 'to revision'")

    if not merged_vendor:
        if orig_repo_url.startswith("http://"):
            wc_dir_orig = checkout(orig_repo_url, r_from)
            check_exit(wc_dir_orig, "Error during checkout")

            check_exit(merge(wc_dir_orig, r_from, r_to), "Error during merge")
        else:
            usage("ORIGINAL_REPO_URL must start with 'http://'")
    else:
        wc_dir_orig = merged_vendor

    if not wc_dir:
        wc_dir = checkout(repo_url+"/"+current_path)
        check_exit(wc_dir, "Error during checkout")

    check_exit(treat_status(wc_dir_orig, wc_dir), "Error during resolving")

    if (interactive):
        fine_tune(wc_dir)

    if not message:
        message = "New vendor version, upgrading from revision %s to revision %s" % (r_from, r_to)
        alert(["No message was specified to commit, the program will use that default one : '%s'" % (message),
            "Press Enter to commit, or Ctrl-C to abort."])

    check_exit(commit(wc_dir, message), "Error during commit")

    if tag:
        if not message:
            message = "Tag %s, when upgrading the vendor branch from revision %s to revision %s" % (tag, r_from, r_to)
            alert(["No message was specified to tag, the program will use that default one : '%s'" % (message),
                "Press Enter to tag, or Ctrl-C to abort."])
        check_exit(tag_wc(repo_url, current_path, tag, message), "Error during tag")

    logger.info("Vendor branch merged, passed from %s to %s !" % (r_from, r_to))

def is_returncode_bad(returncode):
    return returncode is None or returncode == 1

def check_exit(returncode, message):
    global logger
    if is_returncode_bad(returncode):
        logger.error(message)
        sys.exit(1)

if __name__ == "__main__":
    if (os.name == "nt"):
        DEVNULL = open("nul:", "w")
    else:
        DEVNULL = open("/dev/null", "w")
    main()
