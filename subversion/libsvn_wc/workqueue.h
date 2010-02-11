/*
 * workqueue.h :  manipulating work queue items
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

#ifndef SVN_WC_WORKQUEUE_H
#define SVN_WC_WORKQUEUE_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_wc.h"

#include "wc_db.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* For the WCROOT identified by the DB and WRI_ABSPATH pair, run any
   work items that may be present in its workqueue.  */
svn_error_t *
svn_wc__wq_run(svn_wc__db_t *db,
               const char *wri_abspath,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool);


/* Record a work item to revert LOCAL_ABSPATH.  */
svn_error_t *
svn_wc__wq_add_revert(svn_boolean_t *will_revert,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      svn_boolean_t use_commit_times,
                      apr_pool_t *scratch_pool);


/* Record a work item to prepare the "revert props" and "revert text base"
   for LOCAL_ABSPATH.  */
svn_error_t *
svn_wc__wq_prepare_revert_files(svn_wc__db_t *db,
                                const char *local_abspath,
                                apr_pool_t *scratch_pool);


/* Handle the old "KILLME" concept -- perform the actual deletion of a
   subdir (or just its admin area) during post-commit processing of a
   deleted subdir.  */
svn_error_t *
svn_wc__wq_add_killme(svn_wc__db_t *db,
                      const char *adm_abspath,
                      svn_boolean_t adm_only,
                      apr_pool_t *scratch_pool);


/* ### temporary compat for mapping the old loggy into workqueue space.  */
svn_error_t *
svn_wc__wq_add_loggy(svn_wc__db_t *db,
                     const char *adm_abspath,
                     const svn_stringbuf_t *log_content,
                     apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__wq_add_deletion_postcommit(svn_wc__db_t *db,
                                   const char *local_abspath,
                                   svn_revnum_t new_revision,
                                   svn_boolean_t no_unlock,
                                   apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__wq_add_postcommit(svn_wc__db_t *db,
                          const char *local_abspath,
                          svn_revnum_t new_revision,
                          apr_time_t new_date,
                          const char *new_author,
                          const svn_checksum_t *new_checksum,
                          apr_hash_t *new_dav_cache,
                          svn_boolean_t keep_changelist,
                          apr_pool_t *scratch_pool);

svn_error_t *
svn_wc__wq_add_install_properties(svn_wc__db_t *db,
                                  const char *local_abspath,
                                  apr_hash_t *pristine_props,
                                  apr_hash_t *actual_props,
                                  svn_boolean_t force_base_install,
                                  apr_pool_t *scratch_pool);

/* Add a work item to delete a node.

   ### LOCAL_ABSPATH is the node to be deleted and the queue exists in
   PARENT_ABSPATH (because when LOCAL_ABSPATH is a directory it might
   not exist on disk).  This use of PARENT_ABSPATH is inherited from
   the log file conversion but perhaps we don't need to use a work
   queue when deleting a directory that does not exist on disk.
 */
svn_error_t *
svn_wc__wq_add_delete(svn_wc__db_t *db,
                      const char *parent_abspath,
                      const char *local_abspath,
                      svn_wc__db_kind_t kind,
                      svn_boolean_t was_added,
                      svn_boolean_t was_copied,
                      svn_boolean_t was_replaced,
                      svn_boolean_t base_shadowed,
                      apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_WORKQUEUE_H */
