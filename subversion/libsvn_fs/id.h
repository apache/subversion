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

              node_id ::= number ;
              copy_id ::= number ;
               txn_id ::= number ;
     node_revision_id ::= node_id "." copy_id "." txn_id ;

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
   large trees.  */


struct svn_fs_id_t
{
  /* node id, unique to a node across all revisions of that node. */
  const char *node_id;

  /* copy id, a key into the `copies' table. */
  const char *copy_id;

  /* txn id, a key into the `transactions' table. */
  const char *txn_id;
};



/*** Accessor functions. ***/

/* Create a ID based on NODE_ID, COPY_ID, and TXN_ID, and allocated in
   POOL.  */
svn_fs_id_t *svn_fs__create_id (const char *node_id,
                                const char *copy_id,
                                const char *txn_id,
                                apr_pool_t *pool);

/* Access the "node id" portion of ID. */
const char *svn_fs__id_node_id (const svn_fs_id_t *id);

/* Access the "copy id" portion of ID. */
const char *svn_fs__id_copy_id (const svn_fs_id_t *id);

/* Access the "txn id" portion of ID. */
const char *svn_fs__id_txn_id (const svn_fs_id_t *id);

/* Return non-zero iff the node or node revision ID's A and B are equal.  */
int svn_fs__id_eq (const svn_fs_id_t *a, 
                   const svn_fs_id_t *b);

/* Return a copy of ID, allocated from POOL.  */
svn_fs_id_t *svn_fs__id_copy (const svn_fs_id_t *id, 
                              apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_ID_H */
