/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file svn_fs.h
 * @brief Interface to the Subversion filesystem.
 */


#ifndef SVN_FS_H
#define SVN_FS_H

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_io.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Opening and creating filesystems.  */


/** An object representing a Subversion filesystem.  */
typedef struct svn_fs_t svn_fs_t;


/** Create a new filesystem object.
 *
 * Create a new filesystem object in @a pool.  It doesn't refer to any
 * actual repository yet; you need to invoke @c svn_fs_open_* or
 * @c svn_fs_create_* on it for that to happen.  
 *
 * NOTE: you probably don't want to use this directly, especially not
 * if it's followed immediately by a call to @c svn_fs_open_berkeley().
 * Take a look at @c svn_repos_open() instead.
 */
svn_fs_t *svn_fs_new (apr_pool_t *pool);


/** Free the filesystem object @a fs.
 *
 * Free the filesystem object @a fs.  This frees memory, closes files,
 * frees database library structures, etc.
 */
svn_error_t *svn_fs_close_fs (svn_fs_t *fs);


/** The type of a warning callback function.
 *
 * The type of a warning callback function.  @a baton is the value specified
 * in the call to @c svn_fs_set_warning_func; the filesystem passes it through
 * to the callback.  @a fmt is a printf-style format string, which tells us
 * how to interpret any successive arguments.  @a pool can be used for any
 * allocations, but the implemntation may decide to pass a pool around
 * in the baton.
 */
#ifndef SWIG
typedef void (*svn_fs_warning_callback_t) (apr_pool_t *pool, void *baton,
                                           const char *fmt, ...);
#endif


/** Provide a callback function, @a warning, that @a fs should use to report
 * warning messages.
 *
 * Provide a callback function, @a warning, that @a fs should use to report
 * warning messages.  To print a warning message, the filesystem will
 * call @a warning, passing it @a baton, a printf-style format string, and
 * any further arguments as appropriate for the format string.
 *
 * If it's acceptable to print messages on a standard stream, then the
 * function @c svn_handle_warning, declared in @c svn_error.h, would be
 * a suitable warning function.
 *
 * By default, this is set to a function that will crash the process.
 * Dumping to @c stderr or <tt>/dev/tty</tt> is not acceptable default 
 * behavior for server processes, since those may both be equivalent to
 * <tt>/dev/null</tt>.
 */
void svn_fs_set_warning_func (svn_fs_t *fs,
                              svn_fs_warning_callback_t warning,
                              void *warning_baton);



/** Subversion filesystems based on Berkeley DB.
 * 
 * There are many possible ways to implement the Subversion filesystem
 * interface.  You could implement it directly using ordinary POSIX
 * filesystem operations; you could build it using an SQL server as a
 * back end; you could build it on RCS; and so on.
 *
 * The functions on this page create filesystem objects that use
 * Berkeley DB (http://www.sleepycat.com) to store their data.
 * Berkeley DB supports transactions and recoverability, making it
 * well-suited for Subversion.
 *
 * A Berkeley DB ``environment'' is a Unix directory containing
 * database files, log files, backing files for shared memory buffers,
 * and so on --- everything necessary for a complex database
 * application.  Each Subversion filesystem lives in a single Berkeley
 * DB environment.
 *
 * @defgroup svn_fs_bdb berkeley bd filesystems
 * @{
 */

/** Create a new, empty Subversion filesystem.
 *
 * Create a new, empty Subversion filesystem, stored in a Berkeley DB
 * environment under @a path, a utf8-encoded path.  Make @a fs refer to 
 * this new filesystem.  @a fs provides the memory pool, warning function,
 * etc.  If @a path exists, it must be an empty directory.
 */
svn_error_t *svn_fs_create_berkeley (svn_fs_t *fs, const char *path);


/** Make @a fs refer to the Berkeley DB-based Subversion filesystem at
 * @a path.
 *
 * Make @a fs refer to the Berkeley DB-based Subversion filesystem at
 * @a path.  @a path is utf8-encoded, and must refer to a file or directory
 * created by @c svn_fs_create_berkeley.
 *
 * Only one thread may operate on any given filesystem object at once.
 * Two threads may access the same filesystem simultaneously only if
 * they open separate filesystem objects.  
 *
 * NOTE: you probably don't want to use this directly, especially not
 * if it's immediately preceded by a call to @c svn_fs_new().  Take a
 * look at @c svn_repos_open() instead.
 */
svn_error_t *svn_fs_open_berkeley (svn_fs_t *fs, const char *path);


/** Return the utf8-encoded path to @a fs's repository.
 *
 * Return the utf8-encoded path to @a fs's repository, allocated in
 * @a POOL.  Note: this is just what was passed to
 * @c svn_fs_create_berkeley() or @a svn_fs_open_berkeley() -- might be
 * absolute, might not.
 */
const char *svn_fs_berkeley_path (svn_fs_t *fs, apr_pool_t *pool);


/** Register an error handling function for Berkeley DB error messages.
 *
 * Register an error handling function for Berkeley DB error messages.
 * If a Berkeley DB error occurs, the filesystem will call @a handler
 * with two strings: an error message prefix, which will be zero, and
 * an error message.  @a handler should print it out, log it somewhere,
 * etc.
 *
 * Since Berkeley DB's error messages are sometimes much more
 * informative than the error codes the functions return, it's worth
 * calling this function and providing some kind of error message
 * handler.
 *
 * This function calls @c DBENV->set_errcall, with @a handler as the
 * @c db_errcall_fcn argument.
 */
svn_error_t *svn_fs_set_berkeley_errcall (svn_fs_t *fs, 
                                          void (*handler) (const char *errpfx,
                                                           char *msg));


/** Delete the Berkeley DB-based filesystem @a path.
 *
 * Delete the Berkeley DB-based filesystem @a path.  This deletes the
 * database files, log files, shared memory segments, etc.  @a path should
 * refer to a file or directory created by @c svn_fs_create_berkeley.
 */
svn_error_t *svn_fs_delete_berkeley (const char *PATH, apr_pool_t *pool);


