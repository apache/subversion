/* svn_fs.h :  interface to the Subversion filesystem
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

/* ==================================================================== */


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_FS_H
#define SVN_FS_H

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_io.h"


/* Data written to the filesystem through the svn_fs_apply_textdelta()
   interface is cached in memory until the end of the data stream, or
   until a size trigger is hit.  Define that trigger here (in bytes).
   Setting the value to 0 will result in no filesystem buffering at
   all.  The value only really matters when dealing with file contents
   bigger than the value itself.  Above that point, large values here
   allow the filesystem to buffer more data in memory before flushing
   to the database, which increases memory usage but greatly decreases
   the amount of disk access (and log-file generation) in database.
   Smaller values will limit your overall memory consumption, but can
   drastically hurt throughput by necessitating more write operations
   to the database (which also generates more log-files).  */
#define SVN_FS_WRITE_BUFFER_SIZE   4096000


/* Opening and creating filesystems.  */


/* An object representing a Subversion filesystem.  */
typedef struct svn_fs_t svn_fs_t;


/* Create a new filesystem object in POOL.  It doesn't refer to any
   actual repository yet; you need to invoke svn_fs_open_* or
   svn_fs_create_* on it for that to happen.  

   NOTE: you probably don't want to use this directly, especially not
   if it's followed immediately by a call to svn_fs_open_berkeley().
   Take a look at svn_repos_open() instead.  */
svn_fs_t *svn_fs_new (apr_pool_t *pool);


/* Free the filesystem object FS.  This frees memory, closes files,
   frees database library structures, etc.  */
svn_error_t *svn_fs_close_fs (svn_fs_t *fs);


/* The type of a warning callback function.  BATON is the value specified
   in the call to `svn_fs_set_warning_func'; the filesystem passes it through
   to the callback.  FMT is a printf-style format string, which tells us
   how to interpret any successive arguments.  */
#ifndef SWIG
typedef void (*svn_fs_warning_callback_t) (void *baton, const char *fmt, ...);
#endif


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
                              svn_fs_warning_callback_t warning,
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
   environment under PATH.  Make FS refer to this new filesystem.
   FS provides the memory pool, warning function, etc.  If PATH
   exists, it must be an empty directory.  */
svn_error_t *svn_fs_create_berkeley (svn_fs_t *fs, const char *path);


/* Make FS refer to the Berkeley DB-based Subversion filesystem at
   PATH.  PATH must refer to a file or directory created by
   `svn_fs_create_berkeley'.

   Only one thread may operate on any given filesystem object at once.
   Two threads may access the same filesystem simultaneously only if
   they open separate filesystem objects.  

   NOTE: you probably don't want to use this directly, especially not
   if it's immediately preceded by a call to svn_fs_new().  Take a
   look at svn_repos_open() instead.  */
svn_error_t *svn_fs_open_berkeley (svn_fs_t *fs, const char *path);


/* Return the path to FS's repository, allocated in POOL.
   Note: this is just what was passed to svn_fs_create_berkeley() or
   svn_fs_open_berkeley() -- might be absolute, might not.  */
const char *svn_fs_berkeley_path (svn_fs_t *fs, apr_pool_t *pool);


/* Register an error handling function for Berkeley DB error messages.
   If a Berkeley DB error occurs, the filesystem will call HANDLER
   with two strings: an error message prefix, which will be zero, and
   an error message.  HANDLER should print it out, log it somewhere,
   etc.

   Since Berkeley DB's error messages are sometimes much more
   informative than the error codes the functions return, it's worth
   calling this function and providing some kind of error message
   handler.

   This function calls `DBENV->set_errcall', with HANDLER as the
   `db_errcall_fcn' argument.  */
svn_error_t *svn_fs_set_berkeley_errcall (svn_fs_t *fs, 
                                          void (*handler) (const char *errpfx,
                                                           char *msg));


/* Delete the Berkeley DB-based filesystem PATH.  This deletes the
   database files, log files, shared memory segments, etc.  PATH should
   refer to a file or directory created by `svn_fs_create_berkeley'.  */
svn_error_t *svn_fs_delete_berkeley (const char *PATH, apr_pool_t *pool);


/* Perform any necessary non-catastrophic recovery on a Berkeley
   DB-based Subversion filesystem, stored in the environment PATH.  Do
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


/* Return true iff PARENT is a direct parent of CHILD.  */
int svn_fs_is_parent (const svn_fs_id_t *parent,
                      const svn_fs_id_t *child);


