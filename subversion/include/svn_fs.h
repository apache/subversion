/* svn_fs.h :  interface to the Subversion filesystem
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


#ifndef SVN_FS_H
#define SVN_FS_H

#include <apr_pools.h>
#include <apr_hash.h>
#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"


/* A lot of the functions in this interface take a POOL argument; if
   it is zero, they try to choose a good default pool.  If you find
   that a different default pool would be more helpful, let me know.

   Perhaps having default pools is not a smart idea.  Will it lead to
   lifetime bugs?  */



/* Opening and creating filesystems.  */


/* An object representing a Subversion filesystem.  */
typedef struct svn_fs svn_fs_t;


/* Create a new filesystem object in POOL.  It doesn't refer to any
   actual database yet; you need to invoke svn_fs_open or
   svn_fs_create on it for that to happen.  */
extern svn_fs_t *svn_fs_new (apr_pool_t *pool);


/* Create a new, empty Subversion filesystem, stored in a Unix file or
   directory named PATH, and make FS refer to it.  FS provides the
   memory pool, warning function, etc.  */
extern svn_error_t *svn_fs_newfs (svn_fs_t *fs, const char *path);


/* Make FS refer to the Subversion filesystem stored at the Unix file
   or directory named PATH.  PATH must refer to a file or directory
   created by `svn_fs_newfs'.

   Only one thread may operate on any given filesystem object at once.
   Two threads may access the same filesystem simultaneously only if
   they open separate filesystem objects.  */
extern svn_error_t *svn_fs_open (svn_fs_t *fs, const char *path);


/* Free the filesystem object FS.  This frees memory, closes files,
   frees database library structures, etc.  */
extern svn_error_t *svn_fs_close (svn_fs_t *fs);


/* The type of a warning callback function.  BATON is the value specified
   in the call to `svn_fs_set_warning_func'; the filesystem passes it through
   to the callback.  FMT is a printf-style format string, which tells us
   how to interpret any successive arguments.  */
typedef void (svn_fs_warning_callback_t) (void *baton, const char *fmt, ...);


/* Provide a callback function, WARNING, that FS should use to report
   warning messages.  To print a warning message, the filesystem will
   call WARNING, passing it BATON, a printf-style format string, and
   any further arguments as appropriate for the format string.

   If it's acceptable to print messages on stderr, then the function
   `svn_handle_warning', declared in "svn_error.h", would be a
   suitable warning function.

   By default, this is set to a function that will crash the process.
   Dumping to stderr or /dev/tty is not acceptable default behavior
   for server processes, since those may both be equivalent to
   /dev/null.  */
extern void svn_fs_set_warning_func (svn_fs_t *fs,
				     svn_fs_warning_callback_t *warning,
				     void *warning_baton);


/* Create a new sub-pool of the pool used by the filesystem FS.  This
   pool will be freed whenever FS is closed, but could also be freed
   earlier, if you like.  */
extern apr_pool_t *svn_fs_subpool (svn_fs_t *fs);


/* Special requirements of Berkeley DB-based filesystems.  */


/* Perform any necessary non-catastrophic recovery on a Berkeley
   DB-based Subversion filesystem, stored at PATH.  Do any necessary
   allocation within POOL.

   After an unexpected server exit, due to a server crash or a system
   crash, a Subversion filesystem based on Berkeley DB needs to run
   recovery procedures to bring the database back into a consistent
   state and release the locks held by the deceased process.  The
   recovery procedures require exclusive access to the database ---
   while they execute, no other process or thread may access the
   database.

   In a server with multiple worker processes, like Apache, if a
   worker process accessing the filesystem dies, you must stop the
   other worker processes, and run recovery.  Then, the other worker
   processes can re-open the database and resume work.

   If the server exited cleanly, there is no need to run recovery, but
   there is no harm in it, either.  So it's a fine idea to run
   recovery when the server process starts, before it begins handling
   any requests.  */

extern svn_error_t *svn_fs_berkeleydb_recover (const char *path,
					       apr_pool_t *pool);



/* Reading and traversing directories.  */


