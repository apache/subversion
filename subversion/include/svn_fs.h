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
#include "svn_io.h"



/* Opening and creating filesystems.  */


/* An object representing a Subversion filesystem.  */
typedef struct svn_fs_t svn_fs_t;


/* Create a new filesystem object in POOL.  It doesn't refer to any
   actual repository yet; you need to invoke svn_fs_open_* or
   svn_fs_create_* on it for that to happen.  */
svn_fs_t *svn_fs_new (apr_pool_t *pool);


/* Free the filesystem object FS.  This frees memory, closes files,
   frees database library structures, etc.  */
svn_error_t *svn_fs_close_fs (svn_fs_t *fs);


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
void svn_fs_set_warning_func (svn_fs_t *fs,
			      svn_fs_warning_callback_t *warning,
			      void *warning_baton);



/* Subversion filesystems based on Berkeley DB.  */

/* There are many possible ways to implement the Subversion filesystem
   interface.  You could implement it directly using ordinary POSIX
   filesystem operations; you could build it using an SQL server as a
   back end; you could build it on RCS; and so on.

   The functions on this page create filesystem objects that use
   Berkeley DB (http://www.sleepycat.com) to store their data.
   Berkeley DB supports transactions and recoverability, making it
   well-suited for Subversion.

   A Berkeley DB ``environment'' is a Unix directory containing
   database files, log files, backing files for shared memory buffers,
   and so on --- everything necessary for a complex database
   application.  Each Subversion filesystem lives in a single Berkeley
   DB environment.  */


/* Create a new, empty Subversion filesystem, stored in a Berkeley DB
   environment named ENV.  Make FS refer to this new filesystem.
   FS provides the memory pool, warning function, etc.  */
svn_error_t *svn_fs_create_berkeley (svn_fs_t *fs, const char *env);


/* Make FS refer to the Subversion filesystem stored in the Berkeley
   DB environment ENV.  ENV must refer to a file or directory created
   by `svn_fs_create_berkeley'.

   Only one thread may operate on any given filesystem object at once.
   Two threads may access the same filesystem simultaneously only if
   they open separate filesystem objects.  */
svn_error_t *svn_fs_open_berkeley (svn_fs_t *fs, const char *env);


/* Perform any necessary non-catastrophic recovery on a Berkeley
   DB-based Subversion filesystem, stored in the environment ENV.  Do
   any necessary allocation within POOL.

   After an unexpected server exit, due to a server crash or a system
   crash, a Subversion filesystem based on Berkeley DB needs to run
   recovery procedures to bring the database back into a consistent
   state and release any locks that were held by the deceased process.
   The recovery procedures require exclusive access to the database
   --- while they execute, no other process or thread may access the
   database.

   In a server with multiple worker processes, like Apache, if a
   worker process accessing the filesystem dies, you must stop the
   other worker processes, and run recovery.  Then, the other worker
   processes can re-open the database and resume work.

   If the server exited cleanly, there is no need to run recovery, but
   there is no harm in it, either, and it take very little time.  So
   it's a fine idea to run recovery when the server process starts,
   before it begins handling any requests.  */

svn_error_t *svn_fs_berkeley_recover (const char *path,
				      apr_pool_t *pool);



/* Node and Node Revision ID's.  */

/* In a Subversion filesystem, a `node' corresponds roughly to an
   `inode' in a Unix filesystem:
   - A node is either a file or a directory.
   - A node's contents change over time.
   - When you change a node's contents, it's still the same node; it's
     just been changed.  So a node's identity isn't bound to a specific
     set of contents.
   - If you rename a node, it's still the same node, just under a
     different name.  So a node's identity isn't bound to a particular
     filename.

   A `node revision' refers to a node's contents at a specific point in
   time.  Changing a node's contents always creates a new revision of that
   node.  Once created, a node revision's contents never change.

   When we create a node, its initial contents are the initial revision of
   the node.  As users make changes to the node over time, we create new
   revisions of that same node.  When a user commits a change that deletes
   a file from the filesystem, we don't delete the node, or any revision
   of it --- those stick around to allow us to recreate prior revisions of
   the filesystem.  Instead, we just remove the reference to the node
   from the directory.

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


/* Within the code, we represent node and node revision ID's as arrays
   of integers, terminated by a -1 element.  This is the type of an
   element of a node ID.  */
