package org.tigris.subversion.lib;

/**
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * Remark: this class corresponds to the subversion c api type
 * 'svn_delta_edit_fns_t'
 */

import org.tigris.subversion.SubversionException;

/**
* ====================================================================
* In Subversion, we've got various producers and consumers of tree
 deltas.

 In processing a `commit' command:
 - The client examines its working copy data, and produces a tree
   delta describing the changes to be committed.
 - The client networking library consumes that delta, and sends them
   across the wire as an equivalent series of WebDAV requests.
 - The Apache WebDAV module receives those requests and produces a
   tree delta --- hopefully equivalent to the one the client
   produced above.
 - The Subversion server module consumes that delta and commits an
   appropriate transaction to the filesystem.

 In processing an `update' command, the process is reversed:
 - The Subversion server module talks to the filesystem and produces
   a tree delta describing the changes necessary to bring the
   client's working copy up to date.
 - The Apache WebDAV module consumes this delta, and assembles a
   WebDAV reply representing the appropriate changes.
 - The client networking library receives that WebDAV reply, and
   produces a tree delta --- hopefully equivalent to the one the
   Subversion server produced above.
 - The working copy library consumes that delta, and makes the
   appropriate changes to the working copy.

 The simplest approach would be to represent tree deltas using the
 obvious data structure.  To do an update, the server would
 construct a delta structure, and the working copy library would
 apply that structure to the working copy; WebDAV's job would simply
 be to get the structure across the net intact.

 However, we expect that these deltas will occasionally be too large
 to fit in a typical workstation's swap area.  For example, in
 checking out a 200Mb source tree, the entire source tree is
 represented by a single tree delta.  So it's important to handle
 deltas that are too large to fit in swap all at once.

 So instead of representing the tree delta explicitly, we define a
 standard way for a consumer to process each piece of a tree delta
 as soon as the producer creates it.  The `svn_delta_edit_fns_t'
 structure is a set of callback functions to be defined by a delta
 consumer, and invoked by a delta producer.  Each invocation of a
 callback function describes a piece of the delta --- a file's
 contents changing, something being renamed, etc.


 Here's how to use these functions to express a tree delta.

   The delta consumer implements the callback functions described in
   this structure, and the delta producer invokes them.  So the
   caller (producer) is pushing tree delta data at the callee
   (consumer).

   At the start of traversal, the consumer provides EDIT_BATON, a
   baton global to the entire delta edit.  In the case of
   `svn_xml_parse', this would be the EDIT_BATON argument; other
   producers will work differently.  If there is a target revision
   that needs to be set for this operation, the producer should
   called the 'set_target_revision' function at this point.  Next,
   the producer should pass this EDIT_BATON to the `replace_root'
   function, to get a baton representing root of the tree being
   edited.

   Most of the callbacks work in the obvious way:

       delete_entry
       add_file           add_directory
       replace_file       replace_directory

   Each of these takes a directory baton, indicating the directory
   in which the change takes place, and a NAME argument, giving the
   name of the file, subdirectory, or directory entry to change.
   (NAME is always a single path component, never a full directory
   path.)

   Since every call requires a parent directory baton, including
   add_directory and replace_directory, where do we ever get our
   initial directory baton, to get things started?  The `replace_root'
   function returns a baton for the top directory of the change.  In
   general, the producer needs to invoke the editor's `replace_root'
   function before it can get anything of interest done.

   While `replace_root' provides a directory baton for the root of
   the tree being changed, the `add_directory' and
   `replace_directory' callbacks provide batons for other
   directories.  Like the callbacks above, they take a PARENT_BATON
   and a single path component NAME, and then return a new baton for
   the subdirectory being created / modified --- CHILD_BATON.  The
   producer can then use CHILD_BATON to make further changes in that
   subdirectory.

   So, if we already have subdirectories named `foo' and `foo/bar',
   then the producer can create a new file named `foo/bar/baz.c' by
   calling:
      replace_root () --- yielding a baton ROOT for the top directory
      replace_directory (ROOT, "foo") --- yielding a baton F for `foo'
      replace_directory (F, "bar") --- yielding a baton B for `foo/bar'
      add_file (B, "baz.c")

   When the producer is finished making changes to a directory, it
   should call `close_directory'.  This lets the consumer do any
   necessary cleanup, and free the baton's storage.

   The `add_file' and `replace_file' callbacks each return a baton
   for the file being created or changed.  This baton can then be
   passed to `apply_textdelta' to change the file's contents, or
   `change_file_prop' to change the file's properties.  When the
   producer is finished making changes to a file, it should call
   `close_file', to let the consumer clean up and free the baton.

   The `add_file', `add_directory', `replace_file', and
   `replace_directory' functions all take arguments ANCESTOR_PATH
   and ANCESTOR_REVISION.  If ANCESTOR_PATH is non-zero, then
   ANCESTOR_PATH and ANCESTOR_REVISION indicate the ancestor of the
   resulting object.

   There are six restrictions on the order in which the producer
   may use the batons:

   1. The producer may call `replace_directory', `add_directory',
      `replace_file', `add_file', or `delete_entry' at most once on
      any given directory entry.

   2. The producer may not close a directory baton until it has
      closed all batons for its subdirectories.

   3. When a producer calls `replace_directory' or `add_directory',
      it must specify the most recently opened of the currently open
      directory batons.  Put another way, the producer cannot have
      two sibling directory batons open at the same time.

   4. A producer must call `change_dir_prop' on a directory either
      before opening any of the directory's subdirs or after closing
      them, but not in the middle.

   5. When the producer calls `replace_file' or `add_file', either:

      (a) The producer must follow with the changes to the file
      (`change_file_prop' and/or `apply_textdelta', as applicable)
      followed by a `close_file' call, before issuing any other file
      or directory calls, or

      (b) The producer must follow with a `change_file_prop' call if
      it is applicable, before issuing any other file or directory
      calls; later, after all directory batons including the root
      have been closed, the producer must issue `apply_textdelta'
      and `close_file' calls.

   6. When the producer calls `apply_textdelta', it must make all of
      the window handler calls (including the NULL window at the
      end) before issuing any other edit_fns calls.

   So, the producer needs to use directory and file batons as if it
   is doing a single depth-first traversal of the tree, with the
   exception that the producer may keep file batons open in order to
   make apply_textdelta calls at the end.

   These restrictions make it easier to write a consumer that
   generates an XML-style tree delta.  An XML tree delta mentions
   each directory once, and includes all the changes to that
   directory within the <directory> element.  However, it does allow
   text deltas to appear at the end.
*/
public interface TreeDeltaEditor {
    /**
      * Set the target revision for this edit to TARGET_REVISION.  This
      * call, if used, should precede all other editor calls.
      */
    public void setTargetRevision(Object editBaton, Revision targetRevision)
      throws SubversionException;

