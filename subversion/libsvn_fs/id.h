/* id.h : interface to node ID functions, private to libsvn_fs
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
 */

#ifndef SVN_LIBSVN_FS_ID_H
#define SVN_LIBSVN_FS_ID_H

#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Node Revision IDs.

   Within the database, we refer to nodes and node revisions using strings
   of numbers separated by periods that look a lot like RCS revision
   numbers.

     node_id ::= number | node_revision_id "." number
     node_revision_id ::= node_id "." number

   So: 
   - "100" is a node id.
   - "100.10" is a node revision id, referring to revision 10 of node 100.
   - "100.10.3" is a node id, referring to the third branch based on
     revision 10 of node 100.
   - "100.10.3.4" is a node revision id, referring to revision 4 of
     of the third branch from revision 10 of node 100.
   And so on.

   Node revision numbers start with 1.  Thus, N.1 is the first revision
   of node N.

   Node / branch numbers start with 1.  Thus, N.M.1 is the first
   branch off of N.M.

   A directory entry identifies the file or subdirectory it refers to
   using a node revision number --- not a node number.  This means that
   a change to a file far down in a directory hierarchy requires the
   parent directory of the changed node to be updated, to hold the new
   node revision ID.  Now, since that parent directory has changed, its
   parent needs to be updated.

   If a particular subtree was unaffected by a given commit, the node
   revision ID that appears in its parent will be unchanged.  When
   doing an update, we can notice this, and ignore that entire
   subtree.  This makes it efficient to find localized changes in
   large trees.

   Note that the number specifying a particular revision of a node is
   unrelated to the global filesystem revision when that node revision
   was created.  So 100.10 may have been created in filesystem revision
   1218; 100.10.3.2 may have been created any time after 100.10; it
   doesn't matter.

   Since revision numbers increase by one each time a delta is added,
   we can compute how many deltas separate two related node revisions
   simply by comparing their ID's.  For example, the distance between
   100.10.3.2 and 100.12 is the distance from 100.10.3.2 to their
   common ancestor, 100.10 (two deltas), plus the distance from 100.10
   to 100.12 (two deltas).

   However, this is kind of a kludge, since the number of deltas is
   not necessarily an accurate indicator of how different two files
   are --- a single delta could be a minor change, or a complete
   replacement.  Furthermore, the filesystem may decide arbitrary to
   store a given node revision as a delta or as full text --- perhaps
   depending on how recently the node was used --- so revision id
   distance isn't necessarily an accurate predictor of retrieval time.

   If you have insights about how this stuff could work better, let me
   know.  I've read some of Josh MacDonald's stuff on this; his
   discussion seems to be mostly about how to retrieve things quickly,
   which is important, but only part of the issue.  I'd like to find
   better ways to recognize renames, and find appropriate ancestors in
   a source tree for changed files.  */

struct svn_fs_id_t
{
  /* Within the code, we represent node and node revision ID's as arrays
     of integers, terminated by a -1 element.  This is the type of an
     element of a node ID.  */
  svn_revnum_t *digits;
};



/* Return non-zero iff the node or node revision ID's A and B are equal.  */
int svn_fs__id_eq (const svn_fs_id_t *a, const svn_fs_id_t *b);


/* Return the number of components in ID, not including the final -1.  */
int svn_fs__id_length (const svn_fs_id_t *id);


/* Convert ID to its predecessor.  If there is no possible predecessor
   id, set ID to the empty id (that is, an id of zero length, whose
   first digit is -1).  No allocation is performed, as ID can only
   stay the same length or shrink.

   Does not check that the predecessor id is actually present in the
   filesystem.

   Does not check that ID is a valid node revision ID.  If you pass in
   something else, the results are undefined.  */
void svn_fs__precede_id (svn_fs_id_t *id);


/* Like svn_fs__precede_id(), but return the predecessor allocated in
   POOL.  If no possible predecessor, still return an empty id.  */
svn_fs_id_t *svn_fs__id_predecessor (const svn_fs_id_t *id, apr_pool_t *pool);


/* Return non-zero iff node revision A is an ancestor of node revision B.  
   If A == B, then we consider A to be an ancestor of B.  */
int svn_fs__id_is_ancestor (const svn_fs_id_t *a, const svn_fs_id_t *b);


/* Return a copy of ID, allocated from POOL.  */
svn_fs_id_t *svn_fs__id_copy (const svn_fs_id_t *id, apr_pool_t *pool);


/* Return true iff PARENT is a direct parent of CHILD.  */
int svn_fs__id_is_parent (const svn_fs_id_t *parent,
                          const svn_fs_id_t *child);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_ID_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