typedef svn_revnum_t svn_fs_id_t;


/* Return the number of components in ID, not including the final -1.  */
int svn_fs_id_length (const svn_fs_id_t *id);


/* Return non-zero iff the node or node revision ID's A and B are equal.  */
int svn_fs_id_eq (const svn_fs_id_t *a, const svn_fs_id_t *b);


/* Return non-zero iff node revision A is an ancestor of node revision B.  
   If A == B, then we consider A to be an ancestor of B.  */
int svn_fs_id_is_ancestor (const svn_fs_id_t *a, const svn_fs_id_t *b);


/* Return the distance between node revisions A and B.  Return -1 if
   they are completely unrelated.  */
int svn_fs_id_distance (const svn_fs_id_t *a, const svn_fs_id_t *b);


/* Return a copy of ID, allocated from POOL.  */
svn_fs_id_t *svn_fs_copy_id (const svn_fs_id_t *id, apr_pool_t *pool);


/* Parse the LEN bytes at DATA as a node ID.  Return zero if the bytes
   are not a properly-formed node ID.

   Allocate the parsed ID in POOL.  As a special case for the Berkeley
   DB comparison function, if POOL is zero, malloc the ID.  It's
   generally better to use a pool if you can.  */
svn_fs_id_t *svn_fs_parse_id (const char *data, apr_size_t len,
                              apr_pool_t *pool);


/* Return a Subversion string containing the unparsed form of the node
   id ID.  Allocate the buffer for the unparsed form in POOL.  */
svn_string_t *svn_fs_unparse_id (const svn_fs_id_t *id, apr_pool_t *pool);


/* Nodes.  */

/* svn_fs_node_t is the common "superclass" for files, directories,
   and any other kinds of nodes we decide to add to the filesystem.
   Given an svn_fs_node_t, you can use the `svn_fs_node_is_*'
   functions to see what specific kind of node it is, and the
   `svn_fs_*_to_*' functions to cast between super- and sub-classes.  */
typedef struct svn_fs_node_t svn_fs_node_t;


/* Return non-zero iff NODE is a...  */
int svn_fs_node_is_dir (svn_fs_node_t *node);
int svn_fs_node_is_file (svn_fs_node_t *node);


/* Free the node object NODE.  */
void svn_fs_close_node (svn_fs_node_t *node);


/* Arrange for NODE to be closed when POOL is freed.

   This registers a cleanup function with POOL that calls
   `svn_fs_close_node' on NODE when POOL is freed.  If you later close
   NODE explicitly, you should call `svn_fs_kill_cleanup_node', to
   cancel the cleanup request; otherwise, the cleanup function will
   still run when POOL is freed, and try to close NODE again.  */
void svn_fs_cleanup_node (apr_pool_t *pool, svn_fs_node_t *node);


/* Cancel the request to close NODE when POOL is freed.  */
void svn_fs_kill_cleanup_node (apr_pool_t *pool, svn_fs_node_t *node);


/* Close NODE, and remove the request to clean it up from POOL.  This
   is the equivalent of calling `apr_run_cleanup' on the node cleanup
   function in POOL.  */
void svn_fs_run_cleanup_node (apr_pool_t *pool, svn_fs_node_t *node);


/* Set *VALUE_P to the value of the property of NODE named PROPNAME.
   If NODE has no property by that name, set *VALUE_P to zero.
   Allocate the result in POOL.  */
svn_error_t *svn_fs_get_node_prop (svn_string_t **value_p,
				   svn_fs_node_t *node,
				   svn_string_t *propname,
				   apr_pool_t *pool);
   

