/* reps-strings.h : interpreting representations w.r.t. strings
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

#ifndef SVN_LIBSVN_FS_REPS_STRINGS_H
#define SVN_LIBSVN_FS_REPS_STRINGS_H

#include "db.h"
#include "svn_io.h"
#include "svn_fs.h"
#include "trail.h"
#include "reps-table.h"
#include "strings-table.h"


/* Return the string key pointed to by REP, allocated in POOL.
   If REP is a fulltext rep, just return the string; if delta, return
   the string key for the svndiff data, not the base.  */
const char *svn_fs__string_key_from_rep (skel_t *rep, apr_pool_t *pool);


/* Return non-zero if representation skel REP is mutable.  */
int svn_fs__rep_is_mutable (skel_t *rep);


/* Get or create a mutable representation in FS, store the new rep's
   key in *NEW_KEY.

   If KEY already refers to a mutable representation, *NEW_KEY is set
   to KEY, else *NEW_KEY is set to a new rep key allocated in
   TRAIL->pool.

   In the latter case, if KEY refers to an immutable representation,
   then *NEW_KEY refers to a mutable copy of it (a deep copy,
   including a new copy of the underlying string); else if KEY is the
   empty string or null, *NEW_KEY refers to a new, empty mutable
   representation (containing a new, empty string).

   If KEY is neither null nor empty, but does not refer to any
   representation, the error SVN_ERR_FS_NO_SUCH_REPRESENTATION is
   returned.  */
svn_error_t *svn_fs__get_mutable_rep (const char **new_key,
                                      const char *key,
                                      svn_fs_t *fs, 
                                      trail_t *trail);


/* Make representation KEY in FS immutable, if it isn't already, as
   part of TRAIL.  If there is no such rep, return the error
   SVN_ERR_FS_NO_SUCH_REPRESENTATION.  */
svn_error_t *svn_fs__make_rep_immutable (svn_fs_t *fs,
                                         const char *key,
                                         trail_t *trail);


/* Delete representation KEY from FS if it's mutable, as part of
   trail, or do nothing if the rep is immutable.  If a mutable rep is
   deleted, the string it refers to is deleted as well.

   If no such rep, return SVN_ERR_FS_NO_SUCH_REPRESENTATION.  */ 
svn_error_t *svn_fs__delete_rep_if_mutable (svn_fs_t *fs,
                                            const char *key,
                                            trail_t *trail);




/*** Reading and writing rep contents. ***/

/* Set *SIZE to the size of rep REP_KEY's contents in FS, as part of
   TRAIL.  Note: this is the fulltext size, no matter how the contents
   are represented in storage.  */
svn_error_t *svn_fs__rep_contents_size (apr_size_t *size,
                                        svn_fs_t *fs,
                                        const char *rep_key,
                                        trail_t *trail);


/* Set STR->data to the contents of rep REP_KEY in FS, and STR->len to
   the contents' length, as part of TRAIL.  The data is allocated in
   TRAIL->pool.  */
svn_error_t *svn_fs__rep_contents (svn_string_t *str,
                                   svn_fs_t *fs,
                                   const char *rep_key,
                                   trail_t *trail);


/* Return a stream to read the contents of the representation
   identified by REP_KEY.  Allocate the stream in POOL, and start
   reading at OFFSET in the rep's contents.

   If TRAIL is non-null, the stream's reads are part of TRAIL;
   otherwise, each read happens in an internal, one-off trail. 
   POOL may be TRAIL->pool.  */
svn_stream_t *svn_fs__rep_contents_read_stream (svn_fs_t *fs,
                                                const char *rep_key,
                                                apr_size_t offset,
                                                trail_t *trail,
                                                apr_pool_t *pool);

                                       
/* Return a stream to write the contents of the representation
   identified by REP_KEY.  Allocate the stream in POOL.

   If the rep already has contents, the stream will append.  You can
   use svn_fs__rep_contents_clear() to clear the contents first.

   If TRAIL is non-null, the stream's writes are part of TRAIL;
   otherwise, each write happens in an internal, one-off trail.
   POOL may be TRAIL->pool.

   If the representation is not mutable, writes will return the error
   SVN_ERR_FS_REP_NOT_MUTABLE.  */
svn_stream_t *svn_fs__rep_contents_write_stream (svn_fs_t *fs,
                                                 const char *rep_key,
                                                 trail_t *trail,
                                                 apr_pool_t *pool);


/* Clear the contents of representation REP_KEY, so that it represents
   the empty string, as part of TRAIL.  If the representation is not
   mutable, return the error SVN_ERR_FS_REP_NOT_MUTABLE.  */
svn_error_t *svn_fs__rep_contents_clear (svn_fs_t *fs,
                                         const char *rep_key,
                                         trail_t *trail);


/* stabilize_rep */
/* ### todo: yes, precisely.  This should be here, instead of in
   node-rev.c:deltify(). */

#endif /* SVN_LIBSVN_FS_REPS_STRINGS_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
