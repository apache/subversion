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
   reads and writes strings of bytes.  

   They KEY argument is often, but not necessarily, an unparsed
   svn_fs_id_t.  */


/* Open a `strings' table in ENV.  If CREATE is non-zero, create
 * one if it doesn't exist.  Set *STRINGS_P to the new table.  
 * Return a Berkeley DB error code.
 */
int svn_fs__open_strings_table (DB **strings_p,
                                DB_ENV *env,
                                int create);


/* Set *STREAM to a read stream on string KEY in FS, as part of
 * TRAIL.  If KEY does not exist, SVN_ERR_FS_NO_SUCH_STRING is the
 * error returned.
 */
svn_error_t *svn_fs__read_string_stream (svn_stream_t **stream,
                                         svn_fs_t *fs,
                                         const char *key,
                                         trail_t *trail);


/* Set *STREAM to a write stream for string KEY in FS, as part of
 * TRAIL.  The string will be created if it did not exist; otherwise,
 * it will be overwritten.
 */
svn_error_t *svn_fs__write_string_stream (svn_stream_t **stream,
                                          svn_fs_t *fs,
                                          const char *key,
                                          trail_t *trail);


/* Set *STREAM to an appending write stream for string KEY in FS, as
 * part of TRAIL.  The string will be created if it did not exist,
 * otherwise, it will be appended to.
 */
svn_error_t *svn_fs__append_string_stream (svn_stream_t **stream,
                                           svn_fs_t *fs,
                                           const char *key,
                                           trail_t *trail);


/* There is no function to delete an entry from `strings'.  Deleting a
   string is very dangerous: it would render unusable both nodes that
   refer directly to the string and those that are deltas against it.
   If we ever really need string deletion, though, it's easy to add.  */


#endif /* SVN_LIBSVN_FS_STRINGS_TABLE_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