/* Return the distance between node revisions A and B.  Return -1 if
   they are completely unrelated.  */
int svn_fs_id_distance (const svn_fs_id_t *a, const svn_fs_id_t *b);


/* Return a copy of ID, allocated from POOL.  */
svn_fs_id_t *svn_fs_copy_id (const svn_fs_id_t *id, apr_pool_t *pool);


/* Return the predecessor id to ID, allocated in POOL.  If there is no
   possible predecessor id, return NULL.

   Does not check that the predecessor id is actually present in the
   filesystem.

   Does not check that ID is a valid node revision ID.  If you pass in
   something else, the results are undefined.  */
svn_fs_id_t *svn_fs_predecessor_id (const svn_fs_id_t *id, apr_pool_t *pool);


/* Perform an exhaustive traversal through node-id and copy-from
   history to determine if the nodes associated with ID1 and ID2, and
   found in filesystem FS, are related.  If so, set *RELATED to 1,
   else to 0.  Use POOL for allocations.  */
svn_error_t *svn_fs_check_related (int *related, 
                                   svn_fs_t *fs,
                                   const svn_fs_id_t *id1,
                                   const svn_fs_id_t *id2,
                                   apr_pool_t *pool);


/* Parse the LEN bytes at DATA as a node or node revision ID.  Return
   zero if the bytes are not a properly-formed ID.  A properly formed
   ID matches the regexp:

       [0-9]+(\.[0-9]+)*

   Allocate the parsed ID in POOL.  If POOL is zero, malloc the ID; we
   need this in certain cases where we can't pass in a pool, but it's
   generally best to use a pool whenever possible.  */
svn_fs_id_t *svn_fs_parse_id (const char *data, apr_size_t len,
                              apr_pool_t *pool);


/* Return a Subversion string containing the unparsed form of the node
   or node revision id ID.  Allocate the string containing the
   unparsed form in POOL.  */
svn_stringbuf_t *svn_fs_unparse_id (const svn_fs_id_t *id, apr_pool_t *pool);



/* Transactions.  */


/* To make a change to a Subversion filesystem:
   - Create a transaction object, using `svn_fs_begin_txn'.
   - Call `svn_fs_txn_root', to get the transaction's root directory.
   - Make whatever changes you like in that tree.
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


   There are two kinds of nodes in the filesystem: mutable, and
   immutable.  Revisions in the filesystem consist entirely of
   immutable nodes, whose contents never change.  A transaction in
   progress, which the user is still constructing, uses mutable nodes
   for those nodes which have been changed so far, and refers to
   immutable nodes from existing revisions for portions of the tree
   which haven't been changed yet in that transaction.

   Immutable nodes, as part of revisions, never refer to mutable
   nodes, which are part of uncommitted transactions.  Mutable nodes
   may refer to immutable nodes, or other mutable nodes.

   Note that the terms "immutable" and "mutable" describe whether or
   not the nodes have been changed as part of a transaction --- not
   the permissions on the nodes they refer to.  Even if you aren't
   authorized to modify the filesystem's root directory, you might be
   authorized to change some descendant of the root; doing so would
   create a new mutable copy of the root directory.  Mutability refers
   to the role of the node: part of an existing revision, or part of a
   new one.  This is independent of your authorization to make changes
   to a given node.


   Transactions are actually persistent objects, stored in the
   database.  You can open a filesystem, begin a transaction, and
   close the filesystem, and then a separate process could open the
   filesystem, pick up the same transaction, and continue work on it.
   When a transaction is successfully committed, it is removed from
   the database.

   Every transaction is assigned a name.  You can open a transaction
   by name, and resume work on it, or find out the name of a
   transaction you already have open.  You can also list all the
   transactions currently present in the database.

   Transaction names are guaranteed to contain only letters (upper-
   and lower-case), digits, `-', and `.', from the ASCII character
   set.  */



/* The type of a Subversion transaction object.  */
typedef struct svn_fs_txn_t svn_fs_txn_t;


/* Begin a new transaction on the filesystem FS, based on existing
   revision REV.  Set *TXN_P to a pointer to the new transaction.
   When committed, this transaction will create a new revision.

   Allocate the new transaction in POOL; when POOL is freed, the new
   transaction will be closed (neither committed nor aborted).  You
   can also close the transaction explicitly, using
   `svn_fs_close_txn'.  

     >> Note: if you're building a txn for committing, you probably <<
     >> don't want to call this directly.  Instead, call            <<
     >> svn_repos_fs_begin_txn_for_commit(), which honors the       <<
     >> repository's hook configurations.                           <<
*/
svn_error_t *svn_fs_begin_txn (svn_fs_txn_t **txn_p,
                               svn_fs_t *fs,
                               svn_revnum_t rev,
                               apr_pool_t *pool);