/* Set *TABLE_P to the entire property list of NODE, as an APR hash
   table allocated in POOL.  The resulting table maps property names
   to pointers to svn_string_t objects containing the property value.  */
svn_error_t *svn_fs_get_node_proplist (apr_hash_t **table_p,
				       svn_fs_node_t *node,
				       apr_pool_t *pool);
				       


/* Reading and traversing directories.  */


/* An object representing a directory in a Subversion filesystem.  */
typedef struct svn_fs_dir_t svn_fs_dir_t;


/* Return a directory object representing the same directory as NODE.
   If NODE is not a directory, return zero.

   This call does no allocation, and every call to this function on
   the same node object returns the same directory object.  Closing
   either NODE or the returned directory object closes both objects.  */
svn_fs_dir_t *svn_fs_node_to_dir (svn_fs_node_t *node);


/* Return a node object representing the same directory as DIR.

   This call does no allocation, and every call to this function on
   the same directory object returns the same node object.  Closing
   either DIR or the returned node object closes both objects.  */
svn_fs_node_t *svn_fs_dir_to_node (svn_fs_dir_t *dir);


/* Set *DIR to point to a directory object representing the root
   directory of revision V of filesystem FS.  */
svn_error_t *svn_fs_open_root (svn_fs_dir_t **dir,
			       svn_fs_t *fs,
			       svn_revnum_t v);


/* Set *CHILD_P to a node object representing the node named NAME in
   PARENT_DIR.  NAME is a directory path. 

   Do any necessary temporary allocation in POOL.  (The returned node
   is *not* allocated in POOL; call svn_fs_cleanup_node if you want that
   behavior.)

   The details about NAME's syntax:

   - NAME must be a series of path components, encoded using UTF-8,
     and separated by slash characters (U+002f).
   - NAME may not contain the null character (U+0000).
   - Sequences of two or more consecutive slash characters are treated
     like a single slash.
   - If NAME ends with a slash, it refers to the same node it would
     without the slash, but that node must be a directory, or else the
     function returns an SVN_ERR_FS_PATH_SYNTAX error.
   - If any path component is '.' or '..', the function returns an
     SVN_ERR_FS_PATH_SYNTAX error.
   - NAME is always interpreted relative to PARENT_DIR.  If NAME
     starts with a '/', this function will return an
     SVN_ERR_FS_PATH_SYNTAX error.  If you want to process absolute
     paths, you'll need to provide a root directory object as
     PARENT_DIR, and strip off the leading slash.  */
svn_error_t *svn_fs_open_node (svn_fs_node_t **child_p,
			       svn_fs_dir_t *parent_dir,
			       svn_string_t *name,
			       apr_pool_t *pool);


/* Free the directory object DIR.  */
void svn_fs_close_dir (svn_fs_dir_t *dir);


/* The type of a Subversion directory entry.  */
typedef struct svn_fs_dirent_t {

  /* The name of this directory entry.  */
  svn_string_t *name;

  /* The node revision ID it names.  */
  svn_fs_id_t *id;

} svn_fs_dirent_t;


/* Set *TABLE_P to a newly allocated APR hash table containing the
   entries of the directory DIR.  The keys of the table are entry
   names, as byte strings; the table's values are pointers to
   svn_fs_dirent_t structures.  Allocate the table and its contents in
   POOL.  */
svn_error_t *svn_fs_dir_entries (apr_hash_t **table_p,
				 svn_fs_dir_t *dir,
				 apr_pool_t *pool);


/* Accessing files.  */

/* An object representing a file in a Subversion filesystem.  */
typedef struct svn_fs_file_t svn_fs_file_t;


/* Free the file object FILE.  */
void svn_fs_close_file (svn_fs_file_t *file);


/* Return a file object representing the same file as NODE.
   If NODE is not a file, return zero.

   This call does no allocation, and every call to this function on
   the same node object returns the same file object.  Closing
   either NODE or the returned file object closes both objects.  */
