/* bdb_fs.h : interface to Subversion filesystem, private to libsvn_fs/bdb
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

#ifndef SVN_BDB_FS_H
#define SVN_BDB_FS_H

#include <db.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Temporary semi-public declarations. The functions listed here become static
 * when vtables are implemented in a later patch.
 */
svn_error_t *
bdb_set_berkeley_errcall (svn_fs_t *fs, 
                          void (*db_errcall_fcn) (const char *errpfx,
                          char *msg));
apr_status_t bdb_cleanup_fs_apr (void *data);
svn_error_t *bdb_create_fs (svn_fs_t *fs, const char *path, void *cfg);
svn_error_t *bdb_open_fs (svn_fs_t *fs, const char *path);
svn_error_t *bdb_recover_fs (const char *path, apr_pool_t *pool);
svn_error_t *bdb_delete_fs (const char *path, apr_pool_t *pool);

/* 
 * Table opens are here (moved from xxx-table.h) so that the <db.h> include 
 * could be removed from the xxx-table.h files thus removing the dependancy 
 * for all files that include them.
 */

/* Open a `changes' table in ENV.  If CREATE is non-zero, create one
   if it doesn't exist.  Set *CHANGES_P to the new table.  Return a
   Berkeley DB error code. */
int svn_fs__bdb_open_changes_table (DB **changes_p,
                                    DB_ENV *env,
                                    int create);

/* Open a `copies' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *COPIES_P to the new table.
   Return a Berkeley DB error code. */  
int svn_fs__bdb_open_copies_table (DB **copies_p,
                                   DB_ENV *env,
                                   int create);

/* Open a `nodes' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *NODES_P to the new table.
   Return a Berkeley DB error code.  */
int svn_fs__bdb_open_nodes_table (DB **nodes_p,
                                  DB_ENV *env,
                                  int create);
/* Open a `representations' table in ENV.  If CREATE is non-zero,
   create one if it doesn't exist.  Set *REPS_P to the new table.
   Return a Berkeley DB error code.  */
int svn_fs__bdb_open_reps_table (DB **reps_p, DB_ENV *env, int create);

/* Open a `revisions' table in ENV.  If CREATE is non-zero, create one
   if it doesn't exist.  Set *REVS_P to the new table.  Return a
   Berkeley DB error code.  */
int svn_fs__bdb_open_revisions_table (DB **revisions_p,
                                      DB_ENV *env,
                                      int create);

/* Open a `strings' table in ENV.  If CREATE is non-zero, create
 * one if it doesn't exist.  Set *STRINGS_P to the new table.
 * Return a Berkeley DB error code.
 */
int svn_fs__bdb_open_strings_table (DB **strings_p,
                                    DB_ENV *env,
                                    int create);

/* Open a `transactions' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *TRANSACTIONS_P to the new table.
   Return a Berkeley DB error code.  */
int svn_fs__bdb_open_transactions_table (DB **transactions_p,
                                         DB_ENV *env,
                                         int create);

/* Open a `uuids' table in @a env.
 *
 * Open a `uuids' table in @a env.  If @a create is non-zero, create
 * one if it doesn't exist.  Set @a *uuids_p to the new table.
 * Return a Berkeley DB error code.
 */
int svn_fs__bdb_open_uuids_table (DB **uuids_p,
                                  DB_ENV *env,
                                  int create);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_BDB_FS_H */
