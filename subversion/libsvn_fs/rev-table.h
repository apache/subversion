/* rev-table.h : internal interface to revision table operations
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_FS_REV_TABLE_H
#define SVN_LIBSVN_FS_REV_TABLE_H

#include "svn_fs.h"
#include "trail.h"


/* Set *ROOT_ID_P to the ID of the root directory of revision REV in FS,
   as part of TRAIL.  Allocate the ID in TRAIL->pool.  */
svn_error_t *svn_fs__rev_get_root (svn_fs_id_t **root_id_p,
				   svn_fs_t *fs,
				   svn_revnum_t rev,
				   trail_t *trail);


#endif /* SVN_LIBSVN_FS_REV_TABLE_H */