svn_fs_file_t *svn_fs_node_to_file (svn_fs_node_t *node);


/* Return a node object representing the same file as FILE.

   This call does no allocation, and every call to this function on
   the same file object returns the same node object.  Closing
   either FILE or the returned node object closes both objects.  */
svn_fs_node_t *svn_fs_file_to_node (svn_fs_file_t *file);


/* Set *LENGTH to the length of FILE, in bytes.  */
svn_error_t *svn_fs_file_length (apr_off_t *length,
				 svn_fs_file_t *file);


/* Set *CONTENTS to a `read'-like function which will return the
   contents of FILE; see the description of svn_read_fn_t in
   `svn_delta.h'.  Set *CONTENTS_BATON to a baton to pass to CONTENTS.
   Allocate the baton in POOL.

   You may only use CONTENTS and CONTENTS_BATON for as long as the
   underlying filesystem is open.  */
svn_error_t *svn_fs_file_contents (svn_read_fn_t **contents,
				   void **contents_baton,
				   svn_fs_file_t *file,
				   apr_pool_t *pool);



/* Computing deltas.  */

/* Compute the differences between SOURCE_DIR and TARGET_DIR, and make
   calls describing those differences on EDITOR, using the provided
   EDIT_BATON.  SOURCE_DIR and TARGET_DIR must be from the same
   filesystem.

   The caller must call editor->close_edit on EDIT_BATON;
   svn_fs_dir_delta does not close the edit itself.

   If POOL is non-zero, do any allocation necessary for the delta
   computation there; you must ensure that POOL is freed before the
   underlying filesystem is closed.  If POOL is zero, do allocation in
   the pool of the filesystem SOURCE_DIR and TARGET_DIR came from; it
   will be freed when the filesystem is closed.  */
svn_error_t *svn_fs_dir_delta (svn_fs_dir_t *source_dir,
			       svn_fs_dir_t *target_dir,
			       svn_delta_edit_fns_t *editor,
			       void *edit_baton,
			       apr_pool_t *pool);


/* Set *STREAM to a pointer to a delta stream that will turn the
   contents of SOURCE_FILE into the contents of TARGET_FILE.  If
   SOURCE_FILE is zero, treat it as a file with zero length.

   This function does not compare the two files' properties.
   
   If POOL is non-zero, do any allocation needed for the delta
   computation there.  If POOL is zero, allocate in a pool that will
   be freed when STREAM is freed.  */
svn_error_t *svn_fs_file_delta (svn_txdelta_stream_t **stream,
				svn_fs_file_t *source_file,
				svn_fs_file_t *target_file,
				apr_pool_t *pool);



/* Committing changes to Subversion filesystems.  */


