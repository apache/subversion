/* copies-table.h : internal interface to ops on `copies' table
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

#ifndef SVN_LIBSVN_FS_COPIES_TABLE_H
#define SVN_LIBSVN_FS_COPIES_TABLE_H

#include "svn_fs.h"
#include "../fs.h"
#include "../trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Open a `copies' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *COPIES_P to the new table.
   Return a Berkeley DB error code.  */
int svn_fs__open_copies_table (DB **copies_p,
                               DB_ENV *env,
                               int create);

/* Reserve a slot in the `copies' table in FS for a new copy operation
   as part of TRAIL.  Return the slot's id in *COPY_ID_P, allocated in
   TRAIL->pool.  */
svn_error_t *svn_fs__reserve_copy_id (const char **copy_id_p,
                                      svn_fs_t *fs,
                                      trail_t *trail);

/* Create a new copy with id COPY_ID in FS as part of TRAIL.
   SRC_PATH/SRC_REV are the path/revision (respectively) of the copy
   source, and DST_NODEREV_ID is the node revision id of the copy
   destination.  

   COPY_ID should generally come from a call to svn_fs__reserve_copy_id().  */
svn_error_t *svn_fs__create_copy (const char *copy_id,
                                  svn_fs_t *fs,
                                  const char *src_path,
                                  svn_revnum_t src_rev,
                                  const svn_fs_id_t *dst_noderev_id,
                                  trail_t *trail);

/* Remove the copy whose name is COPY_ID from the `copies' table of
   FS, as part of TRAIL.  */
svn_error_t *svn_fs__delete_copy (svn_fs_t *fs,
                                  const char *copy_id,
                                  trail_t *trail);

/* Retrieve the copy *COPY_P named COPY_ID from the `copies' table of
   FS, as part of TRAIL.  Perform all allocations in TRAIL->pool.
   
   If there is no such copy, SVN_ERR_FS_NO_SUCH_COPY is the error
   returned.  */
svn_error_t *svn_fs__get_copy (svn_fs__copy_t **copy_p,
                               svn_fs_t *fs,
                               const char *copy_id,
                               trail_t *trail);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_COPIES_TABLE_H */


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
