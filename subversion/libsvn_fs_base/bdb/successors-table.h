/* successors-table.h : internal interface to `successors' table
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

#ifndef SVN_LIBSVN_FS_SUCCESSORS_TABLE_H
#define SVN_LIBSVN_FS_SUCCESSORS_TABLE_H

#define APU_WANT_DB
#include <apu_want.h>

#include "svn_io.h"
#include "svn_fs.h"
#include "../fs.h"
#include "../trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Open a `successors' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *SUCCESSORS_P to the new table.
   Return a Berkeley DB error code.  */
int svn_fs_bdb__open_successors_table(DB **successors_p,
                                      DB_ENV *env,
                                      svn_boolean_t create);


/* Add SUCC_ID as a record to the `successors' table in FS as part of
   TRAIL, keyed on NODE_ID.

   NODE_ID and SUCC_ID are unparsed node-revision-ID strings. 

   Note that because the `successors' table uses duplicate keys, this
   function will not overwrite prior additions that have the NODE_ID
   key, but simply adds this new record alongside previous ones.  */
svn_error_t *svn_fs_bdb__successors_add(svn_fs_t *fs,
                                        const char *node_id,
                                        const char *succ_id,
                                        trail_t *trail,
                                        apr_pool_t *pool);


/* Remove SUCC_ID as a successor for NODE_ID in the `successors' table
   in FS, as part of TRAIL.

   NODE_ID and SUCC_ID are unparsed node-revision-ID strings.  */
svn_error_t *svn_fs_bdb__successors_delete(svn_fs_t *fs,
                                           const char *node_id,
                                           const char *succ_id,
                                           trail_t *trail,
                                           apr_pool_t *pool);


/* Remove all successors associated with unparsed node-revision-ID
   NODE_ID from the `successors' table in FS, as part of TRAIL. */
svn_error_t *svn_fs_bdb__successors_delete_all(svn_fs_t *fs,
                                               const char *node_id,
                                               trail_t *trail,
                                               apr_pool_t *pool);

/* Return an array *SUCCESSORS_P of const char * successor IDs
   representing all the successors of NODE_ID in FS, as part of TRAIL.

   NODE_ID and the returned successor IDs are all unparsed
   node-revision-ID strings.

   Allocate the array and its items in POOL.  */
svn_error_t *svn_fs_bdb__successors_fetch(apr_array_header_t **successors_p,
                                          svn_fs_t *fs,
                                          const char *key,
                                          trail_t *trail,
                                          apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_SUCCESSORS_TABLE_H */
