/* reps-strings.h : interpreting representations w.r.t. strings
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

#ifndef SVN_LIBSVN_FS_REPS_STRINGS_H
#define SVN_LIBSVN_FS_REPS_STRINGS_H

#include "db.h"
#include "svn_io.h"
#include "svn_fs.h"
#include "trail.h"
#include "reps-table.h"
#include "strings-table.h"


/************************************************************************
 *                                                                      *
 * Note: all the functions here use representation keys, never rep      *
 * skels.  For readability, the doc strings refer to parameters such    *
 * as "REP", "NEW_REP", and so on.  These always refer to rep keys,     *
 * never actual representation objects.                                 *
 *                                                                      *
 ************************************************************************/
   




/* Get or create a mutable representation in FS, store the new rep's
   key in *NEW_REP.

   If REP is already a mutable representation, set *NEW_REP to REP,
   else set *NEW_REP to a new rep key allocated in TRAIL->pool.

   In the latter case, if REP refers to an immutable representation,
   then *NEW_REP refers to a mutable copy of it (a deep copy,
   including the rep's contents); else if REP is the empty string or
   null, *NEW_REP refers to a new, empty mutable representation
   (containing a new, empty string).

   If REP is neither null nor empty, but does not refer to any
   representation, the error SVN_ERR_FS_NO_SUCH_REPRESENTATION is
   returned.  */
svn_error_t *svn_fs__get_mutable_rep (const char **new_rep,
                                      const char *rep,
                                      svn_fs_t *fs, 
                                      trail_t *trail);


/* Make REP in FS immutable, if it isn't already, as part of TRAIL.
   If no such rep, return SVN_ERR_FS_NO_SUCH_REPRESENTATION.  */
svn_error_t *svn_fs__make_rep_immutable (svn_fs_t *fs,
                                         const char *rep,
                                         trail_t *trail);


/* Delete REP from FS if REP is mutable, as part of trail, or do
   nothing if REP is immutable.  If a mutable rep is deleted, the
   string it refers to is deleted as well.

   If no such rep, return SVN_ERR_FS_NO_SUCH_REPRESENTATION.  */ 
svn_error_t *svn_fs__delete_rep_if_mutable (svn_fs_t *fs,
                                            const char *rep,
                                            trail_t *trail);




/*** Reading and writing rep contents. ***/

/* Set *SIZE_P to the size of REP's contents in FS, as part of TRAIL.
   Note: this is the fulltext size, no matter how the contents are
   represented in storage.  */
svn_error_t *svn_fs__rep_contents_size (apr_size_t *size_p,
                                        svn_fs_t *fs,
                                        const char *rep,
                                        trail_t *trail);


/* Set STR->data to the contents of REP in FS, and STR->len to the
   contents' length, as part of TRAIL.  The data is allocated in
   TRAIL->pool.  If an error occurs, the effect on STR->data and
   STR->len is undefined.

   Note: this is the fulltext contents, no matter how the contents are
   represented in storage.  */
svn_error_t *svn_fs__rep_contents (svn_string_t *str,
                                   svn_fs_t *fs,
                                   const char *rep,
                                   trail_t *trail);


/* Return a stream to read the contents of REP.  Allocate the stream
   in POOL, and start reading at OFFSET in the rep's contents.

   If TRAIL is non-null, the stream's reads are part of TRAIL;
   otherwise, each read happens in an internal, one-off trail. 
   POOL may be TRAIL->pool.  */
svn_stream_t *svn_fs__rep_contents_read_stream (svn_fs_t *fs,
                                                const char *rep,
                                                apr_size_t offset,
                                                trail_t *trail,
                                                apr_pool_t *pool);

                                       
/* Return a stream to write the contents of REP.  Allocate the stream
   in POOL.

   If the rep already has contents, the stream will append.  You can
   use svn_fs__rep_contents_clear() to clear the contents first.

   If TRAIL is non-null, the stream's writes are part of TRAIL;
   otherwise, each write happens in an internal, one-off trail.
   POOL may be TRAIL->pool.

   If REP is not mutable, writes will return the error
   SVN_ERR_FS_REP_NOT_MUTABLE.  */
svn_stream_t *svn_fs__rep_contents_write_stream (svn_fs_t *fs,
                                                 const char *rep,
                                                 trail_t *trail,
                                                 apr_pool_t *pool);


/* Clear the contents of REP, so that it represents the empty string,
   as part of TRAIL.  If REP is not mutable, return the error
   SVN_ERR_FS_REP_NOT_MUTABLE.  */  
svn_error_t *svn_fs__rep_contents_clear (svn_fs_t *fs,
                                         const char *rep,
                                         trail_t *trail);



/*** Deltified storage. ***/

/* Offer TARGET the chance to store its contents as a delta against
   SOURCE, in FS, as part of TRAIL.  TARGET and SOURCE are both
   representation keys.

   This usually results in TARGET's data being stored as a diff
   against SOURCE; but it might not, if it turns out to be more
   efficient to store the contents some other way.  */
svn_error_t *svn_fs__rep_deltify (svn_fs_t *fs,
                                  const char *target,
                                  const char *source,
                                  trail_t *trail);


/* Ensure that REP refers to storage that is maintained as fulltext,
   not as a delta against other strings, in FS, as part of TRAIL.  */
svn_error_t *svn_fs__rep_undeltify (svn_fs_t *fs,
                                    const char *rep,
                                    trail_t *trail);



#endif /* SVN_LIBSVN_FS_REPS_STRINGS_H */



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
