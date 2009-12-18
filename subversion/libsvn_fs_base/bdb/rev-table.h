/* rev-table.h : internal interface to revision table operations
 *
 * ====================================================================
 * Copyright (c) 2000-2004, 2009 CollabNet.  All rights reserved.
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

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "svn_fs.h"

#include "../fs.h"
#include "../trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Creating and opening the `revisions' table.  */

/* Open a `revisions' table in ENV.  If CREATE is non-zero, create one
   if it doesn't exist.  Set *REVS_P to the new table.  Return a
   Berkeley DB error code.  */
int svn_fs_bdb__open_revisions_table(DB **revisions_p,
                                     DB_ENV *env,
                                     svn_boolean_t create);



/* Storing and retrieving filesystem revisions.  */


/* Set *REVISION_P to point to the revision structure for the
   filesystem revision REV in FS, as part of TRAIL.  Perform all
   allocations in POOL.  */
svn_error_t *svn_fs_bdb__get_rev(revision_t **revision_p,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 trail_t *trail,
                                 apr_pool_t *pool);

/* Store REVISION in FS as revision *REV as part of TRAIL.  If *REV is
   an invalid revision number, create a brand new revision and return
   its revision number as *REV to the caller.  Do any necessary
   temporary allocation in POOL.  */
svn_error_t *svn_fs_bdb__put_rev(svn_revnum_t *rev,
                                 svn_fs_t *fs,
                                 const revision_t *revision,
                                 trail_t *trail,
                                 apr_pool_t *pool);


/* Set *YOUNGEST_P to the youngest revision in filesystem FS,
   as part of TRAIL.  Use POOL for all temporary allocation. */
svn_error_t *svn_fs_bdb__youngest_rev(svn_revnum_t *youngest_p,
                                      svn_fs_t *fs,
                                      trail_t *trail,
                                      apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_REV_TABLE_H */
