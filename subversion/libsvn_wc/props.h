/*
 * props.h :  properties
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


#ifndef SVN_LIBSVN_WC_PROPS_H
#define SVN_LIBSVN_WC_PROPS_H

#include <apr_pools.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* If the working item at PATH has properties attached, set HAS_PROPS. */
svn_error_t *svn_wc__has_props (svn_boolean_t *has_props,
                                const char *path,
                                apr_pool_t *pool);



/* Given two propchange objects, return TRUE iff they conflict.  If
   there's a conflict, DESCRIPTION will contain an english description
   of the problem. */

/* For note, here's the table being implemented:

              |  update set     |    update delete   |
  ------------|-----------------|--------------------|
  user set    | conflict iff    |      conflict      |
              |  vals differ    |                    |
  ------------|-----------------|--------------------|
  user delete |   conflict      |      merge         |
              |                 |    (no problem)    |
  ----------------------------------------------------

*/
svn_boolean_t
svn_wc__conflicting_propchanges_p (const svn_string_t **description,
                                   const svn_prop_t *local,
                                   const svn_prop_t *update,
                                   apr_pool_t *pool);

/* Look up the entry NAME within PATH and see if it has a `current'
   reject file describing a state of conflict.  If such a file exists,
   return the name of the file in REJECT_FILE.  If no such file exists,
   return (REJECT_FILE = NULL). */
svn_error_t *
svn_wc__get_existing_prop_reject_file (const char **reject_file,
                                       const char *path,
                                       const char *name,
                                       apr_pool_t *pool);

/* If PROPFILE_PATH exists (and is a file), assume it's full of
   properties and load this file into HASH.  Otherwise, leave HASH
   untouched.  */
svn_error_t *svn_wc__load_prop_file (const char *propfile_path,
                                     apr_hash_t *hash,
                                     apr_pool_t *pool);



/* Given a HASH full of property name/values, write them to a file
   located at PROPFILE_PATH */
svn_error_t *svn_wc__save_prop_file (const char *propfile_path,
                                     apr_hash_t *hash,
                                     apr_pool_t *pool);


/* Given PATH/NAME and an array of PROPCHANGES, merge the changes into
   the working copy.  Necessary log entries will be appended to
   ENTRY_ACCUM.

   If we are attempting to merge changes to a directory, simply pass
   the directory as PATH and NULL for NAME.

   If conflicts are found when merging, they are placed into a
   temporary .prej file within SVN. Log entries are then written to
   move this file into PATH, or to append the conflicts to the file's
   already-existing .prej file in PATH.

   If STATE is non-null, set *STATE to the state of the local properties
   after the merge.

   Any conflicts are also returned in a hash that maps (const char *)
   propnames -> conflicting (const svn_prop_t *) ptrs from the PROPCHANGES
   array.  In this case, *CONFLICTS will be allocated in POOL.  If no
   conflicts occurred, then *CONFLICTS is simply allocated as an empty
   hash.
*/
svn_error_t *svn_wc__merge_prop_diffs (svn_wc_notify_state_t *state,
                                       apr_hash_t **conflicts,
                                       const char *path,
                                       const char *name,
                                       const apr_array_header_t *propchanges,
                                       apr_pool_t *pool,
                                       svn_stringbuf_t **entry_accum);


/* Get a single 'wcprop' NAME for versioned object PATH, return in
   *VALUE. */
svn_error_t *svn_wc__wcprop_get (const svn_string_t **value,
                                 const char *name,
                                 const char *path,
                                 apr_pool_t *pool);

/* Set a single 'wcprop' NAME to VALUE for versioned object PATH. 
   If VALUE is null, remove property NAME. */
svn_error_t *svn_wc__wcprop_set (const char *name,
                                 const svn_string_t *value,
                                 const char *path,
                                 apr_pool_t *pool);

/* Remove all wc properties under PATH, recursively.  Do any temporary
   allocation in POOL.  If PATH is not a directory, return the error
   SVN_ERR_WC_NOT_DIRECTORY. */
svn_error_t *svn_wc__remove_wcprops (const char *path, apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_PROPS_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
