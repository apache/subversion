/* clones-table.h : internal interface to `clones' table
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

#ifndef SVN_LIBSVN_FS_CLONES_TABLE_H
#define SVN_LIBSVN_FS_CLONES_TABLE_H

#include "svn_fs.h"

/* Set *CLONE_INFO_P to the entry from the `clones' table for
   BASE_PATH in the Subversion transaction SVN_TXN in FS, or zero if
   there is no such entry, as part of the Berkeley DB transaction
   DB_TXN.  This assures that *CLONE_INFO_P is either zero or a
   well-formed CLONE skel.  Allocate the result in POOL.  */
svn_error_t *svn_fs__check_clone (skel_t **clone_info_p,
				  svn_fs_t *fs,
				  const char *svn_txn,
				  const char *base_path,
				  DB_TXN *db_txn,
				  apr_pool_t *pool);


/* If CLONE_INFO indicates that a node was cloned, set *CLONE_ID_P to
   the ID of the clone, and return non-zero.  Else, return zero.
   CLONE_INFO must be a well-formed CLONE skel.  */
int svn_fs__is_cloned (skel_t **clone_id_p, skel_t *clone_info);


/* If CLONE_INFO indicates that a node was renamed, set
   *PARENT_CLONE_ID_P to an atom skel representing the ID of the new
   parent, and *ENTRY_NAME_P to an atom skel holding the name of the
   entry in *PARENT_CLONE_P that now refers to the node, and return
   non-zero.  Else, return zero.  CLONE_INFO must be a well-formed
   CLONE skel.  */
int svn_fs__is_renamed (skel_t **parent_clone_id_p,
			skel_t **entry_name_p,
			skel_t *clone_info);


#endif /* SVN_LIBSVN_FS_CLONES_TABLE_H */
