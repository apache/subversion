/* strings-table.h : internal interface to `strings' table
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_FS_STRINGS_TABLE_H
#define SVN_LIBSVN_FS_STRINGS_TABLE_H

#include "db.h"
#include "svn_io.h"
#include "svn_fs.h"
#include "trail.h"



/* This interface provides raw access to the `strings' table.  It does
   not deal with deltification, undeltification, or skels.  It just
   reads and writes strings of bytes. */


/* Open a `strings' table in ENV.  If CREATE is non-zero, create
 * one if it doesn't exist.  Set *STRINGS_P to the new table.  
 * Return a Berkeley DB error code.
 */
int svn_fs__open_strings_table (DB **strings_p,
                                DB_ENV *env,
                                int create);


/* Set *STREAM to a read stream on string KEY in FS, as part of
 * TRAIL.  
 * 
 * Allocate the stream in TRAIL->pool.  KEY will be shared by the
 * stream, not copied, so KEY's storage must be at least as long-lived
 * as TRAIL->pool.
 * 
 * If string KEY does not exist, return SVN_ERR_FS_NO_SUCH_STRING.
 */
svn_error_t *svn_fs__read_string_stream (svn_stream_t **stream,
                                         svn_fs_t *fs,
                                         const char *key,
                                         trail_t *trail);


/* Set *SIZE to the size in bytes of string KEY in FS, as part of
 * TRAIL.
 *
 * If string KEY does not exist, return SVN_ERR_FS_NO_SUCH_STRING.
 */
svn_error_t *svn_fs__string_size (apr_size_t *size,
                                  svn_fs_t *fs,
                                  const char *key,
                                  trail_t *trail);


/* Set *STREAM to a write stream for string KEY in FS, as part of
 * TRAIL.
 * 
 * Allocate the stream in TRAIL->pool.  KEY will be shared by the
 * stream, not copied, so KEY's storage must be at least as long-lived
 * as TRAIL->pool.
 * 
 * Create the string if it does not exist; otherwise, overwrite it.
 */
svn_error_t *svn_fs__write_string_stream (svn_stream_t **stream,
                                          svn_fs_t *fs,
                                          const char *key,
                                          trail_t *trail);


/* Set *STREAM to a write stream for a new string in FS, as part of
 * TRAIL.  Store the new string's key in *KEY.
 * 
 * Allocate the stream and the new key in TRAIL->pool.
 */
svn_error_t *svn_fs__new_string_stream (const char *key,
                                        svn_stream_t **stream,
                                        svn_fs_t *fs,
                                        trail_t *trail);


/* Set *STREAM to an appending write stream for string KEY in FS, as
 * part of TRAIL.
 * 
 * Allocate the stream in TRAIL->pool.  KEY will be shared by the
 * stream, not copied, so KEY's storage must be at least as long-lived
 * as TRAIL->pool.
 * 
 * Create the string if it does not exist; otherwise, overwrite it.
 */
svn_error_t *svn_fs__append_string_stream (svn_stream_t **stream,
                                           svn_fs_t *fs,
                                           const char *key,
                                           trail_t *trail);


/* Delete string KEY from FS, as part of TRAIL.
 *
 * If string KEY does not exist, return SVN_ERR_FS_NO_SUCH_STRING.
 *
 * WARNING: Deleting a string renders unusable both nodes that refer
 * directly to the string and nodes that are deltas against it.
 * Probably the only time to remove a string is when it is referred to
 * only by a txn that is itself being removed.
 */ 
svn_error_t *svn_fs__delete_string (svn_fs_t *fs,
                                    const char *key,
                                    trail_t *trail);




#endif /* SVN_LIBSVN_FS_STRINGS_TABLE_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
