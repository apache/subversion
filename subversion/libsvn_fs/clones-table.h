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
#include "trail.h"
#include "skel.h"


/* Open a `clones' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *CLONES_P to the new table.  
   Return a Berkeley DB error code.  */
int svn_fs__open_clones_table (DB **clones_p,
			       DB_ENV *env,
			       int create);


/* Set *CLONE_P to the entry from the `clones' table for
   BASE_PATH in the Subversion transaction SVN_TXN in FS, or zero if
   there is no such entry, as part of TRAIL.  This assures that
   *CLONE_P is either zero or a well-formed CLONE skel.  Allocate
   the result in TRAIL->pool.  */
svn_error_t *svn_fs__check_clone (skel_t **clone_p,
				  svn_fs_t *fs,
				  const char *svn_txn,
				  const char *base_path,
				  trail_t *trail);


/* If CLONE indicates that a node was cloned, set *CLONE_ID_P to
   the ID of the clone, and return non-zero.  Else, return zero.
   CLONE must be a well-formed CLONE skel.  */
int svn_fs__is_cloned (skel_t **clone_id_p, skel_t *clone);


/* If CLONE indicates that a node was renamed, set
   *PARENT_CLONE_ID_P to an atom skel representing the ID of the new
   parent, and *ENTRY_NAME_P to an atom skel holding the name of the
   entry in *PARENT_CLONE_P that now refers to the node, and return
   non-zero.  Else, return zero.  CLONE must be a well-formed
   CLONE skel.  */
int svn_fs__is_renamed (skel_t **parent_clone_id_p,
			skel_t **entry_name_p,
			skel_t *clone);


/* Record that BASE_PATH was cloned in the Subversion transaction
   SVN_TXN to produce node CLONE_ID in FS, as part of TRAIL.  */
svn_error_t *svn_fs__record_clone (svn_fs_t *fs,
				   const char *svn_txn,
				   const char *base_path,
				   const svn_fs_id_t *clone_id,
				   trail_t *trail);


/* Record that BASE_PATH was renamed in the Subversion transaction
   SVN_TXN, and is now named ENTRY_NAME in the mutable directory
   PARENT_ID, as part of TRAIL.  */
svn_error_t *svn_fs__record_rename (svn_fs_t *fs,
				    const char *svn_txn,
				    const char *base_path,
				    const svn_fs_id_t *parent_id,
				    const char *entry_name,
				    trail_t *trail);


#endif /* SVN_LIBSVN_FS_CLONES_TABLE_H */