/** Perform any necessary non-catastrophic recovery on a Berkeley
 * DB-based Subversion filesystem, stored in the environment @a path.
 *
 * Perform any necessary non-catastrophic recovery on a Berkeley
 * DB-based Subversion filesystem, stored in the environment @a path.
 * Do any necessary allocation within @a pool.
 *
 * After an unexpected server exit, due to a server crash or a system
 * crash, a Subversion filesystem based on Berkeley DB needs to run
 * recovery procedures to bring the database back into a consistent
 * state and release any locks that were held by the deceased process.
 * The recovery procedures require exclusive access to the database
 * --- while they execute, no other process or thread may access the
 * database.
 *
 * In a server with multiple worker processes, like Apache, if a
 * worker process accessing the filesystem dies, you must stop the
 * other worker processes, and run recovery.  Then, the other worker
 * processes can re-open the database and resume work.
 *
 * If the server exited cleanly, there is no need to run recovery, but
 * there is no harm in it, either, and it take very little time.  So
 * it's a fine idea to run recovery when the server process starts,
 * before it begins handling any requests.
 */
svn_error_t *svn_fs_berkeley_recover (const char *path,
                                      apr_pool_t *pool);

/** @} */


/** Filesystem Nodes.
 *
 * In a Subversion filesystem, a `node' corresponds roughly to an
 * `inode' in a Unix filesystem:
 * - A node is either a file or a directory.
 * - A node's contents change over time.
 * - When you change a node's contents, it's still the same node; it's
 *   just been changed.  So a node's identity isn't bound to a specific
 *   set of contents.
 * - If you rename a node, it's still the same node, just under a
 *   different name.  So a node's identity isn't bound to a particular
 *   filename.
 *
 * A `node revision' refers to a node's contents at a specific point in
 * time.  Changing a node's contents always creates a new revision of that
 * node.  Once created, a node revision's contents never change.
 *
 * When we create a node, its initial contents are the initial revision of
 * the node.  As users make changes to the node over time, we create new
 * revisions of that same node.  When a user commits a change that deletes
 * a file from the filesystem, we don't delete the node, or any revision
 * of it --- those stick around to allow us to recreate prior revisions of
 * the filesystem.  Instead, we just remove the reference to the node
 * from the directory.
 *
 * @defgroup svn_fs_nodes filesystem nodes
 * @{
 */

/** An object representing a node-id.  */
typedef struct svn_fs_id_t svn_fs_id_t;


/** Return -1, 0, or 1 if node revisions @a a and @a B are unrelated,
 * equivalent, or otherwise related (respectively).
 */
int svn_fs_compare_ids (const svn_fs_id_t *a, const svn_fs_id_t *b);



/** Return non-zero IFF the nodes associated with @a id1 and @a id2 are
 * related, else return zero.  
 *
 * NOTE: While this might seem redundent in the presence of
 * @c svn_fs_compare_ids (looking for a return value != -1), it is
 * slightly faster to run if the equality case is not interesting to
 * you.
 */
int svn_fs_check_related (const svn_fs_id_t *id1,
                          const svn_fs_id_t *id2);


/** Parse the @a len bytes at @a data as a node revision @a id.
 *
 * Parse the @a len bytes at @a data as a node revision @a id.  Return zero 
 * if the bytes are not a properly-formed @a id.  Allocate the parsed @a id 
 * in @a pool.
 */
svn_fs_id_t *svn_fs_parse_id (const char *data, 
                              apr_size_t len,
                              apr_pool_t *pool);


/** Return a Subversion string containing the unparsed form of the
 *  node or node revision id @a id.
 *
 * Return a Subversion string containing the unparsed form of the
 * node or node revision id @a id.  Allocate the string containing the
 * unparsed form in @a pool.
 */
svn_string_t *svn_fs_unparse_id (const svn_fs_id_t *id, 
                                 apr_pool_t *pool);

/** @} */


/** Filesystem Transactions.
 *
 * To make a change to a Subversion filesystem:
 * - Create a transaction object, using @c svn_fs_begin_txn.
 * - Call @c svn_fs_txn_root, to get the transaction's root directory.
 * - Make whatever changes you like in that tree.
 * - Commit the transaction, using @c svn_fs_commit_txn.
 *
 * The filesystem implementation guarantees that your commit will
 * either:
 * - succeed completely, so that all of the changes are committed to
 *   create a new revision of the filesystem, or
 * - fail completely, leaving the filesystem unchanged.
 *
 * Until you commit the transaction, any changes you make are
 * invisible.  Only when your commit succeeds do they become visible
 * to the outside world, as a new revision of the filesystem.
 *
 * If you begin a transaction, and then decide you don't want to make
 * the change after all (say, because your net connection with the
 * client disappeared before the change was complete), you can call
 * @c svn_fs_abort_txn, to cancel the entire transaction; this
 * leaves the filesystem unchanged.
 *
 * The only way to change the contents of files or directories, or
 * their properties, is by making a transaction and creating a new
 * revision, as described above.  Once a revision has been committed, it
 * never changes again; the filesystem interface provides no means to
 * go back and edit the contents of an old revision.  Once history has
 * been recorded, it is set in stone.  Clients depend on this property
 * to do updates and commits reliably; proxies depend on this property
 * to cache changes accurately; and so on.
 *
 * There are two kinds of nodes in the filesystem: mutable, and
 * immutable.  Revisions in the filesystem consist entirely of
 * immutable nodes, whose contents never change.  A transaction in
 * progress, which the user is still constructing, uses mutable nodes
 * for those nodes which have been changed so far, and refers to
 * immutable nodes from existing revisions for portions of the tree
 * which haven't been changed yet in that transaction.
 *
 * Immutable nodes, as part of revisions, never refer to mutable
 * nodes, which are part of uncommitted transactions.  Mutable nodes
 * may refer to immutable nodes, or other mutable nodes.
 *
 * Note that the terms "immutable" and "mutable" describe whether or
 * not the nodes have been changed as part of a transaction --- not
 * the permissions on the nodes they refer to.  Even if you aren't
 * authorized to modify the filesystem's root directory, you might be
 * authorized to change some descendant of the root; doing so would
 * create a new mutable copy of the root directory.  Mutability refers
 * to the role of the node: part of an existing revision, or part of a
 * new one.  This is independent of your authorization to make changes
 * to a given node.
 *
 * Transactions are actually persistent objects, stored in the
 * database.  You can open a filesystem, begin a transaction, and
 * close the filesystem, and then a separate process could open the
 * filesystem, pick up the same transaction, and continue work on it.
 * When a transaction is successfully committed, it is removed from
 * the database.
 *
 * Every transaction is assigned a name.  You can open a transaction
 * by name, and resume work on it, or find out the name of a
 * transaction you already have open.  You can also list all the
 * transactions currently present in the database.
 *
 * Transaction names are guaranteed to contain only letters (upper-
 * and lower-case), digits, `-', and `.', from the ASCII character
 * set.
 *
 * @defgroup svn_fs_txns filesystem transactions
 * @{
 */

