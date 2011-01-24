/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_fspath.h
 * @brief Implementation of path manipulation functions similar to
 *        those in svn_dirent_uri.h (which see for details) but for
 *        the private fspath class of paths.
 */

#ifndef SVN_FSPATH_H
#define SVN_FSPATH_H

#include <apr.h>
#include <apr_pools.h>

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Return TRUE iff @a fspath is canonical.
 * @a fspath need not be canonical, of course.
 *
 * @since New in 1.7.
 */
svn_boolean_t
svn_fspath__is_canonical(const char *fspath);


/** Return the dirname of @a fspath, defined as the path with its basename
 * removed.  If @a fspath is "/", return "/".
 *
 * Allocate the result in @a pool.
 *
 * @since New in 1.7.
 */
const char *
svn_fspath__dirname(const char *fspath,
                    apr_pool_t *pool);

/** Return the last component of @a fspath.  The returned value will have no
 * slashes in it.  If @a fspath is "/", return "".
 *
 * If @a pool is NULL, return a pointer to within @a fspath, else allocate
 * the result in @a pool.
 *
 * @since New in 1.7.
 */
const char *
svn_fspath__basename(const char *fspath,
                     apr_pool_t *pool);

/** Divide the canonical @a fspath into @a *dirpath and @a
 * *base_name, allocated in @a pool.
 *
 * If @a dirpath or @a base_name is NULL, then don't set that one.
 *
 * Either @a dirpath or @a base_name may be @a fspath's own address, but they
 * may not both be the same address, or the results are undefined.
 *
 * If @a fspath has two or more components, the separator between @a dirpath
 * and @a base_name is not included in either of the new names.
 *
 * @since New in 1.7.
 */
void
svn_fspath__split(const char **dirpath,
                  const char **base_name,
                  const char *fspath,
                  apr_pool_t *result_pool);

/** Return the fspath composed of @a fspath with @a relpath appended.
 * Allocate the result in @a result_pool.
 *
 * @since New in 1.7.
 */
char *
svn_fspath__join(const char *fspath,
                 const char *relpath,
                 apr_pool_t *result_pool);


/** Test if @a child_fspath is a child of @a parent_fspath.  If not, return
 * NULL.  If so, return the relpath which, if joined to @a parent_fspath,
 * would yield @a child_fspath.
 *
 * If @a child_fspath is the same as @a parent_fspath, it is not considered
 * a child, so the result is NULL; an empty string is never returned.
 *
 * If @a pool is NULL, a pointer into @a child_fspath will be returned to
 * identify the remainder fspath.
 *
 * @since New in 1.7.
 */
const char *
svn_fspath__is_child(const char *parent_fspath,
                     const char *child_fspath,
                     apr_pool_t *pool);

/** Return the relative path part of @a child_fspath that is below
 * @a parent_fspath, or just "" if @a parent_fspath is equal to
 * @a child_fspath. If @a child_fspath is not below @a parent_fspath,
 * return @a child_fspath.
 *
 * ### Returning the child in the no-match case is a bad idea.
 *
 * @since New in 1.7.
 */
const char *
svn_fspath__skip_ancestor(const char *parent_fspath,
                          const char *child_fspath);

/** Return TRUE if @a parent_fspath is an ancestor of @a child_fspath or
 * the fspaths are equal, and FALSE otherwise.
 *
 * @since New in 1.7.
 */
svn_boolean_t
svn_fspath__is_ancestor(const char *parent_fspath,
                        const char *child_fspath);

/** Return the longest common path shared by two fspaths, @a fspath1 and
 * @a fspath2.  If there's no common ancestor, return "/".
 *
 * @since New in 1.7.
 */
char *
svn_fspath__get_longest_ancestor(const char *fspath1,
                                 const char *fspath2,
                                 apr_pool_t *result_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FSPATH_H */