/* Commit TXN.

     >> Note: you usually don't want to call this directly.        <<
     >> Instead, call svn_repos_fs_commit_txn(), which honors the  <<
     >> repository's hook configurations.                          <<

   If the transaction conflicts with other changes committed to the
   repository, return an SVN_ERR_FS_CONFLICT error.  Otherwise, create
   a new filesystem revision containing the changes made in TXN,
   storing that new revision number in *NEW_REV, and return zero.

   If CONFLICT_P is non-zero, use it to provide details on any
   conflicts encountered merging TXN with the most recent committed
   revisions.  If a conflict occurs, set *CONFLICT_P to the path of
   the conflict in TXN.  Otherwise, set *CONFLICT_P to null.

   If the commit succeeds, it frees TXN, and any temporary resources
   it holds.  Any root objects (see below) referring to the root
   directory of TXN become invalid; performing any operation on them
   other than closing them will produce an SVN_ERR_FS_DEAD_TRANSACTION
   error.

   If the commit fails, TXN is still valid; you can make more
   operations to resolve the conflict, or call `svn_fs_abort_txn' to
   abort the transaction.  */
svn_error_t *svn_fs_commit_txn (const char **conflict_p,
                                svn_revnum_t *new_rev,
                                svn_fs_txn_t *txn);


/* Abort the transaction TXN.  Any changes made in TXN are discarded,
   and the filesystem is left unchanged.

   If the abort succeeds, it frees TXN, and any temporary resources
   it holds.  Any root objects referring to TXN's root directory
   become invalid; performing any operation on them other than closing
   them will produce an SVN_ERR_FS_DEAD_TRANSACTION error.  */
svn_error_t *svn_fs_abort_txn (svn_fs_txn_t *txn);


/* Set *NAME_P to the name of the transaction TXN, as a
   null-terminated string.  Allocate the name in POOL.  */
svn_error_t *svn_fs_txn_name (const char **name_p,
                              svn_fs_txn_t *txn,
                              apr_pool_t *pool);


/* Return the filesystem to which TXN belongs.  */
svn_fs_t *svn_fs_txn_fs (svn_fs_txn_t *txn);


/* Return TXN's pool.  */
apr_pool_t *svn_fs_txn_pool (svn_fs_txn_t *txn);


/* Return TXN's base revision.  If TXN's base root id is an mutable
   node, return 0.  */
svn_revnum_t svn_fs_txn_base_revision (svn_fs_txn_t *txn);



/* Open the transaction named NAME in the filesystem FS.  Set *TXN to
   the transaction.

   If there is no such transaction, SVN_ERR_FS_NO_SUCH_TRANSACTION is
   the error returned.

   Allocate the new transaction in POOL; when POOL is freed, the new
   transaction will be closed (neither committed nor aborted).  You
   can also close the transaction explicitly, using
   `svn_fs_close_txn'.  */
svn_error_t *svn_fs_open_txn (svn_fs_txn_t **txn,
                              svn_fs_t *fs,
                              const char *name,
                              apr_pool_t *pool);


/* Close the transaction TXN.  This is neither an abort nor a commit;
   the state of the transaction so far is stored in the filesystem, to
   be opened again later.  */
svn_error_t *svn_fs_close_txn (svn_fs_txn_t *txn);


/* Set *NAMES_P to a null-terminated array of pointers to strings,
   containing the names of all the currently active transactions in
   the filesystem FS.  Allocate the array in POOL.  */
svn_error_t *svn_fs_list_transactions (char ***names_p,
                                       svn_fs_t *fs,
                                       apr_pool_t *pool);

/* Transaction properties */

/* Set *VALUE_P to the value of the property named PROPNAME on
   transaction TXN.  If TXN has no property by that name, set *VALUE_P
   to zero.  Allocate the result in POOL.  */
svn_error_t *svn_fs_txn_prop (svn_string_t **value_p,
                              svn_fs_txn_t *txn,
                              const char *propname,
                              apr_pool_t *pool);


/* Set *TABLE_P to the entire property list of transaction TXN in
   filesystem FS, as an APR hash table allocated in POOL.  The
   resulting table maps property names to pointers to svn_string_t
   objects containing the property value.  */