/** The type of a Subversion transaction object.  */
typedef struct svn_fs_txn_t svn_fs_txn_t;


/** Begin a new transaction on the filesystem @a fs, based on existing
 * revision @a rev.
 *
 * Begin a new transaction on the filesystem @a fs, based on existing
 * revision @a rev.  Set @a *txn_p to a pointer to the new transaction.
 * When committed, this transaction will create a new revision.
 *
 * Allocate the new transaction in @a pool; when @a pool is freed, the new
 * transaction will be closed (neither committed nor aborted).  You
 * can also close the transaction explicitly, using
 * @c svn_fs_close_txn.
 *
 *<pre>   >> Note: if you're building a txn for committing, you probably <<
 *   >> don't want to call this directly.  Instead, call            <<
 *   >> @c svn_repos_fs_begin_txn_for_commit(), which honors the       <<
 *   >> repository's hook configurations.                           <<</pre>
 */
svn_error_t *svn_fs_begin_txn (svn_fs_txn_t **txn_p,
                               svn_fs_t *fs,
                               svn_revnum_t rev,
                               apr_pool_t *pool);


/** Commit @a txn.
 *
 * Commit @a txn.
 *
 *<pre>   >> Note: you usually don't want to call this directly.        <<
 *   >> Instead, call @c svn_repos_fs_commit_txn(), which honors the  <<
 *   >> repository's hook configurations.                          <<</pre>
 *
 * If the transaction conflicts with other changes committed to the
 * repository, return an @c SVN_ERR_FS_CONFLICT error.  Otherwise, create
 * a new filesystem revision containing the changes made in @a txn,
 * storing that new revision number in @a *new_rev, and return zero.
 *
 * If @a conflict_p is non-zero, use it to provide details on any
 * conflicts encountered merging @a txn with the most recent committed
 * revisions.  If a conflict occurs, set @a *conflict_p to the path of
 * the conflict in @a txn.  Otherwise, set @a *conflict_p to null.
 *
 * If the commit succeeds, it frees @a txn, and any temporary resources
 * it holds.  Any root objects (see below) referring to the root
 * directory of @a txn become invalid; performing any operation on them
 * other than closing them will produce an @c SVN_ERR_FS_DEAD_TRANSACTION
 * error.
 *
 * If the commit fails, @a txn is still valid; you can make more
 * operations to resolve the conflict, or call @c svn_fs_abort_txn to
 * abort the transaction.
 *
 * NOTE:  Success or failure of the commit of @a txn is determined by
 * examining the value of @a *new_rev upon this function's return.  If
 * the value is a valid revision number, the commit was successful,
 * even though a non-@c NULL function return value may indicate that
 * something else went wrong.
 */
svn_error_t *svn_fs_commit_txn (const char **conflict_p,
                                svn_revnum_t *new_rev,
                                svn_fs_txn_t *txn);


/** Abort the transaction @a txn.
 *
 * Abort the transaction @a txn.  Any changes made in @a txn are discarded,
 * and the filesystem is left unchanged.
 *
 * If the abort succeeds, it frees @a txn, and any temporary resources
 * it holds.  Any root objects referring to @a txn's root directory
 * become invalid; performing any operation on them other than closing
 * them will produce an @c SVN_ERR_FS_DEAD_TRANSACTION error.
 */
svn_error_t *svn_fs_abort_txn (svn_fs_txn_t *txn);


/** Set @a *name_p to the name of the transaction @a txn, as a
 * null-terminated string.
 *
 * Set @a *name_p to the name of the transaction @a txn, as a
 * null-terminated string.  Allocate the name in @a pool.
 */
svn_error_t *svn_fs_txn_name (const char **name_p,
                              svn_fs_txn_t *txn,
                              apr_pool_t *pool);


/** Return the filesystem to which @a txn belongs.  */
svn_fs_t *svn_fs_txn_fs (svn_fs_txn_t *txn);


/** Return @a txn's pool.  */
apr_pool_t *svn_fs_txn_pool (svn_fs_txn_t *txn);


/** Return @a txn's base revision.
 *
 * Return @a txn's base revision.  If @a txn's base root id is an mutable
 * node, return 0.
 */
svn_revnum_t svn_fs_txn_base_revision (svn_fs_txn_t *txn);



/** Open the transaction named @a name in the filesystem @a fs.
 *
 * Open the transaction named @a name in the filesystem @a fs.  Set @a *txn 
 * to the transaction.
 *
 * If there is no such transaction, @c SVN_ERR_FS_NO_SUCH_TRANSACTION is
 * the error returned.
 *
 * Allocate the new transaction in @a pool; when @a pool is freed, the new
 * transaction will be closed (neither committed nor aborted).  You
 * can also close the transaction explicitly, using
 * @c svn_fs_close_txn.
 */
svn_error_t *svn_fs_open_txn (svn_fs_txn_t **txn,
                              svn_fs_t *fs,
                              const char *name,
                              apr_pool_t *pool);


