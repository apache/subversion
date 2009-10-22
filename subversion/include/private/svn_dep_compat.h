/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_compat.h
 * @brief Compatibility macros and functions.
 * @since New in 1.5.0.
 */

#ifndef SVN_DEP_COMPAT_H
#define SVN_DEP_COMPAT_H

#include <apr_version.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Check at compile time if the APR version is at least a certain
 * level.
 * @param major The major version component of the version checked
 * for (e.g., the "1" of "1.3.0").
 * @param minor The minor version component of the version checked
 * for (e.g., the "3" of "1.3.0").
 * @param patch The patch level component of the version checked
 * for (e.g., the "0" of "1.3.0").
 *
 * @since New in 1.5.
 */
#ifndef APR_VERSION_AT_LEAST /* Introduced in APR 1.3.0 */
#define APR_VERSION_AT_LEAST(major,minor,patch)                  \
(((major) < APR_MAJOR_VERSION)                                       \
 || ((major) == APR_MAJOR_VERSION && (minor) < APR_MINOR_VERSION)    \
 || ((major) == APR_MAJOR_VERSION && (minor) == APR_MINOR_VERSION && \
     (patch) <= APR_PATCH_VERSION))
#endif /* APR_VERSION_AT_LEAST */

/**
 * Check at compile time if the Serf version is at least a certain
 * level.
 * @param major The major version component of the version checked
 * for (e.g., the "1" of "1.3.0").
 * @param minor The minor version component of the version checked
 * for (e.g., the "3" of "1.3.0").
 * @param patch The patch level component of the version checked
 * for (e.g., the "0" of "1.3.0").
 *
 * @since New in 1.5.
 */
#ifndef SERF_VERSION_AT_LEAST /* Introduced in Serf 0.1.1 */
#define SERF_VERSION_AT_LEAST(major,minor,patch)                       \
(((major) < SERF_MAJOR_VERSION)                                        \
 || ((major) == SERF_MAJOR_VERSION && (minor) < SERF_MINOR_VERSION)    \
 || ((major) == SERF_MAJOR_VERSION && (minor) == SERF_MINOR_VERSION && \
     (patch) <= SERF_PATCH_VERSION))
#endif /* SERF_VERSION_AT_LEAST */

/**
 * Check at compile time if the SQLite version is at least a certain
 * level.
 * @param major The major version component of the version checked
 * for (e.g., the "1" of "1.3.0").
 * @param minor The minor version component of the version checked
 * for (e.g., the "3" of "1.3.0").
 * @param patch The patch level component of the version checked
 * for (e.g., the "0" of "1.3.0").
 *
 * @since New in 1.6.
 */
#ifndef SQLITE_VERSION_AT_LEAST
#define SQLITE_VERSION_AT_LEAST(major,minor,patch)                     \
((major*1000000 + minor*1000 + patch) <= SQLITE_VERSION_NUMBER)
#endif /* SQLITE_VERSION_AT_LEAST */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DEP_COMPAT_H */
