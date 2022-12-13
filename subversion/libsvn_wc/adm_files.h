/*
 * adm_files.h :  handles locations inside the wc adm area
 *                (This should be the only code that actually knows
 *                *where* things are in .svn/.  If you can't get to
 *                something via these interfaces, something's wrong.)
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


#ifndef SVN_LIBSVN_WC_ADM_FILES_H
#define SVN_LIBSVN_WC_ADM_FILES_H

#include <apr_pools.h>
#include "svn_types.h"

#include "props.h"
#include "wc_db.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Return a path to CHILD in the administrative area of PATH. If CHILD is
   NULL, then the path to the admin area is returned. The result is
   allocated in RESULT_POOL. */
const char *svn_wc__adm_child(const char *path,
                              const char *child,
                              apr_pool_t *result_pool);

/* Return TRUE if the administrative area exists for this directory. */
svn_boolean_t svn_wc__adm_area_exists(const char *adm_abspath,
                                      apr_pool_t *pool);



/*** Opening all kinds of adm files ***/

/* Open `PATH/<adminstrative_subdir>/FNAME'. */
svn_error_t *svn_wc__open_adm_stream(svn_stream_t **stream,
                                     const char *dir_abspath,
                                     const char *fname,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool);


/* Blow away the admistrative directory associated with DIR_ABSPATH.
   For single-db this doesn't perform actual work unless the wcroot is passed.
 */
svn_error_t *svn_wc__adm_destroy(svn_wc__db_t *db,
                                 const char *dir_abspath,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *scratch_pool);


/* Cleanup the temporary storage area of the administrative
   directory (assuming temp and admin areas exist). */
svn_error_t *
svn_wc__adm_cleanup_tmp_area(svn_wc__db_t *db,
                             const char *adm_abspath,
                             apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_ADM_FILES_H */
