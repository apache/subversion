/*
 * props.h :  properties
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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


/* Given LOCAL_ABSPATH/DB and an array of PROPCHANGES based on
   SERVER_BASEPROPS, merge the changes into the working copy.
   Append all necessary log entries except the property changes to
   ENTRY_ACCUM. Return the new property collections to the caller
   via NEW_BASE_PROPS and NEW_ACTUAL_PROPS, so the caller can combine
   the property update with other operations.

   If BASE_PROPS or WORKING_PROPS is NULL, use the props from the
   working copy.

   If SERVER_BASEPROPS is NULL then use base props as PROPCHANGES
   base.

   If BASE_MERGE is FALSE then only change working properties;
   if TRUE, change both the base and working properties.

   If conflicts are found when merging, place them into a temporary
   .prej file, and write log commands to move this file into LOCAL_ABSPATH's
   parent directory, or append the conflicts to the file's already-existing
   .prej file.  Modify base properties unconditionally,
   if BASE_MERGE is TRUE, they do not generate conficts.

   TODO ### LEFT_VERSION and RIGHT_VERSION ...

   TODO ### DRY_RUN ...

   TODO ### CONFLICT_FUNC/CONFLICT_BATON ...

   If STATE is non-null, set *STATE to the state of the local properties
   after the merge.  */
svn_error_t *
svn_wc__merge_props(svn_stringbuf_t **entry_accum,
                    svn_wc_notify_state_t *state,
                    apr_hash_t **new_base_props,
                    apr_hash_t **new_actual_props,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    const svn_wc_conflict_version_t *left_version,
                    const svn_wc_conflict_version_t *right_version,
                    apr_hash_t *server_baseprops,
                    apr_hash_t *base_props,
                    apr_hash_t *working_props,
                    const apr_array_header_t *propchanges,
                    svn_boolean_t base_merge,
                    svn_boolean_t dry_run,
                    svn_wc_conflict_resolver_func_t conflict_func,
                    void *conflict_baton,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);


/* Set a single 'wcprop' NAME to VALUE for versioned object LOCAL_ABSPATH.
   If VALUE is null, remove property NAME.  */
svn_error_t *svn_wc__wcprop_set(svn_wc__db_t *db,
                                const char *local_abspath,
                                const char *name,
                                const svn_string_t *value,
                                apr_pool_t *scratch_pool);

/* Given PROPERTIES is array of @c svn_prop_t structures. Returns TRUE if any
   of the PROPERTIES are the known "magic" ones that might require
   changing the working file. */
svn_boolean_t svn_wc__has_magic_property(const apr_array_header_t *properties);

/* Add a working queue item to install PROPS and, if INSTALL_PRISTINE_PROPS is
   true, BASE_PROPS for the LOCAL_ABSPATH in DB, updating the node to reflect
   the changes.  PRISTINE_PROPS must be supplied even if INSTALL_PRISTINE_PROPS
   is false. If FORCE_BASE_INSTALL properties are always installed in BASE_NODE,
   even though WORKING is used as pristine for the current node.
   Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_wc__install_props(svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_hash_t *pristine_props,
                      apr_hash_t *props,
                      svn_boolean_t install_pristine_props,
                      svn_boolean_t force_base_install,
                      apr_pool_t *scratch_pool);

/* Extend LOG_ACCUM with log entries to save the current baseprops of PATH
   as revert props.

   Makes sure the baseprops are destroyed if DESTROY_BASEPROPS is TRUE,
   the baseprops are preserved otherwise.
*/
svn_error_t *
svn_wc__loggy_revert_props_create(svn_stringbuf_t **log_accum,
                                  svn_wc__db_t *db,
                                  const char *local_abspath,
                                  const char *adm_abspath,
                                  apr_pool_t *pool);

/* Extends LOG_ACCUM to make the revert props back into base props,
   deleting the revert props. */
svn_error_t *
svn_wc__loggy_revert_props_restore(svn_stringbuf_t **log_accum,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   const char *adm_abspath,
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

/* Install LOCAL_ABSPATHs working props as base props. */
svn_error_t *
svn_wc__working_props_committed(svn_wc__db_t *db,
                                const char *local_abspath,
                                apr_pool_t *scratch_pool);

/* Load the base and working props for ENTRY at PATH returning
   them in *BASE_PROPS_P and *PROPS_P respectively.
   Any of BASE_PROPS and PROPS may be NULL.
   Returned hashes/values are allocated in RESULT_POOL. All temporary
   allocations are made in SCRATCH_POOL.  */
svn_error_t *
svn_wc__load_props(apr_hash_t **base_props_p,
                   apr_hash_t **props_p,
                   svn_wc__db_t *db,
                   const char *local_abspath,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool);

/* Load the revert props for ENTRY at PATH returning them in *REVERT_PROPS_P.
   Returned hash/values are allocated in RESULT_POOL. All temporary
   allocations are made in SCRATCH_POOL.  */
svn_error_t *
svn_wc__load_revert_props(apr_hash_t **revert_props_p,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

svn_error_t *
svn_wc__marked_as_binary(svn_boolean_t *marked,
                         const char *local_abspath,
                         svn_wc__db_t *db,
                         apr_pool_t *scratch_pool);


/* Temporary helper for determining where to store pristine properties.
   All calls will eventually be replaced by direct wc_db operations
   of the right type. */
svn_error_t *
svn_wc__prop_pristine_is_working(svn_boolean_t *working,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_PROPS_H */