svn_error_t *svn_fs_txn_proplist (apr_hash_t **table_p,
                                  svn_fs_txn_t *txn,
                                  apr_pool_t *pool);


/* Change a tranactions TXN's property's value, or add/delete a
   property.  NAME is the name of the property to change, and VALUE is
   the new value of the property, or zero if the property should be
   removed altogether.  Do any necessary temporary allocation in
   POOL. */
svn_error_t *svn_fs_change_txn_prop (svn_fs_txn_t *txn,
                                     const char *name,
                                     const svn_string_t *value,
                                     apr_pool_t *pool);



/* Roots.  */

/* An svn_fs_root_t object represents the root directory of some
   revision or transaction in a filesystem.  To refer to particular
   node, you provide a root, and a directory path relative that root.  */

typedef struct svn_fs_root_t svn_fs_root_t;


/* Set *ROOT_P to the root directory of revision REV in filesystem FS.
   Allocate *ROOT_P in POOL.  */
svn_error_t *svn_fs_revision_root (svn_fs_root_t **root_p,
                                   svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   apr_pool_t *pool);


/* Set *ROOT_P to the root directory of TXN.  Allocate *ROOT_P in POOL.  */
svn_error_t *svn_fs_txn_root (svn_fs_root_t **root_p,
                              svn_fs_txn_t *txn,
                              apr_pool_t *pool);


/* Set *ROOT_P to a root object that can be used to access nodes by
   node revision ID in FS.  Whenever you pass *ROOT_P to a filesystem
   access function, any filename "relative" to *ROOT_P must actually
   be the printed form of a node revision ID: a sequence of decimal
   numbers without leading zeros, separated by '.' characters,
   containing an even number of numbers; otherwise, return
   SVN_ERR_FS_NOT_ID.  Allocate ROOT_P in POOL.  */
svn_error_t *svn_fs_id_root (svn_fs_root_t **root_p,
                             svn_fs_t *fs,
                             apr_pool_t *pool);


/* Free the root directory ROOT.  Simply clearing or destroying the
   pool ROOT was allocated in will have the same effect as calling
   this function.  */
void svn_fs_close_root (svn_fs_root_t *root);


/* Return the filesystem to which ROOT belongs.  */
svn_fs_t *svn_fs_root_fs (svn_fs_root_t *root);


/* Return true iff ROOT is a transaction/revision/id root.  */
int svn_fs_is_txn_root      (svn_fs_root_t *root);
int svn_fs_is_revision_root (svn_fs_root_t *root);
int svn_fs_is_id_root       (svn_fs_root_t *root);


/* If ROOT is the root of a transaction, return a pointer to the name
   of the transaction; otherwise, return zero.  The name is owned by
   ROOT, and will be freed when ROOT is closed.  */
const char *svn_fs_txn_root_name (svn_fs_root_t *root,
                                  apr_pool_t *pool);


/* If ROOT is the root of a revision, return the revision number.
   Otherwise, return -1.  */
svn_revnum_t svn_fs_revision_root_revision (svn_fs_root_t *root);



/* Directory entry names and directory paths.  */

/* Here are the rules for directory entry names, and directory paths:

   A directory entry name is a Unicode string encoded in UTF-8, and
   may not contain the null character (U+0000).  The name should be in
   Unicode canonical decomposition and ordering.  No directory entry
   may be named '.', '..', or the empty string.  Given a directory
   entry name which fails to meet these requirements, a filesystem
   function returns an SVN_ERR_FS_PATH_SYNTAX error.

   A directory path is a sequence of zero or more directory entry
   names, separated by slash characters (U+002f), and possibly ending
   with slash characters.  Sequences of two or more consecutive slash
   characters are treated as if they were a single slash.  If a path
   ends with a slash, it refers to the same node it would without the
   slash, but that node must be a directory, or else the function
   returns an SVN_ERR_FS_NOT_DIRECTORY error.

   A path consisting of the empty string, or a string containing only
   slashes, refers to the root directory.  */



/* Operations appropriate to all kinds of nodes.  */

/* Return the type of node present at PATH under ROOT.  If PATH
   does not exist under ROOT, set *KIND to svn_node_none. */
svn_node_kind_t svn_fs_check_path (svn_fs_root_t *root,
                                   const char *path,
                                   apr_pool_t *pool);


