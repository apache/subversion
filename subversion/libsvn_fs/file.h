/* file.h : interface to file nodes --- private to libsvn_fs
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

/* ==================================================================== */



#ifndef SVN_LIBSVN_FS_FILE_H
#define SVN_LIBSVN_FS_FILE_H


/* Set *CONTENTS to a readable generic stream which will return the
   contents of the node ID in FS.  Allocate the stream in POOL.

   *CONTENTS becomes invalid when FS is closed.  */
svn_error_t *svn_fs__file_contents (svn_stream_t **contents,
                                    svn_fs_t *fs,
                                    svn_fs_id_t *id,
                                    apr_pool_t *pool);


/* Create a new file node in FS, and set *ID_P to its node revision
   ID.  The file's initial contents are the empty string, and it has
   no properties.  Do any necessary temporary allocation in POOL.  */
svn_error_t *svn_fs__make_file (svn_fs_id_t *id_p,
                                svn_fs_t *fs,
                                apr_pool_t *pool);


/* Change the contents of the file node ID in FS.  Set *WRITER to a
   writable generic stream which sets the file's new contents.

   The node must have already been cloned as part of some transaction;
   otherwise, this function may corrupt the filesystem.

   Do any necessary temporary allocation in POOL. */
svn_error_t *svn_fs__write_file (svn_stream_t **writer,
                                 svn_fs_t *fs,
                                 svn_fs_id_t *id,
                                 apr_pool_t *pool);


#endif /* SVN_LIBSVN_FS_FILE_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
