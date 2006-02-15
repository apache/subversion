/* changes-table.h : internal interface to `changes' table
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#ifndef SVN_LIBSVN_FS_CHANGES_TABLE_H
#define SVN_LIBSVN_FS_CHANGES_TABLE_H

#define APU_WANT_DB
#include <apu_want.h>

#include "svn_io.h"
#include "svn_fs.h"
#include "../fs.h"
#include "../trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Open a `changes' table in ENV.  If CREATE is non-zero, create one
   if it doesn't exist.  Set *CHANGES_P to the new table.  Return a
   Berkeley DB error code.  */
int svn_fs_bdb__open_changes_table(DB **changes_p,
                                   DB_ENV *env,
                                   svn_boolean_t create);


/* Add CHANGE as a record to the `changes' table in FS as part of
   TRAIL, keyed on KEY.

   CHANGE->path is expected to be a canonicalized filesystem path (see
   svn_fs__canonicalize_abspath).

   Note that because the `changes' table uses duplicate keys, this
   function will not overwrite prior additions that have the KEY
   key, but simply adds this new record alongside previous ones.  */
svn_error_t *svn_fs_bdb__changes_add(svn_fs_t *fs,
                                     const char *key,
                                     change_t *change,
                                     trail_t *trail,
                                     apr_pool_t *pool);


/* Remove all changes associated with KEY from the `changes' table in
   FS, as part of TRAIL. */
svn_error_t *svn_fs_bdb__changes_delete(svn_fs_t *fs,
                                        const char *key,
                                        trail_t *trail,
                                        apr_pool_t *pool);

/* Return a hash *CHANGES_P, keyed on const char * paths, and
   containing svn_fs_path_change_t * values representing summarized
   changed records associated with KEY in FS, as part of TRAIL.
   Allocate the array and its items in POOL.  */
svn_error_t *svn_fs_bdb__changes_fetch(apr_hash_t **changes_p,
                                       svn_fs_t *fs,
                                       const char *key,
                                       trail_t *trail,
                                       apr_pool_t *pool);

/* Return an array *CHANGES_P of change_t * items representing
   all the change records associated with KEY in FS, as part of TRAIL.
   Allocate the array and its items in POOL.  */
svn_error_t *svn_fs_bdb__changes_fetch_raw(apr_array_header_t **changes_p,
                                           svn_fs_t *fs,
                                           const char *key,
                                           trail_t *trail,
                                           apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_CHANGES_TABLE_H */