/** Close the transaction @a txn.
 *
 * Close the transaction @a txn.  This is neither an abort nor a commit;
 * the state of the transaction so far is stored in the filesystem, to
 * be opened again later.
 */
svn_error_t *svn_fs_close_txn (svn_fs_txn_t *txn);


/** Set @a *names_p to an array of <tt>const char *</tt> @a ids which are the 
 * names of all the currently active transactions in the filesystem @a fs.
 *
 * Set @a *names_p to an array of <tt>const char *</tt> @a ids which are the 
 * names of all the currently active transactions in the filesystem @a fs.
 * Allocate the array in @a pool.
 */
svn_error_t *svn_fs_list_transactions (apr_array_header_t **names_p,
                                       svn_fs_t *fs,
                                       apr_pool_t *pool);

/* Transaction properties */

/** Set @a *value_p to the value of the property named @a propname on
 * transaction @a txn.
 *
 * Set @a *value_p to the value of the property named @a propname on
 * transaction @a txn.  If @a txn has no property by that name, set 
 * @a *value_p to zero.  Allocate the result in @a pool.
 */
svn_error_t *svn_fs_txn_prop (svn_string_t **value_p,
                              svn_fs_txn_t *txn,
                              const char *propname,
                              apr_pool_t *pool);


/** Set @a *table_p to the entire property list of transaction @a txn in
 * filesystem @a fs, as an APR hash table allocated in @a pool.
 *
 * Set @a *table_p to the entire property list of transaction @a txn in
 * filesystem @a fs, as an APR hash table allocated in @a pool.  The
 * resulting table maps property names to pointers to @c svn_string_t
 * objects containing the property value.
 */
svn_error_t *svn_fs_txn_proplist (apr_hash_t **table_p,
                                  svn_fs_txn_t *txn,
                                  apr_pool_t *pool);


/** Change a tranactions @a txn's property's value, or add/delete a
 * property.
 *
 * Change a tranactions @a txn's property's value, or add/delete a
 * property.  @a name is the name of the property to change, and @a value 
 * is the new value of the property, or zero if the property should be
 * removed altogether.  Do any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_change_txn_prop (svn_fs_txn_t *txn,
                                     const char *name,
                                     const svn_string_t *value,
                                     apr_pool_t *pool);

/** @} */


/** Roots.
 *
 * An @c svn_fs_root_t object represents the root directory of some
 * revision or transaction in a filesystem.  To refer to particular
 * node, you provide a root, and a directory path relative that root.
 *
 * @defgroup svn_fs_roots filesystem roots
 * @{
 */

/** The Filesystem Root object. */
typedef struct svn_fs_root_t svn_fs_root_t;


/** Set @a *root_p to the root directory of revision @a rev in filesystem 
 * @a fs.  Allocate @a *root_p in @a pool.
 */
svn_error_t *svn_fs_revision_root (svn_fs_root_t **root_p,
                                   svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   apr_pool_t *pool);


/** Set @a *root_p to the root directory of @a txn.  Allocate @a *root_p in 
 * @a pool.
 */
svn_error_t *svn_fs_txn_root (svn_fs_root_t **root_p,
                              svn_fs_txn_t *txn,
                              apr_pool_t *pool);


/** Free the root directory @a root.
 *
 * Free the root directory @a root.  Simply clearing or destroying the
 * pool @a root was allocated in will have the same effect as calling
 * this function.
 */
void svn_fs_close_root (svn_fs_root_t *root);


/** Return the filesystem to which @a root belongs.  */
svn_fs_t *svn_fs_root_fs (svn_fs_root_t *root);


/** Return true iff @a root is a transaction root.  */
int svn_fs_is_txn_root      (svn_fs_root_t *root);

/** Return true iff @a root is a revision root.  */
int svn_fs_is_revision_root (svn_fs_root_t *root);


/** If @a root is the root of a transaction, return a pointer to the name
 * of the transaction; otherwise, return zero.
 *
 * If @a root is the root of a transaction, return a pointer to the name
 * of the transaction; otherwise, return zero.  The name is owned by
 * @a root, and will be freed when @a root is closed.
 */
const char *svn_fs_txn_root_name (svn_fs_root_t *root,
                                  apr_pool_t *pool);


/** If @a root is the root of a revision, return the revision number.
 *
 * If @a root is the root of a revision, return the revision number.
 * Otherwise, return @c SVN_INVALID_REVNUM.
 */
svn_revnum_t svn_fs_revision_root_revision (svn_fs_root_t *root);

/** @} */


/** Directory entry names and directory paths.
 *
 * Here are the rules for directory entry names, and directory paths:
 *
 * A directory entry name is a Unicode string encoded in UTF-8, and
 * may not contain the null character (U+0000).  The name should be in
 * Unicode canonical decomposition and ordering.  No directory entry
 * may be named '.', '..', or the empty string.  Given a directory
 * entry name which fails to meet these requirements, a filesystem
 * function returns an SVN_ERR_FS_PATH_SYNTAX error.
 *
 * A directory path is a sequence of zero or more directory entry
 * names, separated by slash characters (U+002f), and possibly ending
 * with slash characters.  Sequences of two or more consecutive slash
 * characters are treated as if they were a single slash.  If a path
 * ends with a slash, it refers to the same node it would without the
 * slash, but that node must be a directory, or else the function
 * returns an SVN_ERR_FS_NOT_DIRECTORY error.
 *
 * A path consisting of the empty string, or a string containing only
 * slashes, refers to the root directory.
 *
 * @defgroup svn_fs_directories filesystem directories
 * @{
 */



/** The kind of change that occured on the path. */
typedef enum
{
  /** default value */
  svn_fs_path_change_modify = 0,

  /** path added in txn */
  svn_fs_path_change_add,

  /** path removed in txn */
  svn_fs_path_change_delete,

  /** path removed and re-added in txn */
  svn_fs_path_change_replace,

  /** ignore all previous change items for path (internal-use only) */
  svn_fs_path_change_reset

} svn_fs_path_change_kind_t;

