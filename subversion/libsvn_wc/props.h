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

typedef enum svn_wc__props_kind_t
{
  svn_wc__props_base = 0,
  svn_wc__props_revert,
  svn_wc__props_wcprop,
  svn_wc__props_working
} svn_wc__props_kind_t;


/* If the working item at PATH has properties attached, set HAS_PROPS.
   ADM_ACCESS is an access baton set that contains PATH. */
svn_error_t *svn_wc__has_props(svn_boolean_t *has_props,
                               const char *path,
                               svn_wc_adm_access_t *adm_access,
                               apr_pool_t *pool);

/* Given ADM_ACCESS/PATH and an array of PROPCHANGES based on
   SERVER_BASEPROPS, merge the changes into the working copy.
   Append all necessary log entries to ENTRY_ACCUM.

   If BASE_PROPS or WORKING_PROPS is NULL, use the props from the
   working copy.

   If SERVER_BASEPROPS is NULL then use base props as PROPCHANGES
   base.

   If BASE_MERGE is FALSE then only change working properties;
   if TRUE, change both the base and working properties.

   If conflicts are found when merging, place them into a temporary
   .prej file within SVN, and write log commands to move this file
   into PATH, or append the conflicts to the file's already-existing
   .prej file in ADM_ACCESS.  Modify base properties unconditionally,
   if BASE_MERGE is TRUE, they do not generate conficts.

   If STATE is non-null, set *STATE to the state of the local properties
   after the merge.  */
svn_error_t *svn_wc__merge_props(svn_wc_notify_state_t *state,
                                 svn_wc_adm_access_t *adm_access,
                                 const char *path,
                                 apr_hash_t *server_baseprops,
                                 apr_hash_t *base_props,
                                 apr_hash_t *working_props,
                                 const apr_array_header_t *propchanges,
                                 svn_boolean_t base_merge,
                                 svn_boolean_t dry_run,
                                 svn_wc_conflict_resolver_func_t conflict_func,
                                 void *conflict_baton,
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

/* Returns TRUE if PROPS contains the svn:special property */
svn_boolean_t svn_wc__has_special_property(apr_hash_t *props);

/* Given PROPERTIES is array of @c svn_prop_t structures. Returns TRUE if any
   of the PROPERTIES are the known "magic" ones that might require
   changing the working file. */
svn_boolean_t svn_wc__has_magic_property(const apr_array_header_t *properties);

/* Extend LOG_ACCUM with log entries to install PROPS and, if WRITE_BASE_PROPS
   is true, BASE_PROPS for the PATH in ADM_ACCESS, updating the wc entry
   to reflect the changes.  BASE_PROPS must be supplied even if
   WRITE_BASE_PROPS is false.  Use POOL for temporary allocations. */
svn_error_t *svn_wc__install_props(svn_stringbuf_t **log_accum,
                                   svn_wc_adm_access_t *adm_access,
                                   const char *path,
                                   apr_hash_t *base_props,
                                   apr_hash_t *props,
                                   svn_boolean_t write_base_props,
                                   apr_pool_t *pool);

/* Extend LOG_ACCUM with log entries to save the current baseprops of PATH
   as revert props.

   Makes sure the baseprops are destroyed if DESTROY_BASEPROPS is TRUE,
   the baseprops are preserved otherwise.
*/
svn_error_t *
svn_wc__loggy_revert_props_create(svn_stringbuf_t **log_accum,
                                  const char *path,
                                  svn_wc_adm_access_t *adm_access,
                                  svn_boolean_t destroy_baseprops,
                                  apr_pool_t *pool);

/* Extends LOG_ACCUM to make the revert props back into base props,
   deleting the revert props. */
svn_error_t *
svn_wc__loggy_revert_props_restore(svn_stringbuf_t **log_accum,
                                   const char *path,
                                   svn_wc_adm_access_t *adm_access,
                                   apr_pool_t *pool);

/* Extends LOG_ACCUM to delete PROPS_KIND props installed for PATH. */
svn_error_t *
svn_wc__loggy_props_delete(svn_stringbuf_t **log_accum,
                           const char *path,
                           svn_wc__props_kind_t props_kind,
                           svn_wc_adm_access_t *adm_access,
                           apr_pool_t *pool);

/* Delete PROPS_KIND props for PATH */
svn_error_t *
svn_wc__props_delete(const char *path,
                     svn_wc__props_kind_t props_kind,
                     svn_wc_adm_access_t *adm_access,
                     apr_pool_t *pool);


/* Flushes wcprops cached in ADM_ACCESS to disk using SCRATCH_POOL for
   temporary allocations. */
svn_error_t *
svn_wc__wcprops_flush(svn_wc_adm_access_t *adm_access,
                      apr_pool_t *scratch_pool);

/* Install PATHs working props as base props, clearing the
   has_prop_mods cache value in the entries file.

   Updates the on-disk entries file if SYNC_ENTRIES is TRUE.*/
svn_error_t *
svn_wc__working_props_committed(const char *path,
                                svn_wc_adm_access_t *adm_access,
                                svn_boolean_t sync_entries,
                                apr_pool_t *pool);

/* Return in *MOD_TIME the time at which PROPS_KIND props of PATH
   were last modified, or 0 (zero) if unknown. */
svn_error_t *
svn_wc__props_last_modified(apr_time_t *mod_time,
                            const char *path,
                            svn_wc__props_kind_t props_kind,
                            svn_wc_adm_access_t *adm_access,
                            apr_pool_t *pool);

/* Load the base, working and revert props for PATH in ADM_ACCESS returning
   them in *BASE_PROPS_P, *PROPS_P and *REVERT_PROPS_P respectively.
   Any of BASE_PROPS, PROPS and REVERT_PROPS may be null.
   Do all allocations in POOL.  */
svn_error_t *
svn_wc__load_props(apr_hash_t **base_props_p,
                   apr_hash_t **props_p,
                   apr_hash_t **revert_props_p,
                   svn_wc_adm_access_t *adm_access,
                   const char *path,
                   apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_PROPS_H */
