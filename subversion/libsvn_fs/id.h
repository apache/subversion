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

     node_revision_id ::= node_id "." copy_id "." txn_id

   The node_id is a unique identified to a given node, and is
   maintained across all revisions of that node.

   The copy_id is a key into the `copies' table, and groups node
   revisions that were created as the result of a filesystem copy
   operation.

   The txn_id is a key into the `transactions' table, and groups node
   revisions that were created as part of the same transaction.  
*/

struct svn_fs_id_t
{
  svn_fs__node_id_t node_id;
  svn_fs__copy_id_t copy_id;
  svn_fs__txn_id_t txn_id;
};



/* Return non-zero iff the node or node revision ID's A and B are equal.  */
int svn_fs__id_eq (const svn_fs_id_t *a, const svn_fs_id_t *b);


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