/* Allocate and return an array *REVS of svn_revnum_t revisions in
   which PATH under ROOT was modified.  Use POOL for all allocations.

   NOTE: This function uses node-id ancestry alone to determine
   modifiedness, and therefore does NOT claim that in any of the
   returned revisions file contents changed, properties changed,
   directory entries lists changed, etc.  */
svn_error_t *svn_fs_revisions_changed (apr_array_header_t **revs,
                                       svn_fs_root_t *root,
                                       const char *path,
                                       apr_pool_t *pool);


/* Set *IS_DIR to non-zero iff PATH in ROOT is a directory.
   Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs_is_dir (int *is_dir,
                            svn_fs_root_t *root,
                            const char *path,
                            apr_pool_t *pool);

/* Set *IS_FILE to non-zero iff PATH in ROOT is a file.
   Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs_is_file (int *is_file,
                             svn_fs_root_t *root,
                             const char *path,
                             apr_pool_t *pool);


/* Set *ID_P to the node revision ID of PATH in ROOT, allocated in POOL.

   If ROOT is the root of a transaction, keep in mind that other
   changes to the transaction can change which node PATH refers to,
   and even whether the path exists at all.  */
svn_error_t *svn_fs_node_id (svn_fs_id_t **id_p,
                             svn_fs_root_t *root,
                             const char *path,
                             apr_pool_t *pool);

/* Set *REVISION to the revision in which PATH under ROOT was created.
   Use POOL for any temporary allocations.  *REVISION will be set to
   SVN_INVALID_REVNUM for uncommitted nodes (i.e. modified nodes under
   a transaction root).  */
svn_error_t *svn_fs_node_created_rev (svn_revnum_t *revision,
                                      svn_fs_root_t *root,
                                      const char *path,
                                      apr_pool_t *pool);

/* Set *VALUE_P to the value of the property named PROPNAME of PATH in
   ROOT.  If the node has no property by that name, set *VALUE_P to
   zero.  Allocate the result in POOL.  */
svn_error_t *svn_fs_node_prop (svn_string_t **value_p,
                               svn_fs_root_t *root,
                               const char *path,
                               const char *propname,
                               apr_pool_t *pool);
   

/* Set *TABLE_P to the entire property list of PATH in ROOT, as an APR
   hash table allocated in POOL.  The resulting table maps property
   names to pointers to svn_string_t objects containing the property
   value.  */
svn_error_t *svn_fs_node_proplist (apr_hash_t **table_p,
                                   svn_fs_root_t *root,
                                   const char *path,
                                   apr_pool_t *pool);


/* Change a node's property's value, or add/delete a property.
   - ROOT and PATH indicate the node whose property should change.
     ROOT must be the root of a transaction, not the root of a revision.
   - NAME is the name of the property to change.
   - VALUE is the new value of the property, or zero if the property should
     be removed altogether.
   Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs_change_node_prop (svn_fs_root_t *root,
                                      const char *path,
                                      const char *name,
                                      const svn_string_t *value,
                                      apr_pool_t *pool);


/* Set *CHANGED_P to 1 if the properties at PATH1 under ROOT1 differ
   from those at PATH2 under ROOT2, or set it to 0 if they are the
   same.  Both paths must exist under their respective roots, and both
   roots must be in the same filesystem.  */
svn_error_t *svn_fs_props_changed (int *changed_p,
                                   svn_fs_root_t *root1,
                                   const char *path1,
                                   svn_fs_root_t *root2,
                                   const char *path2,
                                   apr_pool_t *pool);


/* Discover a node's copy ancestry, if any.

   If the node at PATH in ROOT was copied from some other node, set
   *REV_P and *PATH_P to the revision and path of the other node,
   allocating *PATH_P in POOL.

   Else if there is no copy ancestry for the node, set *REV_P to
   SVN_INVALID_REVNUM and and *PATH_P to null.

   If an error is returned, the values of *REV_P and *PATH_P are
   undefined, but otherwise, if one of them is set as described above,
   you may assume the other is set correspondingly.

   ROOT may be a revision root or a transaction root.

   Notes:
      - Copy ancestry does not descend.  After copying directory D to
        E, E will have copy ancestry referring to D, but E's children
        may not.  See also svn_fs_copy().

      - Copy ancestry *under* a copy is preserved.  That is, if you
        copy /A/D/G/pi to /A/D/G/pi2, and then copy /A/D/G to /G, then
        /G/pi2 will still have copy ancestry pointing to /A/D/G/pi.
        We don't know if this is a feature or a bug yet; if it turns
        out to be a bug, then the fix is to make svn_fs_copied_from()
        observe the following logic, which currently callers may
        choose to follow themselves: if node X has copy history, but
        its ancestor A also has copy history, then you may ignore X's
        history if X's revision-of-origin is earlier than A's --
        because that would mean that X's copy history was preserved in
        a copy-under-a-copy scenario.  If X's revision-of-origin is
        the same as A's, then it was copied under A during the same
        transaction that created A.  (X's revision-of-origin cannot be
        greater than A's, if X has copy history.)  ### todo: See how
        people like this, it can always be hidden behind the curtain
        if necessary.

      - Copy ancestry is not stored as a regular subversion property
        because it is not inherited.  Copying foo to bar results in a
        revision of bar with copy ancestry; but committing a text
        change to bar right after that results in a new revision of
        bar without copy ancestry.  */
