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
   ### todo:
   The behavior of this function on non-fulltext representations is
   undefined at present.  */
const char *svn_fs__string_key_from_rep (skel_t *rep, apr_pool_t *pool);


/* Set STR->data to the fulltext string for REP in FS, and STR->len to
   the string's length, as part of TRAIL.  The data is allocated in
   TRAIL->pool.  */
svn_error_t *svn_fs__string_from_rep (svn_string_t *str,
                                      svn_fs_t *fs,
                                      skel_t *rep,
                                      trail_t *trail);


/* Return non-zero if representation skel REP is mutable.  */
int svn_fs__rep_is_mutable (skel_t *rep);


/* Get a key to a mutable version of the representation pointed to by
   KEY in FS, and store it in *NEW_KEY.  

   If KEY is already a mutable representation, *NEW_KEY is set to
   KEY, else *NEW_KEY is set to a new rep key allocated in
   TRAIL->pool.  In the latter case, if KEY referred to an immutable
   representation, then *NEW_KEY refers to a mutable copy of it (a
   deep copy, including the underlying string), but if KEY is the
   empty string or null, *NEW_KEY refers to a new, empty mutable
   representation.

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




/*** Stream-based reading. ***/

typedef struct svn_fs__rep_read_baton_t
{
  /* The FS from which we're reading. */
  svn_fs_t *fs;

  /* The representation skel whose contents we want to read.  If this
     is null, the rep has never had any contents, so all reads fetch 0
     bytes.

     Formerly, we cached the entire rep skel here, not just the key.
     That way we didn't have to fetch the rep from the db every time
     we want to read a little bit more of the file.  Unfortunately,
     this has a problem: if, say, a file's representation changes
     while we're reading (changes from fulltext to delta, for
     example), we'll never know it.  So for correctness, we now
     refetch the representation skel every time we want to read
     another chunk.  */
  const char *rep_key;
  
  /* How many bytes have been read already. */
  apr_size_t offset;

  /* If present, the read will be done as part of this trail, and the
     trail's pool will be used.  Otherwise, see `pool' below.  */
  trail_t *trail;

  /* Used for temporary allocations, iff `trail' (above) is null.  */
  apr_pool_t *pool;

} svn_fs__rep_read_baton_t;


/* Stream read func (matches the `svn_read_func_t' type);
   BATON is an `svn_fs__rep_read_baton_t'.

   Read LEN bytes into BUF starting at BATON->offset in the data
   represented by BATON->rep_key, in BATON->FS.  Set *LEN to the
   amount read and add that amount to BATON->offset.  

   If BATON->trail is non-null, then do the read as part of that
   trail, and use the trail's pool for all allocations.  Otherwise,
   do the read in its own internal trail, and use BATON->pool for all
   allocations.  */
svn_error_t *
svn_fs__rep_read_contents (void *baton, char *buf, apr_size_t *len);


/* stabilize_rep */

#endif /* SVN_LIBSVN_FS_REPS_STRINGS_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
