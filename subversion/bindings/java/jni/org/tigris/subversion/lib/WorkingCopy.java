package org.tigris.subversion.lib;

/**
 * public interface for the Subversion Working Copy Library
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 *
 * Remark: this class corresponds to the subversion c api type
 * 'svn_txdelta_op_t'
 */

import org.tigris.subversion.SubversionException;
import java.util.Hashtable;

public interface WorkingCopy {
  /**
   * @return true iff PATH is a valid working copy directory, else
   * set it to false.  PATH must exist, either as a file or directory,
   * else an error will be returned.
   */
  public boolean checkWC(String path) throws SubversionException;

  /**
   * @return true if FILENAME's text is modified
   * w.r.t. the base revision, else set MODIFIED_P to zero.
   * FILENAME is a path to the file, not just a basename.
   */
  public boolean textModified(String filename) throws SubversionException;

  /**
   * @return 'true' if PATH's properties are modified
   * w.r.t. the base revision, else set MODIFIED_P to zero.
   */
  public boolean propsModified(String path) throws SubversionException;

  /**
   * Get the ENTRY structure for PATH
   *
   * Warning to callers:  remember to check whether entry->existence is
   * `deleted'.  If it is, you probably want to ignore it.
   *
   * @see Entry
   */
  public Entry entry(String path) throws SubversionException;

  /**
   * Parse the `entries' file for PATH and return a hash ENTRIES, whose
   * keys are entry names and values are (svn_wc_entry_t  *).
   *
   * Important note: only the entry structures representing files and
   * SVN_WC_ENTRY_THIS_DIR contain complete information.  The entry
   * structures representing subdirs have only the `kind' and `state'
   * fields filled in.  If you want info on a subdir, you must use this
   * routine to open its PATH and read the SVN_WC_ENTRY_THIS_DIR
   * structure, or call svn_wc_get_entry on its PATH.
   *
   * Warning to callers: remember to check whether each entry->existence
   * is `deleted'.  If it is, you probably want to ignore it.
   */
  public Hashtable entriesRead(String path) throws SubversionException;

  /**
   * Given a DIR_PATH under version control, decide if one of its
   * entries (ENTRY) is in state of conflict; return the answers in
   * TEXT_CONFLICTED_P and PROP_CONFLICTED_P.
   *
   * (If the entry mentions that a .rej or .prej exist, but they are
   * both removed, assume the conflict has been resolved by the user.)
   */
  public void conflicted(boolean[] textConflicted, boolean[] propConflicted,
    String path, Entry entry) throws SubversionException;

  /**
   * Fill *STATUS for PATH, with the exception of
   * the repos_rev field, which is normally filled in by the caller.
   *
   * @return Status of single file/directory
   */
  public Status status(String path) throws SubversionException;

  /**
   * Under PATH, fill STATUSHASH mapping paths to svn_wc_status_t
   * structures.  All fields in each struct will be filled in except for
   * repos_rev, which would presumably be filled in by the caller.
   *
   * PATH will usually be a directory, since for a regular file, you would
   * have used svn_wc_status().  However, it is no error if PATH is not
   * a directory; its status will simply be stored in STATUSHASH like
   * any other.
   *
   * Assuming PATH is a directory, then:
   *
   * If DESCEND is zero, statushash will contain paths for PATH and
   * its non-directory entries (subdirectories should be subjects of
   * separate status calls).
   *
   * If DESCEND is non-zero, statushash will contain statuses for PATH
   * and everything below it, including subdirectories.  In other
   * words, a full recursion.
   *
   * If any children of PATH are marked with existence `deleted', they
   * will NOT be returned in the hash.
   */
  public Hashtable statuses(String path, boolean descend);

  public void rename(String src, String dst) throws SubversionException;
  public void copy(String src, String dst) throws SubversionException;
  public void delete(String path) throws SubversionException;

  /**
   * Add an entry for DIR, and create an administrative directory for
   * it.  Does not check that DIR exists on disk; caller should take
   * care of that, if it cares.
   */
  public void addDirectory(String dir) throws SubversionException;

  /**
   * Add an entry for FILE.  Does not check that FILE exists on disk;
   * caller should take care of that, if it cares.
   */
  public void addFile(String file) throws SubversionException;

  /**
   * Recursively un-mark a tree (beginning at a directory or a file
   * PATH) for addition.
   */
  public void unadd(String path) throws SubversionException;

  /**
   * Un-mark a PATH for deletion.  If RECURSE is TRUE and PATH
   * represents a directory, un-mark the entire tree under PATH for
   * deletion.
   */
  public void undelete(String path, boolean recurse)
    throws SubversionException;


  /**
   * Remove entry NAME in PATH from revision control.  NAME must be
   * either a file or SVN_WC_ENTRY_THIS_DIR.
   *
   * If NAME is a file, all its info will be removed from PATH's
   * administrative directory.  If NAME is SVN_WC_ENTRY_THIS_DIR, then
   * PATH's entrire administrative area will be deleted, along with
   *  *all * the administrative areas anywhere in the tree below PATH.
   *
   * Normally, only adminstrative data is removed.  However, if
   * DESTROY_WF is set, then all working file(s) and dirs are deleted
   * from disk as well.  When called with DESTROY_WF, any locally
   * modified files will  *not * be deleted, and the special error
   * SVN_WC_LEFT_LOCAL_MOD might be returned.  (Callers only need to
   * check for this special return value if DESTROY_WF is set.)
   *
   * WARNING:  This routine is exported for careful, measured use by
   * libsvn_client.  Do  *not * call this routine unless you really
   * understand what the heck you're doing.
   */
  public void removeFromRevisionControl(String path, String name,
    boolean destroyWF) throws SubversionException;

  /**
   * Crawl a working copy tree depth-first, describing all local mods to
   * EDIT_FNS/EDIT_BATON.
   *
   * Start the crawl at PARENT_DIR, and only report changes found within
   * CONDENSED_TARGETS.  As the name implies, the targets must be
   * non-overlapping children of the parent dir, either files or
   * directories. (Use svn_path_condense_targets to create the target
   * list).  If the target list is NULL or contains no elements, then a
   * single crawl will be made from PARENT_DIR.
   */
  public void crawlLocalMods(String parentDir, String[] condensedTargets,
    TreeDeltaEditor editor, Object editBaton) throws SubversionException;

  /**
   * Do a depth-first crawl in a working copy, beginning at PATH.
   * Communicate the `state' of the working copy's revisions to
   * REPORTER/REPORT_BATON.  Obviously, if PATH is a file instead of a
   * directory, this depth-first crawl will be a short one.
   *
   * No locks are or logs are created, nor are any animals harmed in the
   * process.  No cleanup is necessary.
   *
   * After all revisions are reported, REPORTER->finish_report() is
   * called, which immediately causes the RA layer to update the working
   * copy.  Thus the return value may very well reflect the result of
   * the update!
   */
  public void crawlRevisions(String path, Reporter reporter)
    throws SubversionException;
}
