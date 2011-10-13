/*
 * tree.h: reading a generic tree
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

#ifndef SVN_LIBSVN_CLIENT_TREE_H
#define SVN_LIBSVN_CLIENT_TREE_H

#include <apr_hash.h>
#include "svn_types.h"
#include "svn_io.h"
#include "svn_delta.h"
#include "svn_opt.h"
#include "svn_client.h"


#ifdef __cplusplus
extern "C" {
#endif


/** A readable tree.  This object presents an interface for reading from
 * a generic version-controlled tree in which each node is a file, a
 * directory or a symbolic link, and each node can have properties.
 *
 * An implementation of this interface could read from a tree that exists
 * at some revision of a repository, or the base or working version of
 * a Working Copy tree, or an unversioned tree on disk, or something else.
 *
 * Paths are relpaths, relative to the tree root, unless otherwise stated.
 *
 * @since New in 1.8.
 */
typedef struct svn_tree_t svn_tree_t;


/** Fetch the node kind of the node at @a relpath.
 * (### and other metadata? revnum? props?)
 *
 * The kind will be 'file', 'dir', 'symlink' or 'none'; not 'unknown'.
 *
 * Set @a *kind to the node kind.
 */
svn_error_t *
svn_tree_get_kind(svn_tree_t *tree,
                  svn_kind_t *kind,
                  const char *relpath,
                  apr_pool_t *scratch_pool);

/** Fetch the contents and/or properties of the file at @a relpath.
 *
 * If @a stream is non-NULL, set @a *stream to a readable stream yielding
 * the contents of the file at @a relpath.  (### ? The stream
 * handlers for @a stream may not perform any operations on @a tree.)
 *
 * If @a props is non-NULL, set @a *props to contain the regular
 * versioned properties of the file (not 'wcprops', 'entryprops', etc.).
 * The hash maps (const char *) names to (#svn_string_t *) values.
 *
 * If the node at @a relpath is not a symlink, return a
 * #SVN_ERR_WRONG_KIND error.
 */
svn_error_t *
svn_tree_get_file(svn_tree_t *tree,
                  svn_stream_t **stream,
                  apr_hash_t **props,
                  const char *relpath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool);

/** Fetch the entries and/or properties of the directory at @a relpath.
 *
 * If @a dirents is non-NULL, set @a *dirents to contain all the entries
 * of directory @a relpath.  The keys will be (<tt>const char *</tt>)
 * entry names; the values are unspecified.
 * Only the @c kind and @c filesize fields are filled in.
 * ### @c special would be useful too.
 *
 * If @a props is non-NULL, set @a *props to contain the regular
 * versioned properties of the file (not 'wcprops', 'entryprops', etc.).
 * The hash maps (const char *) names to (#svn_string_t *) values.
 *
 * If the node at @a relpath is not a symlink, return a
 * #SVN_ERR_WRONG_KIND error.
 */
svn_error_t *
svn_tree_get_dir(svn_tree_t *tree,
                 apr_hash_t **dirents,
                 apr_hash_t **props,
                 const char *relpath,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool);

/** Fetch the target and/or properties of the symlink at @a relpath.
 *
 * If @a link_target is non-NULL, set @a *link_target to the target of
 * the symbolic link.
 *
 * If @a props is non-NULL, set @a *props to contain the regular
 * versioned properties of the file (not 'wcprops', 'entryprops', etc.).
 * The hash maps (const char *) names to (#svn_string_t *) values.
 *
 * If the node at @a relpath is not a symlink, return a
 * #SVN_ERR_WRONG_KIND error.
 */
svn_error_t *
svn_tree_get_symlink(svn_tree_t *tree,
                     const char **link_target,
                     apr_hash_t **props,
                     const char *relpath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool);

/*-----------------------------------------------------------------*/


/* */
svn_error_t *
svn_client__disk_tree(svn_tree_t **tree_p,
                      const char *abspath,
                      apr_pool_t *result_pool);

/* */
svn_error_t *
svn_client__wc_base_tree(svn_tree_t **tree_p,
                         const char *abspath,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *result_pool);

/* */
svn_error_t *
svn_client__wc_working_tree(svn_tree_t **tree_p,
                            const char *abspath,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool);

/* */
svn_error_t *
svn_client__repository_tree(svn_tree_t **tree_p,
                            const char *path_or_url,
                            const svn_opt_revision_t *peg_revision,
                            const svn_opt_revision_t *revision,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool);

/* Open a tree, whether in the repository or a WC or unversioned on disk. */
svn_error_t *
svn_client__open_tree(svn_tree_t **tree,
                      const char *path,
                      const svn_opt_revision_t *revision,
                      const svn_opt_revision_t *peg_revision,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif

#endif /* SVN_LIBSVN_CLIENT_TREE_H */

