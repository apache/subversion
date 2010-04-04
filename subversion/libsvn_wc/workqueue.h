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
 *
 *
 * Greg says:
 *
 * I think the current items are misdirected
 * work items should NOT touch the DB
 * the work items should be inserted into WORK_QUEUE by wc_db,
 * meaning: workqueue.[ch] should return work items for passing to the wc_db API,
 * which installs them during a transaction with the other work,
 * and those items should *only* make the on-disk state match what is in the database
 * before you rejoined the chan, I was discussing with Bert that I might rejigger the postcommit work,
 * in order to do the prop file handling as work items,
 * and pass those to db_global_commit for insertion as part of its transaction
 * so that once we switch to in-db props, those work items just get deleted,
 * (where they're simple things like: move this file to there, or delete that file)
 * i.e. workqueue should be seriously dumb
 * */

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


/* Build a work item (returned in *WORK_ITEM) that will install the working
   copy file at LOCAL_ABSPATH. If USE_COMMIT_TIMES is TRUE, then the newly
   installed file will use the nodes CHANGE_DATE for the file timestamp.
   If RECORD_FILEINFO is TRUE, then the resulting LAST_MOD_TIME and
   TRANSLATED_SIZE will be recorded in the database.

   If SOURCE_ABSPATH is NULL, then the pristine contents will be installed
   (with appropriate translation). If SOURCE_ABSPATH is not NULL, then it
   specifies a source file for the translation. The file must exist for as
   long as *WORK_ITEM exists (and is queued). Typically, it will be a
   temporary file, and an OP_FILE_REMOVE will be queued to later remove it.
*/
svn_error_t *
svn_wc__wq_build_file_install(const svn_skel_t **work_item,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              const char *source_abspath,
                              svn_boolean_t use_commit_times,
                              svn_boolean_t record_fileinfo,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);


/* Build a work item (returned in *WORK_ITEM) that will remove a single
   file.  */
svn_error_t *
svn_wc__wq_build_file_remove(const svn_skel_t **work_item,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *result_pool,
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

/* Record a work item to remove the "revert props" and "revert text base"
   for LOCAL_ABSPATH.  */
svn_error_t *
svn_wc__wq_remove_revert_files(svn_wc__db_t *db,
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


/* ### temporary compat for mapping the old loggy into workqueue space.

   LOG_CONTENT may be NULL or reference an empty log. No work item will be
   queued in this case.
*/
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
                          const char *tmp_text_base_abspath,
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
                      apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_WORKQUEUE_H */
