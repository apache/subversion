package org.tigris.subversion.lib;

/**
 * public interface for libsvn_client
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
 * FIXME: all of the comment is hand-copied-and-pasted comment cut out
 * of the c language header "svn_delta.h". So a lot of the naming
 * convetions in the comment do still relate to the c function names.
 * There should be a preprocessing step which takes the c header file,
 * does naming and type conversions (using translation tables) and then
 * generates this file.
 *
 * Remark: the methods of this class correspond to the function described
 * in the subversion c api located in 'svn_client.h'
 */

import org.tigris.subversion.SubversionException;
import java.util.Vector;
import java.util.Date;

public interface Client {
  /**
   * Perform a checkout from URL, providing pre- and post-checkout hook
   * editors and batons (BEFORE_EDITOR, BEFORE_EDIT_BATON /
   * AFTER_EDITOR, AFTER_EDIT_BATON).  These editors are purely optional
   * and exist only for extensibility;  pass four NULLs here if you
   * don't need them.
   *
   * PATH will be the root directory of your checked out working copy.
   *
   * If XML_SRC is NULL, then the checkout will come from the repository
   * and subdir specified by URL.  An invalid REVISION will cause the
   * "latest" tree to be fetched, while a valid REVISION will fetch a
   * specific tree.  Alternatively, a time TM can be used to implicitly
   * select a revision.  TM cannot be used at the same time as REVISION.
   *
   * If XML_SRC is non-NULL, it is an xml file to check out from; in
   * this case, the working copy will record the URL as artificial
   * ancestry information.  An invalid REVISION implies that the
   * revision *must* be present in the <delta-pkg> tag, while a valid
   * REVISION will be simply be stored in the wc. (Note:  a <delta-pkg>
   * revision will *always* override the one passed in.)
   */
  public void checkout(TreeDeltaEditor beforeEditor,
    TreeDeltaEditor afterEditor, String url, String path,
    Revision revision, Date time, String xml_src) throws SubversionException;

  /**
   * Perform an update of PATH (part of a working copy), providing pre-
   * and post-checkout hook editors and batons (BEFORE_EDITOR,
   * BEFORE_EDIT_BATON / AFTER_EDITOR, AFTER_EDIT_BATON).  These editors
   * are purely optional and exist only for extensibility; pass four
   * NULLs here if you don't need them.
   *
   * If XML_SRC is NULL, then the update will come from the repository
   * that PATH was originally checked-out from.  An invalid REVISION
   * will cause the PATH to be updated to the "latest" revision, while a
   * valid REVISION will update to a specific tree.  Alternatively, a
   * time TM can be used to implicitly select a revision.  TM cannot be
   * used at the same time as REVISION.
   *
   * If XML_SRC is non-NULL, it is an xml file to update from.  An
   * invalid REVISION implies that the revision  *must * be present in the
   * <delta-pkg> tag, while a valid REVISION will be simply be stored in
   * the wc. (Note: a <delta-pkg> revision will  *always * override the
   * one passed in.)
   */
  public void update(TreeDeltaEditor beforeEditor,
    TreeDeltaEditor afterEditor, String path, String xml_src,
    String revision, Date time) throws SubversionException;

  public void add(String path, boolean recursive) throws SubversionException;

  public void delete(String path, boolean force) throws SubversionException;

  /**
   * Import a tree, using optional pre- and post-commit hook editors
   * (BEFORE_EDITOR, BEFORE_EDIT_BATON / AFTER_EDITOR,
   * AFTER_EDIT_BATON).  These editors are purely optional and exist
   * only for extensibility; pass four NULLs here if you don't need
   * them.
   *
   * Store LOG_MSG as the log for the commit.
   *
   * PATH is the path to local tree being imported.  PATH can be a file
   * or directory.
   *
   * URL is the repository directory where the imported data is placed.
   *
   * NEW_ENTRY is the new entry created in the repository directory
   * identified by URL.
   *
   * If PATH is a file, that file is imported as NEW_ENTRY.  If PATH is
   * a directory, the contents of that directory are imported, under a
   * new directory the NEW_ENTRY in the repository.  Note and the
   * directory itself is not imported; that is, the basename of PATH is
   * not part of the import.
   *
   * If PATH is a directory and NEW_ENTRY is null, then the contents of
   * PATH are imported directly into the repository directory identified
   * by URL.  NEW_ENTRY may not be the empty string.
   *
   * If NEW_ENTRY already exists in the youngest revision, return error.
   *
   * If XML_DST is non-NULL, it is a file in which to store the xml
   * result of the commit, and REVISION is used as the revision.
   *
   * Remark: the original subversion api function name is 'import', but
   * this is a reserved word in java
   */
  public void performImport(TreeDeltaEditor beforeEditor,
      TreeDeltaEditor afterEditor, String path, String url,
      String new_entry, String log_msg,	String xml_dst,
      String Revision) throws SubversionException;

  /**
   * Perform an commit, providing pre- and post-commit hook editors and
   * batons (BEFORE_EDITOR, BEFORE_EDIT_BATON / AFTER_EDITOR,
   * AFTER_EDIT_BATON).  These editors are purely optional and exist
   * only for extensibility; pass four NULLs here if you don't need
   * them.
   *
   * Store LOG_MSG as the log for the commit.
   *
   * TARGETS is an array of svn_stringbuf_t  * paths to commit.  They need
   * not be canonicalized nor condensed; this function will take care of
   * that.
   *
   * If XML_DST is NULL, then the commit will write to a repository, and
   * the REVISION argument is ignored.
   *
   * If XML_DST is non-NULL, it is a file path to commit to.  In this
   * case, if REVISION is valid, the working copy's revision numbers
   * will be updated appropriately.  If REVISION is invalid, the working
   * copy remains unchanged.
   */
  public void commit(TreeDeltaEditor beforeEditor,
    TreeDeltaEditor afterEditor, String targets[], String log_msg,
    String xml_dst, String revision) throws SubversionException;

  /**
   * Given PATH to a working copy directory or file, allocate and return
   * a STATUSHASH structure containing the stati of all entries.  If
   * DESCEND is non-zero, recurse fully, else do only immediate
   * children.  (See svn_wc.h:svn_wc_statuses() for more verbiage on
   * this).
   */
  public Vector status(String path, boolean descend,
		       boolean get_all, boolean update )
      throws SubversionException;

  /**
   * Given a PATH to a working copy file, return a path to a temporary
   * copy of the PRISTINE version of the file.  The client can then
   * compare this to the working copy of the file and execute any kind
   * of diff it wishes.
   */
  public String fileDiff(String path) throws SubversionException;

  /**
   * Recursively cleanup a working copy directory DIR, finishing any
   * incomplete operations, removing lockfiles, etc.
   */
  public void cleanup(String dir) throws SubversionException;

  /**
   * Set Username
   */
  public void setUsername(String username);

  /**
   * Get Username
   */
   public String getUsername();

   /**
    * Set Password 
    */
   public void setPassword(String password);

   /**
    * Get Password
    */
    public String getPassword();
}
