/* reps-table.h : internal interface to `representations' table
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

#ifndef SVN_LIBSVN_FS_REPS_TABLE_H
#define SVN_LIBSVN_FS_REPS_TABLE_H

#include "db.h"
#include "svn_io.h"
#include "svn_fs.h"
#include "trail.h"



/*** Creating the `representations' table. ***/

/* Open a `representations' table in ENV.  If CREATE is non-zero,
   create one if it doesn't exist.  Set *REPS_P to the new table.
   Return a Berkeley DB error code.  */
int svn_fs__open_reps_table (DB **reps_p, DB_ENV *env, int create);



/*** Storing and retrieving reps.  ***/

/* Set *SKEL_P to point to the REPRESENTATION skel for the key KEY in
   FS, as part of TRAIL.  Allocate the skel and the data it points
   into in TRAIL->pool.

   If KEY is not a representation in FS, the error
   SVN_ERR_FS_NO_SUCH_REPRESENTATION is returned.  */
svn_error_t *svn_fs__read_rep (skel_t **skel_p,
                               svn_fs_t *fs,
                               const char *key,
                               trail_t *trail);


/* Store SKEL as the representation for KEY in FS, as part of
   TRAIL.  Do any necessary temporary allocation in TRAIL->pool.  */
svn_error_t *svn_fs__write_rep (svn_fs_t *fs,
                                const char *key,
                                skel_t *skel,
                                trail_t *trail);


/* Store SKEL as a new representation in FS, and the new rep's key in
   *KEY, as part of trail.  The new key is allocated in TRAIL->pool.  */
svn_error_t *svn_fs__write_new_rep (const char **key,
                                    svn_fs_t *fs,
                                    skel_t *skel,
                                    trail_t *trail);

/* Delete representation KEY from FS, as part of TRAIL.
   WARNING: This does not ensure that no one references this
   representation!  Callers should ensure that themselves.  */
svn_error_t *svn_fs__delete_rep (svn_fs_t *fs,
                                 const char *key,
                                 trail_t *trail);



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
   KEY in FS, and store it in *NEW_KEY.  If KEY is already mutable,
   *NEW_KEY is set to KEY, else *NEW_KEY is set to a new rep key
   allocated in TRAIL->pool.  */
svn_error_t *svn_fs__get_mutable_rep (const char **new_key,
                                      const char *key,
                                      svn_fs_t *fs, 
                                      trail_t *trail);

/* stabilize_rep */

#endif /* SVN_LIBSVN_FS_REPS_TABLE_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
