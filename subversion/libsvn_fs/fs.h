/* fs.h : interface to Subversion filesystem, private to libsvn_fs
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

#ifndef SVN_LIBSVN_FS_FS_H
#define SVN_LIBSVN_FS_FS_H

#include "db.h"			/* Berkeley DB interface */
#include "apr_pools.h"
#include "apr_hash.h"
#include "svn_fs.h"


/* The filesystem structure.  */

struct svn_fs_t {

  /* A pool managing this filesystem.  Freeing this pool must
     completely clean up the filesystem, including any database
     or system resources it holds.  */
  apr_pool_t *pool;

  /* The filename of the Berkeley DB environment, for use in error
     messages.  */
  char *env_path;

  /* A Berkeley DB environment for all the filesystem's databases.
     This establishes the scope of the filesystem's transactions.  */
  DB_ENV *env;

  /* A btree mapping revision numbers onto root directories and
     property lists.  See revisions.c for the details.  */
  DB *revisions;

  /* A btree mapping node id's onto node representations.  */
  DB *nodes;

  /* A btree mapping transaction id's onto a TRANSACTION skel, and
     the ID's of the nodes that are part of that transaction.  */
  DB *transactions;

  /* The head of a linked list of all currently open transactions, or
     zero if we have no currently open transactions.  */
  svn_fs_txn_t *all_txns;

  /* The head of a linked list of all currently open nodes, or zero if
     we have no currently open nodes.  */
  svn_fs_node_t *all_nodes;
  
  /* A callback function for printing warning messages, and a baton to
     pass through to it.  */
  svn_fs_warning_callback_t *warning;
  void *warning_baton;

  /* A kludge for handling errors noticed by APR pool cleanup functions.

     The APR pool cleanup functions can only return an apr_status_t
     value, not a full svn_error_t value.  This makes it difficult to
     propagate errors detected by fs_cleanup to someone who can handle
     them.

     If FS->cleanup_error is non-zero, it points to a location where
     fs_cleanup should store a pointer to an svn_error_t object, if it
     generates one.  Normally, it's zero, but if the cleanup is
     invoked by code prepared to deal with an svn_error_t object in
     some helpful way, it can create its own svn_error_t *, set it to
     zero, set cleanup_error to point to it, free the pool (thus
     invoking the cleanup), and then check its svn_error_t to see if
     anything went wrong.

     Of course, if multiple errors occur, this will only report one of
     them, but it's better than nothing.  In the case of a cascade,
     the first error message is probably the most helpful, so
     fs_cleanup won't overwrite a pointer to an existing svn_error_t
     if it finds one.  */
  svn_error_t **cleanup_error;
};



/* Transactions.  */


struct svn_fs_txn_t {

  /* The filesystem of which this transaction is a part.  */
  svn_fs_t *fs;

  /* The links in a doubly-linked list of all the transactions open
     in this filesystem.  */
  svn_fs_txn_t *next, *prev;

  /* A subpool for use by this transaction.  Destroying this pool
     closes the transaction neatly.  */
  apr_pool_t *pool;

  /* This transaction's ID, as a null-terminated C string.  If this ID
     no longer exists in the `transactions' table, then the
     transaction is dead; if we discover that this transaction is
     dead, we cache that fact by setting this to zero.  */
  char *id;

  /* The node revision ID of this transaction's root directory.  */
  svn_fs_id_t *root;

};



/* Nodes.  */


/* A parent directory of a node.  */
struct node_parent_t {

  /* The node revision ID of this parent.  */
  svn_fs_id_t *id;

  /* The name of the entry in this parent which we traversed to reach
     the node.  */
  char *entry;

  /* The directory's parent.  */
  struct node_parent_t *parent;

};
typedef struct node_parent_t node_parent_t;


struct svn_fs_node_t {

  /* A pool storing information about this node.  Destroying this pool
     closes the node neatly.  */
  apr_pool_t *pool;

  /* The filesystem of which this transaction is a part.  */
  svn_fs_t *fs;

  /* The links in a doubly-linked list of all the nodes open in this
     filesystem.  */
  svn_fs_node_t *next, *prev;

  /* The node revision ID for this node, allocated in POOL.  */
  svn_fs_id_t *id;

  /* The following fields provide information for bubbling up.  If we
     reach an immutable node via some transaction's root directory,
     and then we change that node, we need to create mutable clones of all
     the node's parents.  */

  /* If this node was reached via a transaction's root directory, this
     is the ID of that transaction, as a null-terminated string
     allocated in POOL.  */
  char *txn_id;

  /* This points to the list of parents through which we reached this
     node, up to the root of the transaction or version.  All the
     elements of this list are allocated in POOL.  */
  node_parent_t *parent;

};



/* Typed allocation macros.  These don't really belong here.  */

/* Allocate space for a value of type T from the pool P, and return a
   typed pointer.  */
#define NEW(P, T) ((T *) apr_palloc ((P), sizeof (T)))

/* Allocate space for an array of N values of type T from pool P, and
   return a typed pointer.  */
#define NEWARRAY(P, T, N) ((T *) apr_palloc ((P), sizeof (T) * (N)))

#endif /* SVN_LIBSVN_FS_FS_H */