/* To make a change to the Subversion filesystem:
   - Create a transaction object, using `svn_fs_begin_txn'.
   - Create a new root directory object, using `svn_fs_replace_root'.
   - Make whatever changes you like to that directory tree, using
     the appropriate functions below.
   - Commit the transaction, using `svn_fs_commit_txn'.

   The filesystem implementation guarantees that your commit will
   either:
   - succeed completely, so that all of the changes are committed to
     create a new revision of the filesystem, or
   - fail completely, leaving the filesystem unchanged.

   Until you commit the transaction, any changes you make are
   invisible.  Only when your commit succeeds do they become visible
   to the outside world, as a new revision of the filesystem.

   If you begin a transaction, and then decide you don't want to make
   the change after all (say, because your net connection with the
   client disappeared before the change was complete), you can call
   `svn_fs_abort_txn', to cancel the entire transaction; this
   leaves the filesystem unchanged.

   The only way to change the contents of files or directories, or
   their properties, is by making a transaction and creating a new
   revision, as described above.  Once a revision has been committed, it
   never changes again; the filesystem interface provides no means to
   go back and edit the contents of an old revision.  Once history has
   been recorded, it is set in stone.  Clients depend on this property
   to do updates and commits reliably; proxies depend on this property
   to cache changes accurately; and so on.


   There are two kinds of nodes: mutable, and immutable.  The
   committed revisions in the filesystem consist entirely of immutable
   nodes, whose contents never change.  An incomplete transaction,
   which the user is in the process of constructing, uses mutable
   nodes for those nodes which have been changed so far, and refer
   back to immutable nodes for portions of the tree which haven't been
   changed yet in this transaction.  Immutable nodes, as part of
   committed transactions, never refer to mutable nodes, which are
   part of uncommitted transactions.

   The functions above start from some revision's root directory, and
   walk down from there; they can only access immutable nodes, in
   extant revisions of the filesystem.  Since those nodes are in the
   history, they can never change.  The functions below, for building
   transactions, create mutable nodes.  They refer to new nodes (or
   new revisions of existing nodes), which you can change as necessary
   to create the new revision the way you want.

   In other words, you use immutable nodes for reading committed,
   fixed revisions of the filesystem, and you use mutable nodes for
   building new directories and files, as part of a transaction.

   Note that the terms "immutable" and "mutable" describe whether the
   nodes are part of a committed filesystem revision or not --- not the
   permissions on the nodes they refer to.  Even if you aren't
   authorized to modify the filesystem's root directory, you could
   still have a mutable directory object referring to it.  Since it's
   mutable, you could call `svn_fs_replace_subdir' to get another
   mutable directory object referring to a directory you do have
   permission to change.  Mutability refers to the role of the node
   --- part of an existing revision, or part of a new one --- which is
   independent of your authorization to make changes in a particular
   place.

   A pattern to note in the interface below: you can't get mutable
   objects from immutable objects.  If you have an immutable directory
   object DIR (like those produced by calling `svn_fs_open', and then
   `svn_fs_cast_dir'), you can't call `svn_fs_replace_subdir' on DIR
   to get a mutable directory object for one of DIR's subdirectories.
   You need to use mutable objects from the very beginning, starting
   with a call to `svn_fs_replace_root'.


   The following calls make changes to nodes:
     svn_fs_delete
     svn_fs_add_file            svn_fs_add_dir
     svn_fs_replace_file        svn_fs_replace_dir
     svn_fs_apply_textdelta    
     svn_fs_change_prop

   Any of these functions may return an SVN_ERR_FS_CONFLICT error.
   This means that the change you requested conflicts with some other
   change committed to the repository since the base revision you
   selected.  If you get this error, the transaction you're building
   is still live --- it's up to you whether you want to abort the
   transaction entirely, try a different revision of the change, or
   drop the conflicting part from the change.  But if you want to
   abort, you'll still need to call svn_fs_abort_txn; simply getting a
   conflict error doesn't free the temporary resources held by the
   transaction.  */


/* The type of a Subversion transaction object.  */
typedef struct svn_fs_txn_t svn_fs_txn_t;


/* Begin a new transaction on the filesystem FS; when committed, this
   transaction will create a new revision.  Set *TXN to a pointer to
   an object representing the new transaction.  */
svn_error_t *svn_fs_begin_txn (svn_fs_txn_t **txn,
			       svn_fs_t *fs);


/* Commit the transaction TXN.  If the transaction conflicts with
   other changes committed to the repository, return an
   SVN_ERR_FS_CONFLICT error.  Otherwise, create a new filesystem
   revision containing the changes made in TXN, and return zero.

   If the commit succeeds, it frees TXN, and any temporary resources
   it holds.  If the commit fails, TXN is still valid; you can make
   more operations to resolve the conflict, or call `svn_fs_abort_txn'
   to abort the transaction.  */
svn_error_t *svn_fs_commit_txn (svn_fs_txn_t *txn);


/* Abort the transaction TXN.  Any changes made in TXN are discarded,
   and the filesystem is left unchanged.

   This frees TXN, and any temporary resources it holds.  */
