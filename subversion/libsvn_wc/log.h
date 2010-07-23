/*
 * log.h :  interfaces for running .svn/log files.
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


#ifndef SVN_LIBSVN_WC_LOG_H
#define SVN_LIBSVN_WC_LOG_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_wc.h"

#include "wc_db.h"
#include "private/svn_skel.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Common arguments:

   Each svn_wc__loggy_* function in this section takes an ADM_ABSPATH
   argument which is the working copy directory inside which the operation
   is to be performed.  Any other *_ABSPATH arguments must be paths inside
   ADM_ABSPATH (or equal to it, where that makes sense).
*/

/* Set *WORK_ITEM to a work queue instruction to delete the entry
   associated with LOCAL_ABSPATH from the entries file.

   REVISION and KIND are used to insert a "not present" base node row if
   REVISION is not SVN_INVALID_REVNUM: see svn_wc__db_base_add_absent_node()
   for details.

   ADM_ABSPATH is described above.
*/
svn_error_t *
svn_wc__loggy_delete_entry(svn_skel_t **work_item,
                           svn_wc__db_t *db,
                           const char *adm_abspath,
                           const char *local_abspath,
                           svn_revnum_t revision,
                           svn_wc__db_kind_t kind,
                           apr_pool_t *result_pool);

/* Set *WORK_ITEM to a work queue instruction to
   ### ...

   ADM_ABSPATH is described above.
*/
svn_error_t *
svn_wc__loggy_add_tree_conflict(svn_skel_t **work_item,
                                svn_wc__db_t *db,
                                const char *adm_abspath,
                                const svn_wc_conflict_description2_t *conflict,
                                apr_pool_t *result_pool);


/* TODO ###

   Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_wc__run_xml_log(svn_wc__db_t *db,
                    const char *adm_abspath,
                    const char *log_contents,
                    apr_size_t log_len,
                    apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_LOG_H */
