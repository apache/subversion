/* rev-table.h : internal interface to revision table operations
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

#ifndef SVN_LIBSVN_FS_REV_TABLE_H
#define SVN_LIBSVN_FS_REV_TABLE_H

#include <db.h>
#include "fs.h"
#include "svn_fs.h"
#include "trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Creating and opening the `revisions' table.  */

/* Open a `revisions' table in ENV.  If CREATE is non-zero, create one
   if it doesn't exist.  Set *REVS_P to the new table.  Return a
   Berkeley DB error code.  */
int svn_fs__open_revisions_table (DB **revisions_p,
                                  DB_ENV *env,
                                  int create);



/* Storing and retrieving filesystem revisions.  */


/* Set *REVISION_P to point to the revision structure for the
   filesystem revision REV in FS, as part of TRAIL.  Perform all
   allocations in TRAIL->pool.  */
svn_error_t *svn_fs__get_rev (svn_fs__revision_t **revision_p,
                              svn_fs_t *fs,
                              svn_revnum_t rev,
                              trail_t *trail);

/* Store REVISION in FS as part of TRAIL, and return the new
   filesystem revision number in *REV.  Do any necessary temporary
   allocation in TRAIL->pool.  */
svn_error_t *svn_fs__put_rev (svn_revnum_t *rev,
                              svn_fs_t *fs,
                              const svn_fs__revision_t *revision,
                              trail_t *trail);


/* Set *ROOT_ID_P to the ID of the root directory of revision REV in FS,
   as part of TRAIL.  Allocate the ID in TRAIL->pool.  */
svn_error_t *svn_fs__rev_get_root (const svn_fs_id_t **root_id_p,
                                   svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   trail_t *trail);


/* Set *YOUNGEST_P to the youngest revision in filesystem FS,
   as part of TRAIL.  Use TRAIL->pool for all temporary allocation. */
svn_error_t *svn_fs__youngest_rev (svn_revnum_t *youngest_p,
                                   svn_fs_t *fs,
                                   trail_t *trail);


/* Set property NAME to VALUE on REV in FS, as part of TRAIL.  */
svn_error_t *svn_fs__set_rev_prop (svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   const char *name,
                                   const svn_string_t *value,
                                   trail_t *trail);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_REV_TABLE_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
