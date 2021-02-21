/*
 * working_file_writer.h :  utility to prepare and install working files
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

#ifndef SVN_LIBSVN_WC_WORKING_FILE_WRITER_H
#define SVN_LIBSVN_WC_WORKING_FILE_WRITER_H

#include <apr_pools.h>
#include "svn_types.h"
#include "svn_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Context of the working file writer. */
typedef struct svn_wc__working_file_writer_t svn_wc__working_file_writer_t;

/* Create a write context for the (provisioned) working file with the specified
   properties.  The file writer must be given data in the repository-normal
   form and will handle its translation according to the specified properties.
   Place the temporary file in the TMP_ABSPATH directory.  If FINAL_MTIME is
   non-negative, it will be set as the last modification time on the installed
   file. */
svn_error_t *
svn_wc__working_file_writer_open(svn_wc__working_file_writer_t **writer_p,
                                 const char *tmp_abspath,
                                 apr_time_t final_mtime,
                                 apr_hash_t *props,
                                 svn_revnum_t changed_rev,
                                 apr_time_t changed_date,
                                 const char *changed_author,
                                 svn_boolean_t has_lock,
                                 svn_boolean_t is_added,
                                 const char *repos_root_url,
                                 const char *repos_relpath,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Get the writable stream for WRITER.  The returned stream supports reset
   and is configured to be truncated on seek. */
svn_stream_t *
svn_wc__working_file_writer_get_stream(svn_wc__working_file_writer_t *writer);

/* Finalize the content, attributes and the timestamps of the underlying
   temporary file.  Return the properties of the finalized file in MTIME_P
   and SIZE_P.  MTIME_P and SIZE_P both may be NULL. */
svn_error_t *
svn_wc__working_file_writer_finalize(apr_time_t *mtime_p,
                                     apr_off_t *size_p,
                                     svn_wc__working_file_writer_t *writer,
                                     apr_pool_t *scratch_pool);

/* Atomically install the contents of WRITER to TARGET_ABSPATH.
   If the writer has not been previously finalized with a call to
   svn_wc__working_file_writer_finalize(), the behavior is undefined. */
svn_error_t *
svn_wc__working_file_writer_install(svn_wc__working_file_writer_t *writer,
                                    const char *target_abspath,
                                    apr_pool_t *scratch_pool);

/* Cleanup WRITER by closing and removing the underlying file. */
svn_error_t *
svn_wc__working_file_writer_close(svn_wc__working_file_writer_t *writer);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_WORKING_FILE_WRITER_H */
