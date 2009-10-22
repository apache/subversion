/*
 * svn_patch.h: svnpatch related functions
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

#ifndef SVN_PATCH_H
#define SVN_PATCH_H

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Output -- Writing */

/* Append a command into @a target in a printf-like fashion.
 * @see svn_ra_svn_write_tuple() for further details with the format. */
svn_error_t *
svn_patch__write_cmd(svn_stream_t *target,
                     apr_pool_t *pool,
                     const char *cmdname,
                     const char *fmt,
                     ...);

/* Input -- Reading */

svn_error_t *
svn_patch__parse_tuple(apr_array_header_t *list,
                       apr_pool_t *pool,
                       const char *fmt,
                       ...);

svn_error_t *
svn_patch__read_tuple(svn_stream_t *from,
                      apr_pool_t *pool,
                      const char *fmt,
                      ...);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_PATCH_H */
