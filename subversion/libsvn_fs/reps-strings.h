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




/*** Stream-based reading. ***/

/* A baton type for representation read streams.  */
typedef struct svn_fs__rep_read_baton_t svn_fs__rep_read_baton_t;


/* Return a rep reading baton to use with the svn stream reading
   function svn_fs__rep_read_contents().  The baton is allocated in
   POOL.

   The baton is for reading representation REP_KEY's data starting at
   OFFSET in FS, doing temporary allocations in POOL.  If TRAIL is
   non-null, do the stream's reads as part of TRAIL; otherwise, each
   read happens in an internal, one-off trail. 

   POOL may be TRAIL->pool.  */
svn_fs__rep_read_baton_t *svn_fs__rep_read_get_baton (svn_fs_t *fs,
                                                      const char *rep_key,
                                                      apr_size_t offset,
                                                      trail_t *trail,
                                                      apr_pool_t *pool);


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