/** Change descriptor. */
typedef struct svn_fs_path_change_t
{
  /** node revision id of changed path */
  const svn_fs_id_t *node_rev_id;

  /** kind of change (see above) */
  svn_fs_path_change_kind_t change_kind;

  /** were there text mods? */
  int text_mod;

  /** were there property mods? */
  int prop_mod;

} svn_fs_path_change_t;


/** Determine what has changed under a @a root.
 *
 * Allocate and return a hash @a *changed_paths_p containing descriptions
 * of the paths changed under @a root.  The hash is keyed with 
 * <tt>const char *</tt> paths, and has @c svn_fs_path_change_t * values.  
 * Use @c pool for all allocations, including the hash and its values.
 */
svn_error_t *svn_fs_paths_changed (apr_hash_t **changed_paths_p,
                                   svn_fs_root_t *root,
                                   apr_pool_t *pool);

/** @} */


/* Operations appropriate to all kinds of nodes.  */

/** Return the type of node present at @a path under @a root.
 *
 * Return the type of node present at @a path under @a root.  If @a path
 * does not exist under @a root, set @a *kind to @c svn_node_none.
 */
svn_node_kind_t svn_fs_check_path (svn_fs_root_t *root,
                                   const char *path,
                                   apr_pool_t *pool);


/** Allocate and return an array @a *revs of @c svn_revnum_t revisions in
 * which @a paths under @a root were modified.
 *
 * Allocate and return an array @a *revs of @c svn_revnum_t revisions in
 * which @a paths under @a root were modified.  Use @a pool for all 
 * allocations.  The array of @a *revs are sorted in descending order.
 * All duplicates will also be removed.  @a paths is an array of 
 * <tt>const char *<tt> entries.
 * 
 * If @a cross_copy_history is not set, this function will halt the
 * search for revisions in which a given path was changed when it
 * detects that the path was copied.
 *
 * NOTE: This function uses node-id ancestry alone to determine
 * modifiedness, and therefore does NOT claim that in any of the
 * returned revisions file contents changed, properties changed,
 * directory entries lists changed, etc.  
 *
 * ALSO NOTE: The revisions returned for a given path will be older
 * than or the same age as the revision of that path in @a root.  That
 * is, if @a root is a revision root based on revision X, and a path was
 * modified in some revision(s) younger than X, those revisions
 * younger than X will not be included for that path.
 */
svn_error_t *svn_fs_revisions_changed (apr_array_header_t **revs,
                                       svn_fs_root_t *root,
                                       const apr_array_header_t *paths,
                                       int cross_copy_history,
                                       apr_pool_t *pool);


/** Set @a *is_dir to non-zero iff @a path in @a root is a directory.
 *
 * Set @a *is_dir to non-zero iff @a path in @a root is a directory.
 * Do any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_is_dir (int *is_dir,
                            svn_fs_root_t *root,
                            const char *path,
                            apr_pool_t *pool);


/** Set @a *is_file to non-zero iff @a path in @a root is a file.
 *
 * Set @a *is_file to non-zero iff @a path in @a root is a file.
 * Do any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_is_file (int *is_file,
                             svn_fs_root_t *root,
                             const char *path,
                             apr_pool_t *pool);


/** Get the id of a node.
 *
 * Set @a *id_p to the node revision ID of @a path in @a root, allocated in 
 * @a pool.
 *
 * If @a root is the root of a transaction, keep in mind that other
 * changes to the transaction can change which node @a path refers to,
 * and even whether the path exists at all.
 */
svn_error_t *svn_fs_node_id (const svn_fs_id_t **id_p,
                             svn_fs_root_t *root,
                             const char *path,
                             apr_pool_t *pool);

/** Set @a *revision to the revision in which @a path under @a root was 
 * created.
 *
 * Set @a *revision to the revision in which @a path under @a root was 
 * created.  Use @a pool for any temporary allocations.  @a *revision will 
 * be set to @c SVN_INVALID_REVNUM for uncommitted nodes (i.e. modified nodes 
 * under a transaction root).
 */
svn_error_t *svn_fs_node_created_rev (svn_revnum_t *revision,
                                      svn_fs_root_t *root,
                                      const char *path,
                                      apr_pool_t *pool);

/** Set @a *value_p to the value of the property named @a propname of 
 * @a path in @a root.
 *
 * Set @a *value_p to the value of the property named @a propname of 
 * @a path in @a root.  If the node has no property by that name, set 
 * @a *value_p to zero.  Allocate the result in @a pool.
 */
svn_error_t *svn_fs_node_prop (svn_string_t **value_p,
                               svn_fs_root_t *root,
                               const char *path,
                               const char *propname,
                               apr_pool_t *pool);
   

/** Set @a *table_p to the entire property list of @a path in @a root, 
 * as an APR hash table allocated in @a pool.
 *
 * Set @a *table_p to the entire property list of @a path in @a root, 
 * as an APR hash table allocated in @a pool.  The resulting table maps 
 * property names to pointers to @c svn_string_t objects containing the 
 * property value.
 */
svn_error_t *svn_fs_node_proplist (apr_hash_t **table_p,
                                   svn_fs_root_t *root,
                                   const char *path,
                                   apr_pool_t *pool);


/** Change a node's property's value, or add/delete a property.
 *
 * Change a node's property's value, or add/delete a property.
 * - @a root and @a path indicate the node whose property should change.
 *   @a root must be the root of a transaction, not the root of a revision.
 * - @a name is the name of the property to change.
 * - @a value is the new value of the property, or zero if the property should
 *   be removed altogether.
 * Do any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_change_node_prop (svn_fs_root_t *root,
                                      const char *path,
                                      const char *name,
                                      const svn_string_t *value,
                                      apr_pool_t *pool);


/** Determine if the properties of two path/root combinations are different.
 *
 * Set @a *changed_p to 1 if the properties at @a path1 under @a root1 differ
 * from those at @a path2 under @a root2, or set it to 0 if they are the
 * same.  Both paths must exist under their respective roots, and both
 * roots must be in the same filesystem.
 */
