/*
 * workqueue.h :  manipulating work queue items
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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_WORKQUEUE_H */
