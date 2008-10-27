/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
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
