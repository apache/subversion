/*
 * props.h :  properties
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */


#ifndef SVN_LIBSVN_WC_PROPS_H
#define SVN_LIBSVN_WC_PROPS_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_props.h"

#include "wc_db.h"

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


/* If the working item at PATH has properties attached, set HAS_PROPS. */
svn_error_t *svn_wc__has_props(svn_boolean_t *has_props,
                               svn_wc__db_t *db,
                               const char *local_abspath,
                               apr_pool_t *pool);


/* Internal function for diffing props. */
svn_error_t *
svn_wc__internal_propdiff(apr_array_header_t **propchanges,
                          apr_hash_t **original_props,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);


/* Internal function for fetching a property.  */
svn_error_t *
svn_wc__internal_propget(const svn_string_t **value,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *name,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);


/* Internal function for setting a property.  */
svn_error_t *
svn_wc__internal_propset(svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *name,
                         const svn_string_t *value,
                         svn_boolean_t skip_checks,
                         svn_wc_notify_func2_t notify_func,
                         void *notify_baton,
                         apr_pool_t *scratch_pool);


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

/* Set a single 'wcprop' NAME to VALUE for versioned object LOCAL_ABSPATH.
   If VALUE is null, remove property NAME.  */
svn_error_t *svn_wc__wcprop_set(svn_wc__db_t *db,
                                const char *local_abspath,
                                const char *name,
                                const svn_string_t *value,
                                apr_pool_t *scratch_pool);

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

/* Delete PROPS_KIND props for LOCAL_ABSPATH */
svn_error_t *
svn_wc__props_delete(svn_wc__db_t *db,
                     const char *local_abspath,
                     svn_wc__props_kind_t props_kind,
                     apr_pool_t *pool);

/* Set *MODIFIED_P TRUE if the props for LOCAL_ABSPATH have been modified. */
svn_error_t *
svn_wc__props_modified(svn_boolean_t *modified_p,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool);

/* Flushes wcprops cached in ADM_ACCESS to disk using SCRATCH_POOL for
   temporary allocations. */
svn_error_t *
svn_wc__wcprops_flush(svn_wc_adm_access_t *adm_access,
                      apr_pool_t *scratch_pool);

/* Install PATHs working props as base props. */
svn_error_t *
svn_wc__working_props_committed(const char *path,
                                svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool);

/* Load the base, working and revert props for ENTRY at PATH returning
   them in *BASE_PROPS_P, *PROPS_P and *REVERT_PROPS_P respectively.
   Any of BASE_PROPS, PROPS and REVERT_PROPS may be NULL.
   Returned hashes/values are allocated in RESULT_POOL. All temporary
   allocations are made in SCRATCH_POOL.  */
svn_error_t *
svn_wc__load_props(apr_hash_t **base_props_p,
                   apr_hash_t **props_p,
                   apr_hash_t **revert_props_p,
                   svn_wc__db_t *db,
                   const char *local_abspath,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__marked_as_binary(svn_boolean_t *marked,
                         const char *local_abspath,
                         svn_wc__db_t *db,
                         apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_PROPS_H */
