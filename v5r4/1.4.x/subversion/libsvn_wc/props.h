/*
 * props.h :  properties
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include "svn_props.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* If the working item at PATH has properties attached, set HAS_PROPS.
   ADM_ACCESS is an access baton set that contains PATH. */
svn_error_t *svn_wc__has_props(svn_boolean_t *has_props,
                               const char *path,
                               svn_wc_adm_access_t *adm_access,
                               apr_pool_t *pool);


/* If PROPFILE_PATH exists (and is a file), assume it's full of
   properties and load this file into HASH.  Otherwise, leave HASH
   untouched.  */
svn_error_t *svn_wc__load_prop_file(const char *propfile_path,
                                    apr_hash_t *hash,
                                    apr_pool_t *pool);



/* Given a HASH full of property name/values, write them to a file
   located at PROPFILE_PATH */
svn_error_t *svn_wc__save_prop_file(const char *propfile_path,
                                    apr_hash_t *hash,
                                    apr_pool_t *pool);


/* Given ADM_ACCESS/NAME and an array of PROPCHANGES based on
   SERVER_BASEPROPS, merge the changes into the working copy.
   Necessary log entries will be appended to ENTRY_ACCUM.

   If SERVER_BASEPROPS is NULL than base props will be used as
   PROPCHANGES base.

   If we are attempting to merge changes to a directory, simply pass
   ADM_ACCESS and NULL for NAME.

   If BASE_MERGE is FALSE only the working properties will be changed,
   if it is TRUE both the base and working properties will be changed.

   If conflicts are found when merging, they are placed into a
   temporary .prej file within SVN. Log entries are then written to
   move this file into PATH, or to append the conflicts to the file's
   already-existing .prej file in ADM_ACCESS. Base properties are modifed
   unconditionally, if BASE_MERGE is TRUE, they do not generate conficts.

   If STATE is non-null, set *STATE to the state of the local properties
   after the merge.  */
svn_error_t *svn_wc__merge_props(svn_wc_notify_state_t *state,
                                 svn_wc_adm_access_t *adm_access,
                                 const char *name,
                                 apr_hash_t *server_baseprops,
                                 const apr_array_header_t *propchanges,
                                 svn_boolean_t base_merge,
                                 svn_boolean_t dry_run,
                                 apr_pool_t *pool,
                                 svn_stringbuf_t **entry_accum);


/* Return a list of wc props for ENTRYNAME in ADM_ACCESS.
   ENTRYNAME must be the name of a file or SVN_WC_ENTRY_THIS_DIR.
   
   The returned WCPROPS may be allocated in POOL, or may be the props
   cached in ADM_ACCESS.  */
svn_error_t *
svn_wc__wcprop_list(apr_hash_t **wcprops,
                    const char *entryname,
                    svn_wc_adm_access_t *adm_access,
                    apr_pool_t *pool);

/* Set a single 'wcprop' NAME to VALUE for versioned object PATH. 
   If VALUE is null, remove property NAME.  ADM_ACCESS is an access
   baton set that contains PATH.

   If FORCE_WRITE is true, then the change will be written to disk
   immediately.  Else, only the in-memory cache (if that is used) will
   be updated and the caller is expected to use
   svn_wc__wcprops_write() later, on the correct access baton, to store
   the change persistently. */
svn_error_t *svn_wc__wcprop_set(const char *name,
                                const svn_string_t *value,
                                const char *path,
                                svn_wc_adm_access_t *adm_access,
                                svn_boolean_t force_write,
                                apr_pool_t *pool);

/* Remove wcprops for entry NAME under ADM_ACCESS, or for all files
   and this_dir if NAME is null.  Recurse into subdirectories if
   RECURSE is true.  Use POOL for temporary allocations. */
svn_error_t *svn_wc__remove_wcprops(svn_wc_adm_access_t *adm_access,
                                    const char *name,
                                    svn_boolean_t recurse,
                                    apr_pool_t *pool);

/* Write the wcprops cached in ADM_ACCESS, if any, to disk using POOL for
   temporary allocations. */
svn_error_t *
svn_wc__wcprops_write(svn_wc_adm_access_t *adm_access, apr_pool_t *pool);


/* Returns TRUE if PROPS contains the svn:special property */
svn_boolean_t svn_wc__has_special_property(apr_hash_t *props);

/* Given PROPERTIES is array of @c svn_prop_t structures. Returns TRUE if any
   of the PROPERTIES are the known "magic" ones that might require
   changing the working file. */
svn_boolean_t svn_wc__has_magic_property(const apr_array_header_t *properties);

/* Extend LOG_ACCUM with log entries to install PROPS and, if WRITE_BASE_PROPS
   is true, BASE_PROPS for the path NAME in ADM_ACCESS, updating the wc entry
   to reflect the changes.  Use POOL for temporary allocations. */
svn_error_t *svn_wc__install_props(svn_stringbuf_t **log_accum,
                                   svn_wc_adm_access_t *adm_access,
                                   const char *name,
                                   apr_hash_t *base_props,
                                   apr_hash_t *props,
                                   svn_boolean_t write_base_props,
                                   apr_pool_t *pool);

/* Load the base and working props for NAME in ADM_ACCESS returning them
   in *BASE_PROPS_P and *PROPS_P, respectively.  BASE_PROPS or PROPS may be null.
   Do all allocations in POOL.  */
svn_error_t *
svn_wc__load_props(apr_hash_t **base_props_p,
                   apr_hash_t **props_p,
                   svn_wc_adm_access_t *adm_access,
                   const char *name,
                   apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_PROPS_H */
