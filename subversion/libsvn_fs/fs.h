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

  /* A pool for allocations for this filesystem.  */
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

  /* A callback function for printing warning messages, and a baton to
     pass through to it.  */
  svn_fs_warning_callback_t *warning;
  void *warning_baton;

  /* A cache of nodes we've read in, mapping svn_fs_id_t arrays onto
     pointers to nodes.  */
  apr_hash_t *node_cache;

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



/* Nodes.  */


/* The private structure underlying the public svn_fs_node_t typedef.

   All the more specific node structures --- files, directories,
   etc. --- include one of these as their first member.  ANSI
   guarantees the right behavior when you cast a pointer to a
   structure to a pointer to its first member, and back.  So this is
   effectively the superclass for files and directories.

   NODE ALLOCATION:

   Nodes are not allocated the way you might assume.  Every node is
   allocated in its own subpool, which is a subpool of the
   filesystem's pool.  Note well: the node's pool is *never* a subpool
   of the pool you may have passed in to the `open' function.

   This is because the filesystem caches nodes.  Two separate `open'
   calls may return the same node object.  If those two `open' calls
   each specified a different pool, then that node object needs to
   live for as long as *either* of those two pools are around.  It's
   wrong to put the node in either pool (or a subpool of either pool),
   since the other one might get freed last.

   What actually happens is this: every node has an `open count',
   indicating how many times it has been opened, minus the number of
   times it has been closed.  When a node's open count reaches zero,
   we know there are no more references to the node, so we can decide
   arbitrarily to keep it around (in case someone opens it again) or
   throw it away (to save memory), without annoying any users.

   When someone passes their own pool to an `open' function, all we do
   is register a cleanup function in that pool that closes the node.
   That way, you can open the same node object in twenty different pools,


   Thus, when the pool is freed, the node's open count is decreased.
   If nobody else is using it, the open count will become zero, and we
   will free the node when our caching policy tells is to.  If other
   people are still using it, the open count will be greater than
   zero, and the node will stick around.  */



struct svn_fs_node_t {
  
  /* The `open count' --- how many times this object has been opened,
     minus the number of times it has been closed.  If this count is
     zero, then we can free the object (although we may keep it around
     anyway as part of a cache).  */
  int open_count;

  /* The filesystem to which we belong.  */
  svn_fs_t *fs;

  /* The pool in which we do our allocation.  Possibly the same as
     fs->pool.  */
  apr_pool_t *pool;

  /* The node version ID of this node.  */
  svn_fs_id_t *id;

  /* What kind of node this is, more specifically.  */
  enum {
    svn_fs_kind_file,
    svn_fs_kind_dir
  } kind;

  /* The node's property list.  */
  svn_fs_proplist_t *proplist;

};



/* Files.  */

/* The private structure underlying the public svn_fs_file_t typedef.  */

struct svn_fs_file_t {
  
  /* The node structure carries information common to all nodes.  */
  struct svn_fs_node_t node;

};



/* Typed allocation macros.  These don't really belong here.  */

/* Allocate space for a value of type T from the pool P, and return a
   typed pointer.  */
#define NEW(P, T) ((T *) apr_palloc ((P), sizeof (T)))

/* Allocate space for an array of N values of type T from pool P, and
   return a typed pointer.  */
#define NEWARRAY(P, T, N) ((T *) apr_palloc ((P), sizeof (T) * (N)))

#endif /* SVN_LIBSVN_FS_FS_H */