/* An object representing a directory in a Subversion filesystem.  */
typedef struct svn_fs_dir_t svn_fs_dir_t;


/* Set *DIR to a pointer to a directory object representing the root
   directory of version V of filesystem FS.

   If POOL is zero, allocate DIR in FS's pool; it will be freed when
   the filesystem is closed.  If POOL is non-zero, it must be a pool
   returned by `svn_fs_subpool'; do allocation there.  */
extern svn_error_t *svn_fs_open_root (svn_fs_dir_t **dir,
				      svn_fs_t *fs,
				      svn_vernum_t v,
				      apr_pool_t *pool);


/* Set *CHILD_DIR to a pointer to the subdirectory of *PARENT_DIR
   named NAME.  NAME must be a single path component.

   If POOL is zero, allocate CHILD_DIR in the filesystem's pool; it
   will be freed when the filesystem is closed.  If POOL is non-zero,
   it must be a pool returned by `svn_fs_subpool'; do allocation
   there.  */
extern svn_error_t *svn_fs_open_subdir (svn_fs_dir_t **child_dir,
					svn_fs_dir_t *parent_dir,
					svn_string_t *name,
					apr_pool_t *pool);


/* Create a new subpool of the pool used by the directory DIR.  This
   pool will be freed whenever DIR is closed, but could also be freed
   earlier, if you like.  */
extern apr_pool_t *svn_dir_subpool (svn_fs_dir_t *dir);


/* Free the directory object DIR.  */
extern void svn_fs_close_dir (svn_fs_dir_t *dir);


/* Return a list of DIR's contents.  Set *ENTRIES to point to a
   null-terminated array of pointers to `svn_string_t' structures.

   If POOL is zero, allocate *ENTRIES in DIR's pool; it will be freed
   when the directory object is freed.  If POOL is non-zero, do
   allocation there.  */
extern svn_error_t *svn_fs_dir_entries (svn_string_t ***entries,
					svn_fs_dir_t *dir,
					apr_pool_t *pool);


/* An enum for the different kinds of objects one might find in a
   Subversion filesystem.

   You know, it would be really nice if files and directories were
   just subclasses of a common `node' class.  Then we could use
   `instanceof' to decide whether something was a file, directory,
   etc.  I can't wait until native code Java compilers are installed
   by default on every system we care about.  */
typedef enum svn_fs_node_kind_t {
  svn_fs_node_nothing = 0,
  svn_fs_node_file,
  svn_fs_node_dir
} svn_fs_node_kind_t;


/* Set *KIND to the kind of the entry in DIR named NAME.  NAME must be
   a single path component.  */
extern svn_error_t *svn_fs_type (svn_fs_node_kind_t *kind,
				 svn_fs_dir_t *dir,
				 svn_string_t *name);



/* Accessing files.  */

/* An object representing a file in a Subversion filesystem.  */
typedef struct svn_fs_file_t svn_fs_file_t;


/* Set *FILE to a pointer to a file object representing the file in
   DIR named NAME.  NAME must be a single path component.

   If POOL is zero, allocate FILE in the filesystem's pool; it will be
   freed when the filesystem is closed.  If POOL is non-zero, it must
   be a pool returned by `svn_fs_subpool'; do allocation there.  */
extern svn_error_t *svn_fs_open_file (svn_fs_file_t **file,
				      svn_fs_dir_t *dir,
				      svn_string_t *name);


/* Create a new subpool of the pool used by FILE.  This pool will be
   freed when FILE is closed, but could also be freed earlier, if
   you like.  */
extern apr_pool_t *svn_file_subpool (svn_fs_file_t *file);


/* Free the file object FILE.  */
extern void svn_fs_close_file (svn_fs_file_t *file);


/* Set *LENGTH to the length of FILE, in bytes.  */
extern svn_error_t *svn_fs_file_length (apr_off_t *length,
					svn_fs_file_t *file);


/* Set *CONTENTS to a `read'-like function which will return the
   contents of FILE; see the description of svn_read_fn_t in
   `svn_delta.h'.  Set *CONTENTS_BATON to a baton to pass to CONTENTS.  */