svn_error_t *svn_fs_abort_txn (svn_fs_txn_t *txn);


/* Close the transaction TXN.  This is neither an abort nor a commit;
   the state of the transaction so far is stored in the filesystem, to
   be resumed later.  See the section below, titled "Transactions are
   persistent," for an explanation of how to resume work on a transaction.  */
svn_error_t *svn_fs_close_txn (svn_fs_txn_t *txn);


/* Arrange for TXN to be closed when POOL is freed.

   This registers a cleanup function with POOL that calls
   `svn_fs_close_txn' on TXN when POOL is freed.  If you later close
   TXN explicitly, you should call `svn_fs_kill_cleanup_txn', to
   cancel the cleanup request; otherwise, the cleanup function will
   still run when POOL is freed, and try to close TXN again.  */
void svn_fs_cleanup_txn (apr_pool_t *pool, svn_fs_txn_t *txn);


/* Cancel the request to close TXN when POOL is freed.  */
void svn_fs_kill_cleanup_txn (apr_pool_t *pool, svn_fs_txn_t *txn);


/* Close TXN, and remove the request to clean it up from POOL.  This
   is the equivalent of calling `apr_run_cleanup' on the transaction
   cleanup function in POOL.  */
void svn_fs_run_cleanup_txn (apr_pool_t *pool, svn_fs_txn_t *txn);


/* Select the root directory of revision REVISION as the base root
   directory for the transaction TXN.  Set *ROOT to a directory object
   for that root dir.  ROOT is a mutable directory object.

   Every change starts with a call to this function.  In order to get
   a mutable file or directory object, you need to have a mutable
   directory object for its parent --- this is the function that gives
   you your first mutable directory object.  */
svn_error_t *svn_fs_replace_root (svn_fs_dir_t **root,
				  svn_fs_txn_t *txn,
				  svn_revnum_t revision);


/* Delete the entry named NAME from the directory DIR.  DIR must be a
   mutable directory object.  Do any necessary temporary allocation in
   POOL.  */
svn_error_t *svn_fs_delete (svn_fs_dir_t *dir, svn_string_t *name,
			    apr_pool_t *pool);


/* Create a new subdirectory of PARENT named NAME.  PARENT must be
   mutable.  Set *CHILD to a pointer to a mutable directory object
   referring to the child.  CHILD is allocated in the pool of the
   underlying transaction.

   The new directory will be based on the directory BASE, an immutable
   directory object.  If BASE is zero, the directory is completely
   new.  */
svn_error_t *svn_fs_add_dir (svn_fs_dir_t **child,
			     svn_fs_dir_t *parent,
			     svn_string_t *name,
			     svn_fs_dir_t *base);


/* Change the subdirectory of PARENT named NAME.  PARENT must be
   mutable.  Set *CHILD to a pointer to a mutable directory object
   referring to the child.  CHILD is allocated in the pool of the
   underlying transaction.

   The new directory will be based on the directory BASE, an immutable
   directory object.  If BASE is zero, the directory will be
   completely new.  If BASE is the magic value `svn_fs_default_base',
   cast (using svn_fs_node_to_dir) to a directory object, then the new
   directory is based on the existing directory named NAME in PARENT.  */
svn_error_t *svn_fs_replace_dir (svn_fs_dir_t **child,
				 svn_fs_dir_t *parent,
				 svn_string_t *name,
				 svn_fs_dir_t *base);


/* Create a new file named NAME in the directory DIR, and set *FILE to
   a mutable file object for the new file.  DIR must be a mutable
   directory object.  FILE is allocated in the pool of the underlying
   transaction.

   The new file will be based on BASE, an immutable file.  If BASE is
   zero, the file is completely new.  */
svn_error_t *svn_fs_add_file (svn_fs_file_t **file,
			      svn_fs_dir_t *dir,
			      svn_string_t *name,
			      svn_fs_file_t *base);