    /**
     * Set *ROOT_BATON to a baton for the top directory of the change.
     * (This is the top of the subtree being changed, not necessarily
     * the root of the filesystem.)  Like any other directory baton, the
     * producer should call `close_directory' on ROOT_BATON when they're
     * done.  And like other replace_* calls, the BASE_REVISION here is
     * the current revision of the directory (before getting bumped up
     * to the new target revision set with set_target_revision).
     *
     * @return root Baton
     */
    public Object replaceRoot(Object baton, Revision baseRevision)
      throws SubversionException;

    public void deleteEntry(String name, Object parentBaton)
      throws SubversionException;

    /**
     * We are going to add a new subdirectory named NAME.  We will use
     * the value this callback stores in *CHILD_BATON as the
     * PARENT_BATON for further changes in the new subdirectory.  If
     * COPYFROM_PATH is non-NULL, this add has history (which is the
     * copy case), and its most recent path-y alias was COPYFROM_PATH,
     * which was at version COPYFROM_REVISION.
     *
     * @return child Baton
     */
    public Object addDirectory(String name, Object parentBaton,
      String copyFromPath, Revision copyFromRevision)
      throws SubversionException;

    /**
     * We are going to change the directory entry named NAME to a
     * subdirectory.  The callback must store a value in *CHILD_BATON
     * that will be used as the PARENT_BATON for subsequent changes in
     * this subdirectory.  BASE_REVISION is the current revision of the
     * subdirectory.
     *
     * @return child Baton
     */
    public Object replaceDirectory(String name, Object parentBaton,
      Revision baseRevision) throws SubversionException;

