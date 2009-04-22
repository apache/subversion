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
#include <apr_tables.h>
#include <apr_getopt.h>

#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Extract the peg revision, if any, from UTF8_TARGET. Return the peg
 * revision in *PEG_REVISION and the true target portion in *TRUE_TARGET.
 * *PEG_REVISION will be an empty string if no peg revision is found.
 *
 * UTF8_TARGET need not be canonical. *TRUE_TARGET will not be canonical
 * unless UTF8_TARGET is.
 *
 * Note that *PEG_REVISION will still contain the '@' symbol as the first
 * character if a peg revision was found.
 *
 * All allocations are done in POOL.
 */
svn_error_t *
svn_opt__split_arg_at_peg_revision(const char **true_target,
                                   const char **peg_revision,
                                   const char *utf8_target,
                                   apr_pool_t *pool);

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

/*
 * Pull remaining target arguments from OS into *TARGETS_P,
 * converting them to UTF-8, followed by targets from KNOWN_TARGETS
 * (which might come from, for example, the "--targets" command line
 * option), which are already in UTF-8.
 *
 * On each URL target, do some IRI-to-URI encoding and some
 * auto-escaping.  On each local path, canonicalize case and path
 * separators.
 *
 * Allocate *TARGETS_P and its elements in POOL.
 *
 * If a path has the same name as a Subversion working copy
 * administrative directory, return SVN_ERR_RESERVED_FILENAME_SPECIFIED;
 * if multiple reserved paths are encountered, return a chain of
 * errors, all of which are SVN_ERR_RESERVED_FILENAME_SPECIFIED.  Do
 * not return this type of error in a chain with any other type of
 * error, and if this is the only type of error encountered, complete
 * the operation before returning the error(s).
 */
svn_error_t *
svn_opt__args_to_target_array(apr_array_header_t **targets_p,
                              apr_getopt_t *os,
                              apr_array_header_t *known_targets,
                              apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_OPT_PRIVATE_H */
