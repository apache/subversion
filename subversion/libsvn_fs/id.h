/* id.h : interface to node ID functions, private to libsvn_fs
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

#ifndef SVN_LIBSVN_FS_ID_H
#define SVN_LIBSVN_FS_ID_H

#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Return non-zero iff the node or node revision ID's A and B are equal.  */
int svn_fs__id_eq (const svn_fs_id_t *a, const svn_fs_id_t *b);


/* Return the number of components in ID, not including the final -1.  */
int svn_fs__id_length (const svn_fs_id_t *id);


/* Return the predecessor id to ID, allocated in POOL.  If there is no
   possible predecessor id, return NULL.

   Does not check that the predecessor id is actually present in the
   filesystem.

   Does not check that ID is a valid node revision ID.  If you pass in
   something else, the results are undefined.  */
svn_fs_id_t *svn_fs__id_predecessor (const svn_fs_id_t *id, apr_pool_t *pool);


/* Return non-zero iff node revision A is an ancestor of node revision B.  
   If A == B, then we consider A to be an ancestor of B.  */
int svn_fs__id_is_ancestor (const svn_fs_id_t *a, const svn_fs_id_t *b);


/* Return a copy of ID, allocated from POOL.  */
svn_fs_id_t *svn_fs__id_copy (const svn_fs_id_t *id, apr_pool_t *pool);


/* Return true iff PARENT is a direct parent of CHILD.  */
int svn_fs__id_is_parent (const svn_fs_id_t *parent,
                          const svn_fs_id_t *child);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_ID_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