    /**
     * Change the value of a directory's property.
     * - DIR_BATON specifies the directory whose property should change.
     * - NAME is the name of the property to change.
     * - VALUE is the new value of the property, or zero if the property
     *   should be removed altogether.
     */
    public void changeDirProp(Object dirBaton, String name, String value)
      throws SubversionException;

    /**
     * We are done processing a subdirectory, whose baton is DIR_BATON
     * (set by add_directory or replace_directory).  We won't be using
     * the baton any more, so whatever resources it refers to may now be
     * freed.
     */
    public void closeDirectory(Object dirBaton) throws SubversionException;

    /**
     * We are going to add a new file named NAME.  The callback can
     * store a baton for this new file in **FILE_BATON; whatever value
     * it stores there will be passed through to apply_textdelta and/or
     * apply_propdelta.  If COPYFROM_PATH is non-NULL, this add has
     * history (which is the copy case), and its most recent path-y
     * alias was COPYFROM_PATH, which was at version
     * COPYFROM_REVISION.
     */
    public void addFile(String name, Object parentBaton, String copyPath,
      Revision copyRevision, Object fileBaton) throws SubversionException;

    /**
     * We are going to change the directory entry named NAME to a file.
     * The callback can store a baton for this new file in **FILE_BATON;
     * whatever value it stores there will be passed through to
     * apply_textdelta and/or apply_propdelta.  This file has a current
     * revision of BASE_REVISION.
     *
     * @return file baton
     */
    public Object replaceFile(String name, Object parentBaton,
      Revision baseRevision) throws SubversionException;

    /**
     * Apply a text delta, yielding the new revision of a file.
     *
     * FILE_BATON indicates the file we're creating or updating, and the
     * ancestor file on which it is based; it is the baton set by some
     * prior `add_file' or `replace_file' callback.
     *
     * The callback should set *HANDLER to a text delta window
     * handler; we will then call *HANDLER on successive text
     * delta windows as we receive them.  The callback should set
     * *HANDLER_BATON to the value we should pass as the BATON
     * argument to *HANDLER.
     *
     * return handler haton
     */
    public Object applyTextdelta(Object fileBaton,
      TextdeltaHandler handler) throws SubversionException;

    /**
     * Change the value of a file's property.
     * - FILE_BATON specifies the file whose property should change.
     * - NAME is the name of the property to change.
     * - VALUE is the new value of the property, or zero if the property
     *   should be removed altogether.
     */
    public void changeFileProp(Object fileBaton, String name, String value)
      throws SubversionException;

    /**
     * We are done processing a file, whose baton is FILE_BATON (set by
     * `add_file' or `replace_file').  We won't be using the baton any
     * more, so whatever resources it refers to may now be freed.
     */
    public void closeFile(Object fileBaton) throws SubversionException;

    /**
     * All delta processing is done.  Call this, with the EDIT_BATON for
     * the entire edit.
     */
    public void closeEdit(Object editBaton) throws SubversionException;

    /**
     * The editor-driver has decided to bail out.  Allow the editor to
     * gracefully clean up things if it needs to.
     */
    public void abortEdit(Object editBaton) throws SubversionException;
}

