/* fs.h : interface to Subversion filesystem, private to libsvn_fs
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */

/* ==================================================================== */


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

  /* A btree mapping version numbers onto root directories and
     property lists.  See versions.c for the details.  */
  DB *versions;

  /* A btree mapping node id's onto node representations.  */
  DB *nodes;

  /* A btree mapping transaction id's onto a TRANSACTION skel, and
     the ID's of the nodes that are part of that transaction.  */
  DB *transactions;

  /* A cache of nodes we've read in, mapping svn_fs_id_t arrays onto
     pointers to nodes.  */
  apr_hash_t *node_cache;

  /* A table of all currently open transactions.  Each value is a
     pointer to an svn_fs_txn_t object, T; every key is T->id.  */
  apr_hash_t *open_txns;

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



/* Property lists.  */


/* The structure underlying the public svn_fs_proplist_t typedef.  */

struct svn_fs_proplist_t {

  /* A hash table, mapping property names (as byte strings) onto
     svn_string_t objects holding the values.  */
  apr_hash_t *hash;

  /* The pool of the underlying object.  */
  apr_pool_t *pool;

};



/* Transactions.  */

/* The private structure underlying the public svn_fs_txn_t typedef.  */

struct svn_fs_txn_t {

  /* This transaction's private pool, a subpool of fs->pool.

     Freeing this must completely clean up the transaction object,
     write back any buffered data, and release any database or system
     resources it holds.  (But don't confused the transaction object
     with the transaction it represents: freeing this does *not* abort
     the transaction.)  */
  apr_pool_t *pool;

  /* The filesystem to which this transaction belongs.  */
  svn_fs_t *fs;

  /* The ID of this transaction --- a null-terminated string.
     This is the key into the `transactions' table.  */
  char *id;

  /* The root directory for this transaction, or zero if the user
     hasn't called svn_fs_replace_root yet.  */
  svn_fs_id_t *root;
};


/* Nodes.  */


/* The different kinds of filesystem nodes.  */
typedef enum {
  kind_file,
  kind_dir
} kind_t;


/* The private structure underlying the public svn_fs_node_t typedef.

   All the more specific node structures --- files, directories,
   etc. --- include one of these as their first member.  ANSI
   guarantees the right behavior when you cast a pointer to a
   structure to a pointer to its first member, and back.  So this is
   effectively the superclass for files and directories.  */

struct svn_fs_node_t {
  
  /* The `open count' --- how many times this object has been opened,
     minus the number of times it has been closed.  If this count is
     zero, then we can free the object (although we may keep it around
     anyway as part of a cache).  */
  int open_count;

  /* The filesystem to which we belong.  */
  svn_fs_t *fs;

  /* This node's private pool, a subpool of fs->pool.
     Freeing this must completely clean up the node, and release any
     database or system resources it holds.  It must also remove the
     node from the filesystem's node cache.  */
  apr_pool_t *pool;

  /* The node version ID of this node.

     If this node is part of a transaction, the immediate ancestor of
     this node is the one we're merging against.  If this node has no
     immediate ancestor, then it's new in this transaction.  */
  svn_fs_id_t *id;

  /* What kind of node this is, more specifically.  */
  kind_t kind;

  /* The node's property list.  */
  svn_fs_proplist_t *proplist;

  /* On mutable nodes, this points to transaction the node belongs to.
     On immutable nodes, this is zero.  */
  svn_fs_txn_t *txn;
};



/* Files.  */

/* The private structure underlying the public svn_fs_file_t typedef.  */

struct svn_fs_file_t {
  
  /* The node structure carries information common to all nodes.  */
  svn_fs_node_t node;

  /* The contents of the file.  In the future, we should replace this
     with a reference to some database record we can read as needed.  */
  svn_string_t *contents;

};



/* Directories.  */

/* The private structure underlying the public svn_fs_dir_t typedef.  */

struct svn_fs_dir_t {

  /* The node structure carries information common to all nodes.  */
  svn_fs_node_t node;

  /* An array of pointers to the entries of this directory, terminated
     by a null pointer.  */
  svn_fs_dirent_t **entries;

  /* The number of directory entries here, and the number of elements
     allocated to the entries array.  */
  int num_entries;
  int entries_size;
};



/* Typed allocation macros.  These don't really belong here.  */

/* Allocate space for a value of type T from the pool P, and return a
   typed pointer.  */
#define NEW(P, T) ((T *) apr_palloc ((P), sizeof (T)))

/* Allocate space for an array of N values of type T from pool P, and
   return a typed pointer.  */
#define NEWARRAY(P, T, N) ((T *) apr_palloc ((P), sizeof (T) * (N)))

#endif /* SVN_LIBSVN_FS_FS_H */
