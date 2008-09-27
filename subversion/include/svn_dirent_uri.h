/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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
 * @file svn_dirent_uri.h
 * @brief A library to manipulate URIs and directory entries.
 *
 * ###TODO: add description in line with svn_path.h, once more functions
 * are moved.
 * ###END TODO
 */

#ifndef SVN_DIRENT_URI_H
#define SVN_DIRENT_URI_H


#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_string.h"
#include "svn_error.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/** Return TRUE if @a directory is considered a root directory on the platform
 * at hand, amongst which '/' on all platforms or 'X:/', '\\\\?\\X:/',
 * '\\\\.\\..', '\\\\server\\share' on Windows.
 *
 * @since New in 1.5.
 */
svn_boolean_t
svn_dirent_is_root(const char *dirent, apr_size_t len);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DIRENT_URI_H */