svn_error_t *svn_fs_props_changed (int *changed_p,
                                   svn_fs_root_t *root1,
                                   const char *path1,
                                   svn_fs_root_t *root2,
                                   const char *path2,
                                   apr_pool_t *pool);


/** Discover a node's copy ancestry, if any.
 *
 * Discover a node's copy ancestry, if any.
 *
 * If the node at @a path in @a root was copied from some other node, set
 * @a *rev_p and @a *path_p to the revision and path of the other node,
 * allocating @a *path_p in @a pool.
 *
 * Else if there is no copy ancestry for the node, set @a *rev_p to
 * @c SVN_INVALID_REVNUM and @a *path_p to null.
 *
 * If an error is returned, the values of @a *rev_p and @a *path_p are
 * undefined, but otherwise, if one of them is set as described above,
 * you may assume the other is set correspondingly.
 *
 * @a root may be a revision root or a transaction root.
 *
 * Notes:
 *    - Copy ancestry does not descend.  After copying directory D to
 *      E, E will have copy ancestry referring to D, but E's children
 *      may not.  See also @c svn_fs_copy().
 *
 *    - Copy ancestry *under* a copy is preserved.  That is, if you
 *      copy /A/D/G/pi to /A/D/G/pi2, and then copy /A/D/G to /G, then
 *      /G/pi2 will still have copy ancestry pointing to /A/D/G/pi.
 *      We don't know if this is a feature or a bug yet; if it turns
 *      out to be a bug, then the fix is to make @c svn_fs_copied_from()
 *      observe the following logic, which currently callers may
 *      choose to follow themselves: if node X has copy history, but
 *      its ancestor A also has copy history, then you may ignore X's
 *      history if X's revision-of-origin is earlier than A's --
 *      because that would mean that X's copy history was preserved in
 *      a copy-under-a-copy scenario.  If X's revision-of-origin is
 *      the same as A's, then it was copied under A during the same
 *      transaction that created A.  (X's revision-of-origin cannot be
 *      greater than A's, if X has copy history.)  ### todo: See how
 *      people like this, it can always be hidden behind the curtain
 *      if necessary.
 *
 *    - Copy ancestry is not stored as a regular subversion property
 *      because it is not inherited.  Copying foo to bar results in a
 *      revision of bar with copy ancestry; but committing a text
 *      change to bar right after that results in a new revision of
 *      bar without copy ancestry.
 */
svn_error_t *svn_fs_copied_from (svn_revnum_t *rev_p,
                                 const char **path_p,
                                 svn_fs_root_t *root,
                                 const char *path,
                                 apr_pool_t *pool);


/** Merge changes between two nodes into a third node.
 *
 * Given nodes @a source and @a target, and a common ancestor @a ancestor,
 * modify @a target to contain all the changes made between @a ancestor and
 * @a source, as well as the changes made between @a ancestor and @a target.
 * @a target_root must be the root of a transaction, not a revision.
 *
 * @a source, @a target, and @a ancestor are generally directories; this
 * function recursively merges the directories' contents.  If they are
 * files, this function simply returns an error whenever @a source,
 * @a target, and @a ancestor are all distinct node revisions.
 *
 * If there are differences between @a ancestor and @a source that conflict
 * with changes between @a ancestor and @a target, this function returns an
 * @c SVN_ERR_FS_CONFLICT error.
 *
 * If the merge is successful, @a target is left in the merged state, and
 * the base root of @a target's txn is set to the root node of @a source.
 * If an error is returned (whether for conflict or otherwise), @a target
 * is left unaffected.
 *
 * If @a conflict_p is non-null, then: a conflict error sets @a *conflict_p
 * to the name of the node in @a target which couldn't be merged,
 * otherwise, success sets @a *conflict_p to null.
 *
 * Do any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_merge (const char **conflict_p,
                           svn_fs_root_t *source_root,
                           const char *source_path,
                           svn_fs_root_t *target_root,
                           const char *target_path,
                           svn_fs_root_t *ancestor_root,
                           const char *ancestor_path,
                           apr_pool_t *pool);



/** Compare the nodes @a root1:path1 and @a root2:path2, and determine 
 * if they are "different".
 *
 * Compare the nodes @a root1:path1 and @a root2:path2, and determine if
 * they are "different".  Return the answer in @a is_different.
 *
 * We define two nodes to be "different" if:
 *
 *     - they are different node types, or
 *
 *     - if both files, they have different node-revision-ids, or 
 * 
 *     - if both dirs, they have different entry lists.
 *
 * (Note that there is a small chance of getting a false positive: two
 * different node-rev-ids don't *necessarily* have different contents.
 * But right now it's not worth doing byte-for-byte comparisons.  This
 * problem will go away when we have deltified storage.)
 */
svn_error_t *svn_fs_is_different (int *is_different,
                                  svn_fs_root_t *root1,
                                  const char *path1,
                                  svn_fs_root_t *root2,
                                  const char *path2,
                                  apr_pool_t *pool);



/* Deltification of Storage.  */


/** Examine the data associated with @a path under @a root, and offer the
 * filesystem a chance store that data in a deltified fashion.
 *
 * Examine the data associated with @a path under @a root, and offer the
 * filesystem a chance store that data in a deltified fashion.  @a root
 * is a revision root.
 *
 * If @a path represents a directory, deltify @a path's properties and list
 * of entries, and if @a recursive is non-zero, perform this operation
 * recursively on @a path's children.
 *
 * If @a path represents a file, deltify @a path's properties and text
 * contents (and ignore the @a recursive argument).
 *
 * Use @a pool for all necessary allocations.
 */
svn_error_t *svn_fs_deltify (svn_fs_root_t *root,
                             const char *path,
                             int recursive,
                             apr_pool_t *pool);


/** Ensure that the data associated with @a path under @a root is stored as
 * fulltext (that is, in an undeltified fashion).
 *
 * Ensure that the data associated with @a path under @a root is stored as
 * fulltext (that is, in an undeltified fashion).  If this is already
 * the case, do nothing.  @a root is a revision root.
 *
 * If @a path represents a directory, un-deltify @a path's properties and 
 * list of entries, and if @a recursive is non-zero, perform this operation
 * recursively on @a path's children.
 *
 * If @a path represents a file, un-deltify @a path's properties and text
 * contents (and ignore the @a recursive argument).
 *
 * Use @a pool for all necessary allocations.
 */