extern svn_error_t *svn_fs_file_contents (svn_read_fn_t **contents,
					  void **contents_baton,
					  svn_fs_file_t *file);



/* Accessing properties and property lists.  */

/* There are three things that have property lists in Subversion:
   - directories
   - files
   - directory entries

   There are three queries one can make of a property list:
   - get an individual property's value
   - get a list of all property names
   - get a list of all property names and values

   In order to avoid having 3 x 3 = 9 separate functions, we follow a
   two-step process:

   - Given a directory, file, or directory entry, you can get a
     `property list' object.
   - Given a property list object, you can perform the various queries
     mentioned above.

   The call to get an object's property list is guaranteed to succeed.
   And the property list object is allocated in the underlying
   object's pool, so you needn't free it.  So, while a two-step
   process is annoying, it's less so than you'd expect because you
   needn't check for the failure of the first call, or capture its
   value to be freed later.  */


/* The type representing a property list of a file, directory, or
   directory entry.  */
typedef struct svn_fs_proplist_t svn_fs_proplist_t;


/* Return the property list of OBJECT.  The property list object will
   live exactly as long as OBJECT does; multiple calls to these functions
   on the same object may return the same property list object.  */
extern svn_fs_proplist_t *svn_fs_dir_proplist (svn_fs_dir_t *object);
extern svn_fs_proplist_t *svn_fs_file_proplist (svn_fs_file_t *object);


/* Set *PROPLIST to a pointer to the property list of the entry of
   directory DIR named NAME.  NAME must be a single path component.
   Unlike the other property list retrieval functions, this one may
   return an error.  */
extern svn_error_t *svn_fs_dirent_proplist (svn_fs_proplist_t **proplist,
					    svn_fs_dir_t *dir,
					    svn_string_t *name);


/* Set *VALUE to the value of the property in PROPLIST named NAME.
   Set *VALUE to zero if there is no such property.  

   If POOL is zero, allocate *VALUE in pool of the object PROPLIST
   came from; it will be freed when the filesystem is closed.  If POOL
   is non-zero, do allocation there.  */
extern svn_error_t *svn_fs_proplist_get (svn_string_t **value,
					 svn_fs_proplist_t *proplist,
					 svn_string_t *name,
					 apr_pool_t *pool);


/* Set *NAMES to point to a null-terminated array of pointers to
   strings giving the names of the properties in PROPLIST.

   If POOL is zero, allocate *NAMES in the pool of the object PROPLIST
   came from; it will be freed when the filesystem is closed.  If POOL
   is non-zero, do allocation there.  */
extern svn_error_t *svn_fs_proplist_names (svn_string_t ***names,
					   svn_fs_proplist_t *proplist,
					   apr_pool_t *pool);


/* Set *TABLE to an APR hash table containing the property list of
   PROPLIST.  Each key in *TABLE is the text of a property name; each
   value is a pointer to an `svn_string_t' object.

   If POOL is zero, allocate *TABLE in the pool of the object PROPLIST
   came from; it will be freed when the filesystem is closed.  If POOL
   is non-zero, do allocation there.  */
extern svn_error_t *svn_fs_proplist_table (apr_hash_t *table,
					   svn_fs_proplist_t *proplist,
					   apr_pool_t *pool);



/* Committing changes to Subversion filesystems.  */


/* [[Comments on the above would be so welcome.]]  

   All changes to a Subversion filesystem take place as part of a
   transaction.  The transaction either succeeds completely, and
   creates a new version of the filesystem, or fails completely,
   leaving the filesystem unchanged.

   File and directory objects, as created by the functions above,
   refer to specific versions of a node.  The objects they refer to
   never change --- the history recorded in the repository must not be
   rewritten.  Instead, when we modify the filesystem, we create new
   objects as we go, which refer to new nodes, or new versions of
   existing nodes.

   While you make changes as part of the transaction, the new nodes
   and node versions are not yet part of the filesystem.  When you
   commit the transaction, they become visible as a new version.  If
   you abort the transaction, they are discarded.

   The directory and file objects created by functions above are
   immutable; they refer to extant filesystem versions, and you cannot
   add, delete, or change files or directories in those objects.  The
   functions below, however, return mutable objects, which can be
   changed.

   This distinction between immutable and mutable objects is
   completely independent of whether you have *permission* to change a
   given directory or file.  For example, you could have a mutable
   directory object DIR referring to a directory which you don't have
   permission to write.  If you do have permission to write some
   subdirectory SUBDIR of DIR, then you could use DIR to obtain a
   mutable directory object referring to SUBDIR, and make changes
   there.  The mutable/immutable distinction refers to directory and
   file *objects*, not to the nodes they refer to.  */


