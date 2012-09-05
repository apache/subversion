/*
 * opt.h: share svn_opt__* functions
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

#ifndef SVN_LIBSVN_SUBR_OPT_H
#define SVN_LIBSVN_SUBR_OPT_H

#include <apr_tables.h>
#include "svn_opt.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* This structure stores the available information about the version,
 * build and runtime environment of the Subversion client libraries.
 */
typedef struct svn_opt__version_info_t svn_opt__version_info_t;
struct svn_opt__version_info_t
{
  const char *version_number;   /* version number */
  const char *version_string;   /* version string */
  const char *build_date;       /* compilation date */
  const char *build_time;       /* compilation time */
  const char *build_host;       /* nuild canonical host name */
  const char *copyright;        /* vopyright notice */
  const char *runtime_host;     /* runtime canonical host name */
  const char *runtime_osname;   /* running OS release name */

  /* Array svn_sysinfo__linked_lib_t describing dependent libraries */
  const apr_array_header_t *linked_libs;

  /* Array of svn_sysinfo__loaded_lib_t describing loaded shared libraries */
  const apr_array_header_t *loaded_libs;
};


/* Return version information for the running program, allocated from POOL.
 *
 * Use POOL for temporary allocations.
 */
const svn_opt__version_info_t *
svn_opt__get_version_info(apr_pool_t *pool);

/* Print version version info for PGM_NAME to the console.  If QUIET is
 * true, print in brief.  Else if QUIET is not true, print the version
 * more verbosely, and if FOOTER is non-null, print it following the
 * version information. If VERBOSE is true, print running system info.
 *
 * Use POOL for temporary allocations.
 */
svn_error_t *
svn_opt__print_version_info(const char *pgm_name,
                            const char *footer,
                            const svn_opt__version_info_t *info,
                            svn_boolean_t quiet,
                            svn_boolean_t verbose,
                            apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_OPT_H */