svn_error_t *svn_fs_undeltify (svn_fs_root_t *root,
                               const char *path,
                               int recursive,
                               apr_pool_t *pool);



/* Directories.  */


/** The type of a Subversion directory entry.  */
typedef struct svn_fs_dirent_t {

  /** The name of this directory entry.  */
  char *name;

  /** The node revision ID it names.  */
  svn_fs_id_t *id;

} svn_fs_dirent_t;


/** Set @a *table_p to a newly allocated APR hash table containing the
 * entries of the directory at @a path in @a root.
 *
 * Set @a *table_p to a newly allocated APR hash table containing the
 * entries of the directory at @a path in @a root.  The keys of the table
 * are entry names, as byte strings, excluding the final null
 * character; the table's values are pointers to @c svn_fs_dirent_t
 * structures.  Allocate the table and its contents in @a pool.
 */
svn_error_t *svn_fs_dir_entries (apr_hash_t **entries_p,
                                 svn_fs_root_t *root,
                                 const char *path,
                                 apr_pool_t *pool);


/** Create a new directory named @a path in @a root.
 *
 * Create a new directory named @a path in @a root.  The new directory has
 * no entries, and no properties.  @a root must be the root of a
 * transaction, not a revision.
 *
 * Do any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_make_dir (svn_fs_root_t *root,
                              const char *path,
                              apr_pool_t *pool);
                              

/** Delete the node named @a path in @a root.
 *
 * Delete the node named @a path in @a root.  @a root must be the root of a
 * transaction, not a revision.  Do any necessary temporary allocation
 * in @a pool.
 *
 * If the node being deleted is a directory, it must be empty, else
 * the error @c SVN_ERR_DIR_NOT_EMPTY is returned.
 *
 * Attempting to remove the root dir also results in an error,
 * @c SVN_ERR_FS_ROOT_DIR, even if the dir is empty.
 */
svn_error_t *svn_fs_delete (svn_fs_root_t *root,
                            const char *path,
                            apr_pool_t *pool);


/** Delete the node named @a path in @a root.
 *
 * Delete the node named @a path in @a root.  If the node being deleted is a
 * directory, its contents will be deleted recursively.  @a root must be
 * the root of a transaction, not of a revision.  Use @a pool for
 * temporary allocation.
 *
 * This function may be more efficient than making the equivalent
 * series of calls to @c svn_fs_delete, because it takes advantage of the
 * fact that, to delete an immutable subtree, shared with some
 * committed revision, you need only remove the directory entry.  The
 * dumb algorithm would recurse into the subtree and end up cloning
 * each non-empty directory it contains, only to delete it later.
 *
 * If return @c SVN_ERR_FS_NO_SUCH_ENTRY, then the basename of @a path is
 * missing from its parent, that is, the final target of the deletion
 * is missing.
 */
svn_error_t *svn_fs_delete_tree (svn_fs_root_t *root,
                                 const char *path,
                                 apr_pool_t *pool);


/** Move the node named @a from to @a to, both in @a root.
 *
 * Move the node named @a from to @a to, both in @a root.  @a root must be the
 * root of a transaction, not a revision.
 *
 * Do any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_rename (svn_fs_root_t *root,
                            const char *from,
                            const char *to,
                            apr_pool_t *pool);


/** Create a copy of @a from_path in @a from_root named @a to_path in 
 * @a to_root.
 *
 * Create a copy of @a from_path in @a from_root named @a to_path in 
 * @a to_root.  If @a from_path in @a from_root is a directory, copy the 
 * tree it refers to recursively.
 *
 * The copy will remember its source; use @c svn_fs_copied_from() to
 * access this information.
 *
 * @a to_root must be the root of a transaction; @a from_path must be the
 * root of a revision.  (Requiring @a from_path to be the root of a
 * revision makes the implementation trivial: there is no detectable
 * difference (modulo node revision ID's) between copying @a from and
 * simply adding a reference to it.  So the operation takes place in
 * constant time.  However, there's no reason not to extend this to
 * mutable nodes --- it's just more code.)
 *
 * Note: to do a copy without preserving copy history, use
 * @c svn_fs_link().
 *
 * Do any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_copy (svn_fs_root_t *from_root,
                          const char *from_path,
                          svn_fs_root_t *to_root,
                          const char *to_path,
                          apr_pool_t *pool);


/** Like @c svn_fs_copy(), but doesn't record copy history, and preserves
 * the @a path.
 *
 * Like @c svn_fs_copy(), but doesn't record copy history, and preserves
 * the PATH.  You cannot use @c svn_fs_copied_from() later to find out
 * where this copy came from.
 *
 * Use @c svn_fs_link() in situations where you don't care about the copy
 * history, and where @a to_path and @a from_path are the same, because it
 * is cheaper than @c svn_fs_copy().
 */
svn_error_t *svn_fs_revision_link (svn_fs_root_t *from_root,
                                   svn_fs_root_t *to_root,
                                   const char *path,
                                   apr_pool_t *pool);

/* Files.  */

/** Set @a *length_p to the length of the file @a path in @a root, in bytes.
 *
 * Set @a *length_p to the length of the file @a path in @a root, in bytes.  Do
 * any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_file_length (apr_off_t *length_p,
                                 svn_fs_root_t *root,
                                 const char *path,
                                 apr_pool_t *pool);


/** Set @a *contents to a readable generic stream will yield the contents
 *  of the file @a path in @a root.
 *
 * Set @a *contents to a readable generic stream will yield the contents
 * of the file @a path in @a root.  Allocate the stream in @a pool.  You can
 * only use @a *contents for as long as the underlying filesystem is
 * open.  If @a path is not a file, return @c SVN_ERR_FS_NOT_FILE.
 *
 * If @a root is the root of a transaction, it is possible that the
 * contents of the file @a path will change between calls to
 * @c svn_fs_file_contents().  In that case, the result of reading from
 * @a *contents is undefined.  
 *
 * ### kff todo: I am worried about lifetime issues with this pool vs
 * the trail created farther down the call stack.  Trace this function
 * to investigate...
 */
