/* node-rev.h : interface to node revision retrieval and storage
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

#ifndef SVN_LIBSVN_FS_NODE_REV_H
#define SVN_LIBSVN_FS_NODE_REV_H

#include "db.h"
#include "svn_fs.h"
#include "skel.h"
#include "trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Accessor Macros. ***/

/* Access the HEADER of a node revision skel. */
#define SVN_FS__NR_HEADER(node_rev) ((node_rev)->children)

/* Access the PROP-KEY of a node revision skel. */
#define SVN_FS__NR_PROP_KEY(node_rev) ((node_rev)->children->next)

/* Access the DATA-KEY (or ENTRIES-KEY) of a node revision skel. */
#define SVN_FS__NR_DATA_KEY(node_rev) ((node_rev)->children->next->next)

/* Access the EDIT-DATA-KEY of a `file' node revision skel. */
#define SVN_FS__NR_EDIT_KEY(node_rev) ((node_rev)->children->next->next->next)

/* Access the KIND skel of a node revision header.
   NOTE: takes a header skel, not a node-revision skel.  */
#define SVN_FS__NR_HDR_KIND(header) ((header)->children)

/* Access the REV skel of a node revision header.
   NOTE: takes a header skel, not a node-revision skel.  */
#define SVN_FS__NR_HDR_REV(header) ((header)->children->next)

/* Access the COPY skel of a node revision header, null if none.
   NOTE: takes a header skel, not a node-revision skel.

   Note for the future: we may eventually have other optional fields
   in a node revision header.  If that happens, and their order is
   unfixed, then it will probably pay to make a function
   svn_fs__nr_hdr_option() that takes a string and returns the
   corresponding skel, and then the accessor macros for the third item
   and beyond in a header would use that function.  Or something. */
#define SVN_FS__NR_HDR_COPY(header) ((header)->children->next->next)



/*** Functions. ***/

/* Create an entirely new, mutable node in the filesystem FS, whose
   NODE-REVISION skel is SKEL, as part of TRAIL.  Set *ID_P to the new
   node revision's ID.  Use TRAIL->pool for any temporary allocation.

   This function checks that SKEL is a well-formed NODE-REVISION skel.

   After this call, the node table manager assumes that the new node's
   contents will change frequently.  */
svn_error_t *svn_fs__create_node (svn_fs_id_t **id_p,
                                  svn_fs_t *fs,
                                  skel_t *skel,
                                  trail_t *trail);


/* Create a node revision in FS which is an immediate successor of
   OLD_ID, whose contents are NEW_SKEL, as part of TRAIL.  Set
   *NEW_ID_P to the new node revision's ID.  Use TRAIL->pool for any
   temporary allocation.

   This function checks that NEW_SKEL is a well-formed NODE-REVISION
   skel.

   After this call, the deltification code assumes that the new node's
   contents will change frequently, and will avoid representing other
   nodes as deltas against this node's contents.  */
svn_error_t *svn_fs__create_successor (svn_fs_id_t **new_id_p,
                                       svn_fs_t *fs,
                                       svn_fs_id_t *old_id,
                                       skel_t *new_skel,
                                       trail_t *trail);


/* Indicate that the contents of the node ID in FS are expected to be
   stable now, as part of TRAIL.  This suggests to the deltification
   code that it could be effective to represent other nodes' contents
   as deltas against this node's contents.  This does not change the
   contents of the node.

   Do any necessary temporary allocation in TRAIL->pool.  */
svn_error_t *svn_fs__stable_node (svn_fs_t *fs,
                                  svn_fs_id_t *id,
                                  trail_t *trail);


/* Delete node revision ID from FS's `nodes' table, as part of TRAIL.
   WARNING: This does not check that the node revision is mutable!
   Callers should do that check themselves.  */
svn_error_t *svn_fs__delete_node_revision (svn_fs_t *fs,
                                           const svn_fs_id_t *id,
                                           trail_t *trail);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_NODE_REV_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
