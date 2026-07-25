/*
 * textbase.h: working with text-bases
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

#ifndef SVN_LIBSVN_WC_TEXTBASE_H
#define SVN_LIBSVN_WC_TEXTBASE_H

#include "svn_types.h"
#include "svn_io.h"

#include "wc_db.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Set *CONTENTS_P to a readonly stream containing the text-base contents
 * of the version of the file LOCAL_ABSPATH identified by CHECKSUM in DB.
 * If the CHECKSUM is NULL, return the text-base of the working version
 * of the file.  If the file is locally copied or moved to this path,
 * the text-base will correspond to the copy source, even if the file
 * replaces a previously existing base node at this path.
 *
 * If the file is simply added or replaced and does not have a text-base,
 * set *CONTENTS_P to NULL if IGNORE_ENOENT is true and return an error
 * if IGNORE_ENOENT is false.
 *
 * For working copies that do not store local text-base contents for all
 * files, the function may return a detranslated stream to the contents
 * of the file itself if the file is not modified.  If the file is
 * modified and its text-base contents is not present locally, return
 * an SVN_ERR_WC_PRISTINE_DEHYDRATED error.
 */
svn_error_t *
svn_wc__textbase_get_contents(svn_stream_t **contents_p,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              const svn_checksum_t *checksum,
                              svn_boolean_t ignore_enoent,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/* Set *RESULT_ABSPATH_P to the path of the temporary file containing the
 * text-base contents of the version of the file LOCAL_ABSPATH identified
 * by CHECKSUM in DB.  The returned file will be removed when the RESULT_POOL
 * is cleared.
 *
 * For more detail, see the description of svn_wc__textbase_get_contents().
 */
svn_error_t *
svn_wc__textbase_setaside(const char **result_abspath_p,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          const svn_checksum_t *checksum,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/* Set *RESULT_ABSPATH_P to the path of the temporary file containing the
 * text-base contents of the version of the file LOCAL_ABSPATH identified
 * by CHECKSUM in DB.  Set *CLEANUP_WORK_ITEM_P to a new work item that
 * will remove the temporary file.  Allocate both *RESULT_ABSPATH_P and
 * *CLEANUP_WORK_ITEM_P in RESULT_POOL.
 *
 * For more detail, see the description of svn_wc__textbase_get_contents().
 */
svn_error_t *
svn_wc__textbase_setaside_wq(const char **result_abspath_p,
                             svn_skel_t **cleanup_work_item_p,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             const svn_checksum_t *checksum,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/* Prepare to install the text-base contents for file LOCAL_ABSPATH in DB.
 * If HYDRATED is true, the contents are guaranteed to be kept and available
 * on disk.  If HYDRATED is false, the contents MAY not be saved on disk,
 * but the actual state is a subject to the current working copy state and
 * configuration.
 *
 * For more detail, see the description of svn_wc__db_pristine_prepare_install().
 */
svn_error_t *
svn_wc__textbase_prepare_install(svn_stream_t **stream_p,
                                 svn_wc__db_install_data_t **install_data_p,
                                 svn_checksum_t **sha1_checksum_p,
                                 svn_checksum_t **md5_checksum_p,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 svn_boolean_t hydrated,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_TEXTBASE_H */