svn_error_t *svn_fs_file_contents (svn_stream_t **contents,
                                   svn_fs_root_t *root,
                                   const char *path,
                                   apr_pool_t *pool);


/** Create a new file named @a path in @a root.
 *
 * Create a new file named @a path in @a root.  The file's initial contents
 * are the empty string, and it has no properties.  @a root must be the
 * root of a transaction, not a revision.
 *
 * Do any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_make_file (svn_fs_root_t *root,
                               const char *path,
                               apr_pool_t *pool);


/** Apply a text delta to the file @a path in @a root.
 *
 * Apply a text delta to the file @a path in @a root.  @a root must be the 
 * root of a transaction, not a revision.
 *
 * Set @a *contents_p to a function ready to receive text delta windows
 * describing how to change the file's contents, relative to its
 * current contents.  Set @a *contents_baton_p to a baton to pass to
 * @a *contents_p.
 *
 * If @a path does not exist in @a root, return an error.  (You cannot use
 * this routine to create new files;  use @c svn_fs_make_file to create
 * an empty file first.)
 *
 * Do any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_apply_textdelta (svn_txdelta_window_handler_t *contents_p,
                                     void **contents_baton_p,
                                     svn_fs_root_t *root,
                                     const char *path,
                                     apr_pool_t *pool);


/** Write data directly to the file @a path in @a root.
 *
 * Write data directly to the file @a path in @a root.  @a root must be the
 * root of a transaction, not a revision.
 *
 * Set @a *contents_p to a stream ready to receive full textual data.
 * When the caller closes this stream, the data replaces the previous
 * contents of the file.
 *
 * If @a path does not exist in @a root, return an error.  (You cannot use
 * this routine to create new files;  use @c svn_fs_make_file to create
 * an empty file first.)
 *
 * Do any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_apply_text (svn_stream_t **contents_p,
                                svn_fs_root_t *root,
                                const char *path,
                                apr_pool_t *pool);


/** Check if the contents of two root/path combos have changed.
 *
 * Set @a *changed_p to 1 if the contents at @a path1 under @a root1 differ
 * from those at @a path2 under @a root2, or set it to 0 if they are the
 * same.  Both paths must exist under their respective roots, and both
 * roots must be in the same filesystem. 
 */
svn_error_t *svn_fs_contents_changed (int *changed_p,
                                      svn_fs_root_t *root1,
                                      const char *path1,
                                      svn_fs_root_t *root2,
                                      const char *path2,
                                      apr_pool_t *pool);



/* Filesystem revisions.  */


/** Set @a *youngest_p to the number of the youngest revision in filesystem 
 * @a fs.
 *
 * Set @a *youngest_p to the number of the youngest revision in filesystem 
 * @a fs.  Use @a pool for all temporary allocation.
 *
 * The oldest revision in any filesystem is numbered zero.
 */
svn_error_t *svn_fs_youngest_rev (svn_revnum_t *youngest_p,
                                  svn_fs_t *fs,
                                  apr_pool_t *pool);


/** Set @a *value_p to the value of the property named @a propname on
 * revision @a rev in the filesystem @a fs.
 *
 * Set @a *value_p to the value of the property named @a propname on
 * revision @a rev in the filesystem @a fs.  If @a rev has no property by 
 * that name, set @a *value_p to zero.  Allocate the result in @a pool.
 */
svn_error_t *svn_fs_revision_prop (svn_string_t **value_p,
                                   svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   const char *propname,
                                   apr_pool_t *pool);


/** Set @a *table_p to the entire property list of revision @a rev in
 * filesystem @a fs, as an APR hash table allocated in @a pool.
 *
 * Set @a *table_p to the entire property list of revision @a rev in
 * filesystem @a fs, as an APR hash table allocated in @a pool.  The table
 * maps <tt>char *</tt> property names to @c svn_string_t * values; the names
 * and values are allocated in @a pool.
 */
svn_error_t *svn_fs_revision_proplist (apr_hash_t **table_p,
                                       svn_fs_t *fs,
                                       svn_revnum_t rev,
                                       apr_pool_t *pool);


/** Change a revision's property's value, or add/delete a property.
 *
 * Change a revision's property's value, or add/delete a property.
 *
 * - @a fs is a filesystem, and @a rev is the revision in that filesystem
 *   whose property should change.
 * - @a name is the name of the property to change.
 * - @a VALUE is the new value of the property, or zero if the property should
 *   be removed altogether.
 *
 * Note that revision properties are non-historied --- you can change
 * them after the revision has been committed.  They are not protected
 * via transactions.
 *
 * Do any necessary temporary allocation in @a pool.
 */
svn_error_t *svn_fs_change_rev_prop (svn_fs_t *fs,
                                     svn_revnum_t rev,
                                     const char *name,
                                     const svn_string_t *value,
                                     apr_pool_t *pool);



/* Computing deltas.  */


/** Set @a *stream_p to a pointer to a delta stream that will turn the
 * contents of the file @a source into the contents of the file @a target.
 *
 * Set @a *stream_p to a pointer to a delta stream that will turn the
 * contents of the file @a source into the contents of the file @a target.
 * If @a source_root is zero, use a file with zero length as the source.
 *
 * This function does not compare the two files' properties.
 *
 * Allocate @a *stream_p, and do any necessary temporary allocation, in
 * @a pool.
 */
svn_error_t *
svn_fs_get_file_delta_stream (svn_txdelta_stream_t **stream_p,
                              svn_fs_root_t *source_root,
                              const char *source_path,
                              svn_fs_root_t *target_root,
                              const char *target_path,
                              apr_pool_t *pool);





/* Non-historical properties.  */

/* [[Yes, do tell.]] */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FS_H */
