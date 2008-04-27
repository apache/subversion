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
 * @file svn_opt_private.h
 * @brief Subversion-internal option parsing APIs.
 */

#ifndef SVN_OPT_PRIVATE_H
#define SVN_OPT_PRIVATE_H

#include <apr_pools.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Attempt to transform URL_IN, which is a URL-like user input, into a
 * valid URL:
 *   - escape IRI characters and some other non-URI characters
 *   - check that only valid URI characters remain
 *   - check that no back-path ("..") components are present
 *   - canonicalize the separator ("/") characters
 * URL_IN is in UTF-8 encoding and has no peg revision specifier.
 * Set *URL_OUT to the result, allocated from POOL.
 */
svn_error_t *
svn_opt__arg_canonicalize_url(const char **url_out,
                              const char *url_in,
                              apr_pool_t *pool);

/*
 * Attempt to transform PATH_IN, which is a local path-like user input, into a
 * valid local path:
 *   - Attempt to get the correct capitialization by trying to actually find
 *     the path specified.
 *   - If the path does not exist (which is valid) the given capitialization
 *     is used.
 *   - canonicalize the separator ("/") characters
 * PATH_IN is in UTF-8 encoding and has no peg revision specifier.
 * Set *PATH_OUT to the result, allocated from POOL.
 */
svn_error_t *
svn_opt__arg_canonicalize_path(const char **path_out,
                               const char *path_in,
                               apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_OPT_PRIVATE_H */