svn_error_t *svn_fs_copied_from (svn_revnum_t *rev_p,
                                 const char **path_p,
                                 svn_fs_root_t *root,
                                 const char *path,
                                 apr_pool_t *pool);


/* Given nodes SOURCE and TARGET, and a common ancestor ANCESTOR,
   modify TARGET to contain all the changes made between ANCESTOR and
   SOURCE, as well as the changes made between ANCESTOR and TARGET.
   TARGET_ROOT must be the root of a transaction, not a revision.

   SOURCE, TARGET, and ANCESTOR are generally directories; this
   function recursively merges the directories' contents.  If they are
   files, this function simply returns an error whenever SOURCE,
   TARGET, and ANCESTOR are all distinct node revisions.

   If there are differences between ANCESTOR and SOURCE that conflict
   with changes between ANCESTOR and TARGET, this function returns an
   SVN_ERR_FS_CONFLICT error.

   If the merge is successful, TARGET is left in the merged state, and
   the base root of TARGET's txn is set to the root node of SOURCE.
   If an error is returned (whether for conflict or otherwise), TARGET
   is left unaffected.

   If CONFLICT_P is non-null, then: a conflict error sets *CONFLICT_P
   to the name of the node in TARGET which couldn't be merged,
   otherwise, success sets *CONFLICT_P to null.

   Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs_merge (const char **conflict_p,
                           svn_fs_root_t *source_root,
                           const char *source_path,
                           svn_fs_root_t *target_root,
                           const char *target_path,
                           svn_fs_root_t *ancestor_root,
                           const char *ancestor_path,
                           apr_pool_t *pool);



/* Compare the nodes ROOT1:PATH1 and ROOT2:PATH2, and determine if
   they are "different".  Return the answer in IS_DIFFERENT.

   We define two nodes to be "different" if:

       - they are different node types, or

       - if both files, they have different node-revision-ids, or 
 
       - if both dirs, they have different entry lists.

   (Note that there is a small chance of getting a false positive: two
   different node-rev-ids don't *necessarily* have different contents.
   But right now it's not worth doing byte-for-byte comparisons.  This
   problem will go away when we have deltified storage.) */
svn_error_t *svn_fs_is_different (int *is_different,
                                  svn_fs_root_t *root1,
                                  const char *path1,
                                  svn_fs_root_t *root2,
                                  const char *path2,
                                  apr_pool_t *pool);



/* Deltification of Storage.  */


/* Examine the data associated with PATH under ROOT, and offer the
   filesystem a chance store that data in a deltified fashion.  ROOT
   is a revision root.

   If PATH represents a directory, deltify PATH's properties and list
   of entries, and if RECURSIVE is non-zero, perform this operation
   recursively on PATH's children.

   If PATH represents a file, deltify PATH's properties and text
   contents (and ignore the RECURSIVE argument).

   Use POOL for all necessary allocations. */
svn_error_t *svn_fs_deltify (svn_fs_root_t *root,
                             const char *path,
                             int recursive,
                             apr_pool_t *pool);


/* Ensure that the data associated with PATH under ROOT is stored as
   fulltext (that is, in an undeltified fashion).  If this is already
   the case, do nothing.  ROOT is a revision root.

   If PATH represents a directory, un-deltify PATH's properties and list
   of entries, and if RECURSIVE is non-zero, perform this operation
   recursively on PATH's children.

   If PATH represents a file, un-deltify PATH's properties and text
   contents (and ignore the RECURSIVE argument).

   Use POOL for all necessary allocations. */
svn_error_t *svn_fs_undeltify (svn_fs_root_t *root,
                               const char *path,
                               int recursive,
                               apr_pool_t *pool);



/* Directories.  */


