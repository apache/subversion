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


/* Returns TRUE if WI refers to a single work item. Returns FALSE if
   WI is a list of work items. WI must not be NULL.

   A work item looks like: (OP_CODE arg1 arg2 ...)

   If we see OP_CODE (an atom) as WI's first child, then this is a
   single work item. Otherwise, it is a list of work items.  */
#define SVN_WC__SINGLE_WORK_ITEM(wi) ((wi)->children->is_atom)


/* Combine WORK_ITEM1 and WORK_ITEM2 into a single, resulting work item.

   Each of the WORK_ITEM parameters may have one of three values:

     NULL                          no work item
     (OPCODE arg1 arg2 ...)        single work item
     ((OPCODE ...) (OPCODE ...))   multiple work items

   These will be combined as appropriate, and returned in one of the
   above three styles.

   The resulting list will be ordered: WORK_ITEM1 first, then WORK_ITEM2  */
svn_skel_t *
svn_wc__wq_merge(svn_skel_t *work_item1,
                 svn_skel_t *work_item2,
                 apr_pool_t *result_pool);


/* For the WCROOT identified by the DB and WRI_ABSPATH pair, run any
   work items that may be present in its workqueue.  */
svn_error_t *
svn_wc__wq_run(svn_wc__db_t *db,
               const char *wri_abspath,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool);


/* Set *WORK_ITEM to a new work item that will install the working
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
svn_wc__wq_build_file_install(svn_skel_t **work_item,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              const char *source_abspath,
                              svn_boolean_t use_commit_times,
                              svn_boolean_t record_fileinfo,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);


/* Set *WORK_ITEM to a new work item that will remove a single
   file.  */