/* Replace the entry named NAME in the mutable directory DIR with a
   file, and set *FILE to a mutable file object representing that
   file.  FILE is allocated in the pool of the underlying transaction.

   The file will be based on BASE, an immutable file.  If BASE is
   zero, the file is completely new.  If BASE is the magic value
   `svn_fs_default_base', cast to a file object, then the new file is
   based on the existing file named NAME in DIR.  */
svn_error_t *svn_fs_replace_file (svn_fs_file_t **file,
				  svn_fs_dir_t *dir,
				  svn_string_t *name,
				  svn_fs_file_t *base);


/* Apply a text delta to the mutable file FILE.
   Set *CONTENTS to a function ready to receive text delta windows
   describing the new file's contents relative to the given base,
   or the empty file if BASE_NAME is zero.  The producer should
   pass CONTENTS_BATON to CONTENTS.  */
svn_error_t *svn_fs_apply_textdelta (svn_fs_file_t *file,
				     svn_txdelta_window_handler_t **contents,
				     void **contents_baton);


/* Change a node's property's value, or add/delete a property.
   - NODE is the mutable node whose property should change.
   - NAME is the name of the property to change.
   - VALUE is the new value of the property, or zero if the property should
     be removed altogether.  */
svn_error_t *svn_fs_change_prop (svn_fs_node_t *node,
				 svn_string_t *name,
				 svn_string_t *value);


/* A magic node object.  If we pass a pointer to this object as the
   BASE argument to certain functions, that has a special meaning;
   search for mentions of `svn_fs_default_base' above to see what they
   mean.  */
extern svn_fs_node_t *svn_fs_default_base;


/* Return non-zero iff NODE is a mutable node --- one that can be
   changed as part of a transaction.  */
int svn_fs_node_is_mutable (svn_fs_node_t *node);



/* Transactions are persistent.  */

/* Transactions are actually persistent objects, stored in the
   database.  You can open a filesystem, begin a transaction, and
   close the filesystem, and then a separate process could open the
   filesystem, pick up the same transaction, and continue work on it.
   When a transaction is successfully committed, it is removed from
   the database.

   Every transaction is assigned a name.  You can open a transaction
   by name, and resume work on it, or find out the name of an existing
   transaction.  You can also list all the transactions currently
   present in the database.

   Transaction names are guaranteed to contain only letters (upper-
   and lower-case), digits, `-', and `.', from the ASCII character
   set.  */


/* Set *NAME_P to the name of the transaction TXN.

   If POOL is zero, allocate NAME in the transaction's pool; it will
   be freed when the transaction is committed or aborted.  If POOL is
   non-zero, do allocation there.  */
svn_error_t *svn_fs_txn_name (svn_string_t **name_p,
			      svn_fs_txn_t *txn,
			      apr_pool_t *pool);


/* Open the transaction named NAME in the filesystem FS.  Set *TXN to
   the transaction.

   If POOL is zero, allocate NAME in the filesystem's pool; it will be
   freed when the transaction is committed or aborted.  If POOL is
   non-zero, it must be a subpool of the filesystem's pool; do
   allocation there.  */
svn_error_t *svn_fs_open_txn (svn_fs_txn_t **txn,
			      svn_fs_t *fs,
			      svn_string_t *name,
			      apr_pool_t *pool);


/* Set *NAMES to a null-terminated array of pointers to strings,
   containing the names of all the currently active transactions in
   the filesystem FS.

   If POOL is zero, allocate NAME in the filesystem's pool; it will be
   freed when the transaction is committed or aborted.  If POOL is
   non-zero, it must be a subpool of the filesystem's pool; do
   allocation there.  */
svn_error_t *svn_fs_list_transactions (svn_string_t ***names,
				       svn_fs_t *fs,
				       apr_pool_t *pool);


/* Non-historical properties.  */

/* [[Yes, do tell.]] */

#endif /* SVN_FS_H */
