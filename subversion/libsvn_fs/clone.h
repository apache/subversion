/* clone.h : interface to cloning and clone-tracking functions
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_FS_CLONE_H
#define SVN_LIBSVN_FS_CLONE_H

#include "apr_pools.h"
#include "svn_fs.h"
#include "fs.h"


/* The main difference between a simple node revision ID and an
   svn_fs_node_t object is that the latter can actually track cloning
   operations.  If you've got an svn_fs_node_t object referring to
   some node in a transaction which happens to still be shared with
   the transaction's base revision's tree, and some other process does
   some operation that clones a node, your svn_fs_node_t contains
   enough information to recognize that this has happened, and find
   the clone.  So an svn_fs_node_t sticks to the node you want, even
   when it gets cloned.

   For that reason, the maintenance of an svn_fs_node_t object falls
   mostly on the cloning module.  So we declare that structure here.  */


/* A structure describing the path from an uncloned node in a
   transaction's tree to the root of the transaction's base revision.  */
struct path {
  svn_fs_id_t *id;		/* The ID of this node.  */
  struct parent *parent;	/* The parent directory of this node,
				   or zero if this is the root directory.  */
  char *entry;			/* The name this node has in that parent.  */
};

struct svn_fs_node_t {

  /* The filesystem to which this node belongs.  */
  svn_fs_t *fs;

  /* The pool in which this node is allocated.  Destroying this pool
     frees all memory used by this node, and any other resources the
     node owns.  */
  apr_pool_t *pool;

  /* If this node was reached from the root of a transaction, this is
     the transaction ID, allocated in POOL.  Otherwise, this is zero.  */
  char *txn_id;

  /* If this node was reached from the root of a transaction, this is
     the ID of the transaction's base revision's root directory.
     Otherwise, this is zero.  */
  svn_fs_id_t *txn_base_root;

  /* If this node was reached from the root of a transaction, but we
     don't know of any clone for it yet, this is the path from this
     node to the root of the transaction's base revision.  Otherwise,
     this is zero.  */
  struct path *path;

  /* If this node was reached from the root of a transaction, and it
     has been cloned, then this is the node revision ID of the clone.  */
  svn_fs_id_t *clone;

  /* If this node was reached from the root of a filesystem revision,
     this is the revision number.  Otherwise, this is -1.  */
  svn_revnum_t rev;

};


/* Clone the node NODE, doing any necessary bubbling-up, as part of
   the Berkeley DB transaction DB_TXN.  If NODE has already been
   cloned, this function has no effect.

   Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs__clone_node (svn_fs_node_t *node,
				 apr_pool_t *pool);


#endif /* SVN_LIBSVN_FS_CLONE_H */