/* The type of a Subversion directory entry.  */
typedef struct svn_fs_dirent_t {

  /* The name of this directory entry.  */
  char *name;

  /* The node revision ID it names.  */
  svn_fs_id_t *id;

} svn_fs_dirent_t;


/* Set *TABLE_P to a newly allocated APR hash table containing the
   entries of the directory at PATH in ROOT.  The keys of the table
   are entry names, as byte strings, excluding the final null
   character; the table's values are pointers to svn_fs_dirent_t
   structures.  Allocate the table and its contents in POOL.  */
svn_error_t *svn_fs_dir_entries (apr_hash_t **entries_p,
                                 svn_fs_root_t *root,
                                 const char *path,
                                 apr_pool_t *pool);


/* Create a new directory named PATH in ROOT.  The new directory has
   no entries, and no properties.  ROOT must be the root of a
   transaction, not a revision.

   Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs_make_dir (svn_fs_root_t *root,
                              const char *path,
                              apr_pool_t *pool);
                              

/* Delete the node named PATH in ROOT.  ROOT must be the root of a
   transaction, not a revision.  Do any necessary temporary allocation
   in POOL.

   If the node being deleted is a directory, it must be empty, else
   the error SVN_ERR_FS_DIR_NOT_EMPTY is returned.

   Attempting to remove the root dir also results in an error,
   SVN_ERR_FS_ROOT_DIR, even if the dir is empty.  */
svn_error_t *svn_fs_delete (svn_fs_root_t *root,
                            const char *path,
                            apr_pool_t *pool);


/* Delete the node named PATH in ROOT.  If the node being deleted is a
   directory, its contents will be deleted recursively.  ROOT must be
   the root of a transaction, not of a revision.  Use POOL for
   temporary allocation.

   This function may be more efficient than making the equivalent
   series of calls to svn_fs_delete, because it takes advantage of the
   fact that, to delete an immutable subtree, shared with some
   committed revision, you need only remove the directory entry.  The
   dumb algorithm would recurse into the subtree and end up cloning
   each non-empty directory it contains, only to delete it later.

   If return SVN_ERR_FS_NO_SUCH_ENTRY, then the basename of PATH is
   missing from its parent, that is, the final target of the deletion
   is missing.  */
svn_error_t *svn_fs_delete_tree (svn_fs_root_t *root,
                                 const char *path,
                                 apr_pool_t *pool);


/* Move the node named FROM to TO, both in ROOT.  ROOT must be the
   root of a transaction, not a revision.

   Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs_rename (svn_fs_root_t *root,
                            const char *from,
                            const char *to,
                            apr_pool_t *pool);


/* Create a copy of FROM_PATH in FROM_ROOT named TO_PATH in TO_ROOT.
   If FROM_PATH in FROM_ROOT is a directory, copy the tree it refers
   to recursively.

   The copy will remember its source; use svn_fs_copied_from() to
   access this information.

   TO_ROOT must be the root of a transaction; FROM_PATH must be the
   root of a revision.  (Requiring FROM_PATH to be the root of a
   revision makes the implementation trivial: there is no detectable
   difference (modulo node revision ID's) between copying FROM and
   simply adding a reference to it.  So the operation takes place in
   constant time.  However, there's no reason not to extend this to
   mutable nodes --- it's just more code.)

   Note: to do a copy without preserving copy history, use
   svn_fs_link().

   Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs_copy (svn_fs_root_t *from_root,
                          const char *from_path,
                          svn_fs_root_t *to_root,
                          const char *to_path,
                          apr_pool_t *pool);


/* Like svn_fs_copy(), but doesn't record copy history.  I.e., you
   cannot use svn_fs_copied_from() later to find out where this copy
   came from.

   Use svn_fs_link() in situations where you don't care about the copy
   history, because it is slightly cheaper than svn_fs_copy().  */
svn_error_t *svn_fs_link (svn_fs_root_t *from_root,
                          const char *from_path,
                          svn_fs_root_t *to_root,
                          const char *to_path,
                          apr_pool_t *pool);

/* Files.  */

/* Set *LENGTH_P to the length of the file PATH in ROOT, in bytes.  Do
   any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs_file_length (apr_off_t *length_p,
                                 svn_fs_root_t *root,
                                 const char *path,
                                 apr_pool_t *pool);


/* Set *CONTENTS to a readable generic stream will yield the contents
   of the file PATH in ROOT.  Allocate the stream in POOL.  You can
   only use *CONTENTS for as long as the underlying filesystem is
   open.  If PATH is not a file, return SVN_ERR_FS_NOT_FILE.

   If ROOT is the root of a transaction, it is possible that the
   contents of the file PATH will change between calls to
   svn_fs_file_contents().  In that case, the result of reading from
   *CONTENTS is undefined.  

   ### kff todo: I am worried about lifetime issues with this pool vs
   the trail created farther down the call stack.  Trace this function
   to investigate...  */