/* The type of a Subversion transaction object.  */
typedef struct svn_fs_txn_t svn_fs_txn_t;


/* Begin a new transaction on the filesystem FS; when committed, this
   transaction will create a new version.  Set *TXN_P to a pointer to
   an object representing the new transaction.  */
extern svn_error_t *svn_fs_txn_begin (svn_fs_txn_t **TXN_P, svn_fs_t *FS);


/* Commit the transaction TXN.  If the transaction conflicts with
   other changes committed to the repository, return an
   SVN_ERR_FS_CONFLICT error.  Otherwise, create a new filesystem
   version containing the changes made in TXN, and return zero.

   This call frees TXN, and any temporary resources it holds.  */
extern svn_error_t *svn_fs_txn_commit (svn_fs_txn_t *txn);


/* Abort the transaction TXN.  Any changes made in TXN are discarded,
   and the filesystem is left unchanged.

   This frees TXN, and any temporary resources it holds.  */
extern svn_error_t *svn_fs_txn_abort (svn_fs_txn_t *txn);


/* Select the root directory of version VERSION as the base root
   directory for the transaction TXN.  Set *ROOT to a directory object
   for that root dir.  ROOT is a mutable directory object.  */
extern svn_error_t *svn_fs_replace_root (svn_fs_dir_t **root,
					 svn_fs_txn_t *txn,
					 svn_vernum_t version);


/* Delete the entry named NAME from the directory DIR.  DIR must be a
   mutable directory object.  */
extern svn_error_t *svn_fs_unlink (svn_fs_dir_t *dir, svn_string_t *name);


/* Create a new file named NAME in the directory DIR, and set *FILE to
   a mutable file object for the new file.  DIR must be a mutable
   directory object.

   The new file will be based on the file named ANCESTOR_NAME in
   ANCESTOR_VERSION.  If ANCESTOR_NAME is zero, the file is completely
   new.  */
extern svn_error_t *svn_fs_add_file (svn_fs_file_t **file,
				     svn_fs_dir_t *dir,
				     svn_string_t *name,
				     svn_string_t *ancestor_name,
				     svn_vernum_t ancestor_version);


/* Replace the entry named NAME in the mutable directory DIR with a file, and
   set *FILE to a mutable file object representing that file.

   The file will be based on the file named ANCESTOR_NAME in
   ANCESTOR_VERSION.  If ANCESTOR_NAME is zero, the file is completely
   new.  */
extern svn_error_t *svn_fs_replace_file (svn_fs_file_t **file,
					 svn_fs_dir_t *dir,
					 svn_string_t *name,
					 svn_string_t *ancestor_name,
					 svn_vernum_t ancestor_version);


/* Apply a text delta to the mutable file FILE.
   Set *CONTENTS to a function ready to receive text delta windows
   describing the new file's contents relative to the given ancestor,
   or the empty file if ANCESTOR_NAME is zero.  The producer should
   pass CONTENTS_BATON to CONTENTS.  */
extern svn_error_t *svn_fs_apply_textdelta (svn_fs_file_t *file,
					    svn_txdelta_window_handler_t
					      **contents,
					    void **contents_baton);


/* Change a file's property's value, or add/delete a property.
   - FILE is the mutable file whose property should change.
   - NAME is the name of the property to change.
   - VALUE is the new value of the property, or zero if the property should
     be removed altogether.  */
extern svn_error_t *svn_fs_change_file_prop (svn_fs_file_t *file,
					     svn_string_t *name,
					     svn_string_t *value);


#endif /* SVN_FS_H */
