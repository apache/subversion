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


/* Read *LEN bytes into BUF from OFFSET in string KEY in FS, as part
 * of TRAIL.
 * 
 * On return, *LEN is set to the number of bytes read.  If the
 * outgoing *LEN is less than the incoming, this indicates that the
 * end of the string was reached (no error is returned on end of
 * string).
 * 
 * If OFFSET is past the end of the string, the error
 * SVN_ERR_FS_SHORT_STRING is returned.  If string KEY does not exist,
 * the error SVN_ERR_FS_NO_SUCH_STRING is returned.
 */
svn_error_t *svn_fs__string_read (svn_fs_t *fs,
                                  const char *key,
                                  apr_off_t offset,
                                  apr_size_t *len,
                                  char *buf,
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


/* Append LEN bytes from BUF to string *KEY in FS, as part of TRAIL.
 *
 * If *KEY is null, then create a new string and store the new key in
 * *KEY (allocating it in TRAIL->pool), and write LEN bytes from BUF
 * as the initial contents of the string.
 *
 * If *KEY is not null but there is no string named *KEY, return
 * SVN_ERR_FS_NO_SUCH_STRING.
 *
 * Note: to overwrite the old contents of a string, call
 * svn_fs__string_clear() and then svn_fs__string_append().  */
svn_error_t *svn_fs__string_append (svn_fs_t *fs,
                                    const char **key,
                                    apr_size_t len,
                                    const char *buf,
                                    trail_t *trail);


/* Make string KEY in FS zero length, as part of TRAIL.
 * If the string does not exist, return SVN_ERR_FS_NO_SUCH_STRING.
 */
svn_error_t *svn_fs__string_clear (svn_fs_t *fs,
                                   const char *key,
                                   trail_t *trail);


/* Delete string KEY from FS, as part of TRAIL.
 *
 * If string KEY does not exist, return SVN_ERR_FS_NO_SUCH_STRING.
 *
 * WARNING: Deleting a string renders unusable any representations
 * that refer to it.  Be careful.
 */ 
svn_error_t *svn_fs__string_delete (svn_fs_t *fs,
                                    const char *key,
                                    trail_t *trail);




#endif /* SVN_LIBSVN_FS_STRINGS_TABLE_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