svn_error_t *svn_fs_file_contents (svn_stream_t **contents,
                                   svn_fs_root_t *root,
                                   const char *path,
                                   apr_pool_t *pool);


/* Create a new file named PATH in ROOT.  The file's initial contents
   are the empty string, and it has no properties.  ROOT must be the
   root of a transaction, not a revision.

   Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs_make_file (svn_fs_root_t *root,
                               const char *path,
                               apr_pool_t *pool);


/* Apply a text delta to the file PATH in ROOT.  ROOT must be the root
   of a transaction, not a revision.

   Set *CONTENTS_P to a function ready to receive text delta windows
   describing how to change the file's contents, relative to its
   current contents.  Set *CONTENTS_BATON_P to a baton to pass to
   *CONTENTS_P.

   If PATH does not exist in ROOT, return an error.  (You cannot use
   this routine to create new files;  use svn_fs_make_file to create
   an empty file first.)

   Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs_apply_textdelta (svn_txdelta_window_handler_t *contents_p,
                                     void **contents_baton_p,
                                     svn_fs_root_t *root,
                                     const char *path,
                                     apr_pool_t *pool);


/* Set *CHANGED_P to 1 if the contents at PATH1 under ROOT1 differ
   from those at PATH2 under ROOT2, or set it to 0 if they are the
   same.  Both paths must exist under their respective roots, and both
   roots must be in the same filesystem.  */
svn_error_t *svn_fs_contents_changed (int *changed_p,
                                      svn_fs_root_t *root1,
                                      const char *path1,
                                      svn_fs_root_t *root2,
                                      const char *path2,
                                      apr_pool_t *pool);



/* Filesystem revisions.  */


/* Set *YOUNGEST_P to the number of the youngest revision in filesystem FS.
   Use POOL for all temporary allocation.

   The oldest revision in any filesystem is numbered zero.  */
svn_error_t *svn_fs_youngest_rev (svn_revnum_t *youngest_p,
                                  svn_fs_t *fs,
                                  apr_pool_t *pool);


/* Set *VALUE_P to the value of the property named PROPNAME on
   revision REV in the filesystem FS.  If REV has no property by that
   name, set *VALUE_P to zero.  Allocate the result in POOL.  */
svn_error_t *svn_fs_revision_prop (svn_string_t **value_p,
                                   svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   const char *propname,
                                   apr_pool_t *pool);


/* Set *TABLE_P to the entire property list of revision REV in
   filesystem FS, as an APR hash table allocated in POOL.  The table
   maps char * property names to svn_string_t * values; the names
   and values are allocated in POOL.  */
svn_error_t *svn_fs_revision_proplist (apr_hash_t **table_p,
                                       svn_fs_t *fs,
                                       svn_revnum_t rev,
                                       apr_pool_t *pool);


/* Change a revision's property's value, or add/delete a property.

   - FS is a filesystem, and REV is the revision in that filesystem
     whose property should change.
   - NAME is the name of the property to change.
   - VALUE is the new value of the property, or zero if the property should
     be removed altogether.

   Note that revision properties are non-historied --- you can change
   them after the revision has been committed.  They are not protected
   via transactions.

   Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs_change_rev_prop (svn_fs_t *fs,
                                     svn_revnum_t rev,
                                     const char *name,
                                     const svn_string_t *value,
                                     apr_pool_t *pool);



/* Computing deltas.  */


/* Set *STREAM_P to a pointer to a delta stream that will turn the
   contents of the file SOURCE into the contents of the file TARGET.
   If SOURCE_ROOT is zero, use a file with zero length as the source.

   This function does not compare the two files' properties.

   Allocate *STREAM_P, and do any necessary temporary allocation, in
   POOL.  */
svn_error_t *
svn_fs_get_file_delta_stream (svn_txdelta_stream_t **stream_p,
                              svn_fs_root_t *source_root,
                              const char *source_path,
                              svn_fs_root_t *target_root,
                              const char *target_path,
                              apr_pool_t *pool);





/* Non-historical properties.  */

/* [[Yes, do tell.]] */

#endif /* SVN_FS_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
