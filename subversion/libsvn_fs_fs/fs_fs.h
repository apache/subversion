/* fs_fs.h : interface to the native filesystem layer
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

#ifndef SVN_LIBSVN_FS__FS_FS_H
#define SVN_LIBSVN_FS__FS_FS_H

/* Open the fs_fs filesystem pointed to by PATH and associate it with
   filesystem object FS.  Use POOL for temporary allocations. */
svn_error_t *svn_fs__fs_open (svn_fs_t *fs,
                              const char *path,
                              apr_pool_t *pool);

/* Set *NODEREV_P to the node-revision for the node ID in FS.  Do any
   allocations in POOL. */
svn_error_t *svn_fs__fs_get_node_revision (svn_fs__node_revision_t **noderev_p,
                                           svn_fs_t *fs,
                                           const svn_fs_id_t *id,
                                           apr_pool_t *pool);

/* Store NODEREV as the node-revision for the node whose id is ID in
   FS.  Do any necessary temporary allocation in POOL. */
svn_error_t *svn_fs__fs_put_node_revision (svn_fs_t *fs,
                                           const svn_fs_id_t *id,
                                           svn_fs__node_revision_t *noderev,
                                           apr_pool_t *pool);

/* Set *YOUNGEST to the youngest revision in filesystem FS.  Do any
   temporary allocation in POOL. */
svn_error_t *svn_fs__fs_youngest_revision (svn_revnum_t *youngest,
                                           svn_fs_t *fs,
                                           apr_pool_t *pool);

/* Set *ROOT_ID to the node-id for the root of revision REV in
   filesystem FS.  Do any allocations in POOL. */
svn_error_t *svn_fs__fs_rev_get_root (svn_fs_id_t **root_id,
                                      svn_fs_t *fs,
                                      svn_revnum_t rev,
                                      apr_pool_t *pool);

/* Set *ENTRIES to an apr_hash_t of dirent structs that contain the
   directory entries of node-revision NODEREV in filesystem FS.  Use
   POOL for temporary allocations. */
svn_error_t *svn_fs__fs_rep_contents_dir (apr_hash_t **entries,
                                          svn_fs_t *fs,
                                          svn_fs__node_revision_t *noderev,
                                          apr_pool_t *pool);

/* Set *CONTENTS to be a readable svn_stream_t that receives the text
   representation of node-revision NODEREV as seen in filesystem FS.
   Use POOL for temporary allocations. */
svn_error_t *svn_fs__fs_get_contents (svn_stream_t **contents,
                                      svn_fs_t *fs,
                                      svn_fs__node_revision_t *noderev,
                                      apr_pool_t *pool);

/* Set *PROPLIST to be an apr_hash_t containing the property list of
   node-revision NODEREV as seen in filesystem FS.  Use POOL for
   temporary allocations. */
svn_error_t *svn_fs__fs_get_proplist (apr_hash_t **proplist,
                                      svn_fs_t *fs,
                                      svn_fs__node_revision_t *noderev,
                                      apr_pool_t *pool);

/* Set *PROPLIST to be an apr_hash_t containing the property list of
   revision REV as seen in filesystem FS.  Use POOL for temporary
   allocations. */
svn_error_t *svn_fs__fs_revision_proplist (apr_hash_t **proplist,
                                           svn_fs_t *fs,
                                           svn_revnum_t rev,
                                           apr_pool_t *pool);

/* Following are defines that specify the textual elements of the
   native filesystem directories and revision files. */

/* Names of special files in the fs_fs filesystem. */
#define SVN_FS_FS__UUID              "uuid"     /* Contains UUID */
#define SVN_FS_FS__CURRENT           "current"  /* Youngest revision */
#define SVN_FS_FS__REV_PROPS_EXT     ".props"   /* extension for rev-props */

/* Headers used to describe node-revision in the revision file. */
#define SVN_FS_FS__NODE_ID           "id"       
#define SVN_FS_FS__KIND              "type"     
#define SVN_FS_FS__COUNT             "count"    
#define SVN_FS_FS__PROPS             "props"    
#define SVN_FS_FS__REP               "rep"      
#define SVN_FS_FS__CPATH             "cpath"

/* Kinds that a node-rev can be. */
#define SVN_FS_FS__FILE              "file"     /* node-rev kind for file */
#define SVN_FS_FS__DIR               "dir"      /* node-rev kind for directory */

#endif