svn_error_t *
svn_wc__wq_build_file_remove(svn_skel_t **work_item,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/* Set *WORK_ITEM to a new work item that describes a moves of
   a file or directory from SRC_ABSPATH to DST_ABSPATH, ready for
   storing in the working copy managing DST_ABSPATH.

   Perform temporary allocations in SCRATCH_POOL and *WORK_ITEM in
   RESULT_POOL.
*/
svn_error_t *
svn_wc__wq_build_file_move(svn_skel_t **work_item,
                           svn_wc__db_t *db,
                           const char *src_abspath,
                           const char *dst_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Set *WORK_ITEM to a new work item that describes a copy from
   SRC_ABSPATH to DST_ABSPATH, while translating the stream using
   the information from LOCAL_ABSPATH. */
svn_error_t *
svn_wc__wq_build_file_copy_translated(svn_skel_t **work_item,
                                      svn_wc__db_t *db,
                                      const char *local_abspath,
                                      const char *src_abspath,
                                      const char *dst_abspath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);


/* Set *WORK_ITEM to a new work item that will synchronize the
   target node's readonly and executable flags with the values defined
   by its properties and lock status.  */
svn_error_t *
svn_wc__wq_build_sync_file_flags(svn_skel_t **work_item,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);


/* Set *WORK_ITEM to a new work item that will install a property reject
   file for LOCAL_ABSPATH into the working copy. The propety conflicts will
   be taken from CONFLICT_SKEL, or if NULL, then from wc_db for the
   given DB/LOCAL_ABSPATH.  */
svn_error_t *
svn_wc__wq_build_prej_install(svn_skel_t **work_item,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              const svn_skel_t *conflict_skel,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/* Set *WORK_ITEM to a new work item that will record file information of
   LOCAL_ABSPATH into the TRANSLATED_SIZE and LAST_MOD_TIME of the node via
   the svn_wc__db_global_record_fileinfo() function.

   If SET_TIME is not 0, set LOCAL_ABSPATH's last modified time to this
   time and after that record the actual file time.

   ### it is unclear whether this should survive.  */
svn_error_t *
svn_wc__wq_build_record_fileinfo(svn_skel_t **work_item,
                                 const char *local_abspath,
                                 apr_time_t set_time,
                                 apr_pool_t *result_pool);


/* Record a work item to revert LOCAL_ABSPATH.  */
svn_error_t *
svn_wc__wq_add_revert(svn_boolean_t *will_revert,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      svn_boolean_t use_commit_times,
                      apr_pool_t *scratch_pool);

/* Set *WORK_ITEM to a new work item that will remove all the data of
   the BASE_NODE of LOCAL_ABSPATH and all it's descendants, but keeping
   any WORKING_NODE data.

   ### This is only used from update_editor.c's do_entry_deletion() and
   ### the current implementation doesn't check if it removes more than
   ### just this documented behavior. (It is just a copy of the old loggy
   ### code)
 */
svn_error_t *
svn_wc__wq_build_base_remove(svn_skel_t **work_item,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             svn_boolean_t keep_not_present,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

#ifndef SVN_WC__SINGLE_DB
/* Handle the old "KILLME" concept -- perform the actual deletion of a
   subdir (or just its admin area) during post-commit processing of a
   deleted subdir.  */
svn_error_t *
svn_wc__wq_add_killme(svn_wc__db_t *db,
                      const char *adm_abspath,
                      svn_boolean_t adm_only,
                      apr_pool_t *scratch_pool);
#endif


/* ### temporary compat for mapping the old loggy into workqueue space.

   Set *WORK_ITEM to a new work item ...

   LOG_CONTENT may be NULL or reference an empty log.  Set *WORK_ITEM to
   NULL in this case.

   NOTE: ADM_ABSPATH and LOG_CONTENT must live at least as long as
   RESULT_POOL (typically, they'll be allocated within RESULT_POOL).
*/
svn_error_t *
svn_wc__wq_build_loggy(svn_skel_t **work_item,
                       svn_wc__db_t *db,
                       const char *adm_abspath,
                       const svn_stringbuf_t *log_content,
                       apr_pool_t *result_pool);


/* ### Temporary helper to store text conflict marker locations as a wq
   ### operation. Eventually the data must be stored in the pristine store+db
   ### before the wq runs (within the operation transaction) and then a wq
   ### operation will create the markers.

   Set *WORK_ITEM to a new work item that sets the conflict marker values
   on ACTUAL_NODE to the passed values or to NULL if NULL is passed.

   Allocate the result in RESULT_POOL and perform temporary allocations
   in SCRATCH_POOL
*/
svn_error_t *
svn_wc__wq_tmp_build_set_text_conflict_markers(svn_skel_t **work_item,
                                               svn_wc__db_t *db,
                                               const char *local_abspath,
                                               const char *old_basename,
                                               const char *new_basename,
                                               const char *wrk_basename,
                                               apr_pool_t *result_pool,
                                               apr_pool_t *scratch_pool);

/* ### Temporary helper to store the property conflict marker location as a wq
   ### operation. Eventually the data must be stored in the pristine store+db
   ### before the wq runs (within the operation transaction) and then a wq
   ### operation will create the marker.

   Set *WORK_ITEM to a new work item that sets the conflict marker values
   on ACTUAL_NODE to the passed values or to NULL if NULL is passed.

   Allocate the result in RESULT_POOL and perform temporary allocations
   in SCRATCH_POOL
*/
svn_error_t *
svn_wc__wq_tmp_build_set_property_conflict_marker(svn_skel_t **work_item,
                                                  svn_wc__db_t *db,
                                                  const char *local_abspath,
                                                  const char *prej_basename,
                                                  apr_pool_t *result_pool,
                                                  apr_pool_t *scratch_pool);

/* Set *WORK_ITEM to a new work item that will create the file NEW_ABSPATH
 * with the pristine text identified by PRISTINE_SHA1, translated into
 * working-copy form according to the versioned properties of
 * VERSIONED_ABSPATH that are current when the work item is executed.  The
 * work item will overwrite NEW_ABSPATH if that already exists. */
svn_error_t *
svn_wc__wq_build_pristine_get_translated(svn_skel_t **work_item,
                                         svn_wc__db_t *db,
                                         const char *versioned_abspath,
                                         const char *new_abspath,
                                         const svn_checksum_t *pristine_sha1,
                                         apr_pool_t *result_pool,
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
                          svn_revnum_t changed_rev,
                          apr_time_t changed_date,
                          const char *changed_author,
                          const svn_checksum_t *new_checksum,
                          apr_hash_t *new_dav_cache,
                          svn_boolean_t keep_changelist,
                          svn_boolean_t no_unlock,
                          apr_pool_t *scratch_pool);


/* See props.h  */
#ifdef SVN__SUPPORT_BASE_MERGE
svn_error_t *
svn_wc__wq_add_install_properties(svn_wc__db_t *db,
                                  const char *local_abspath,
                                  apr_hash_t *pristine_props,
                                  apr_hash_t *actual_props,
                                  apr_pool_t *scratch_pool);
#endif


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_WORKQUEUE_H */
