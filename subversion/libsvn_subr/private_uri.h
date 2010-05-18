/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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
 * @file private_uri.h
 * @brief Header file for currently internal svn_uri_*() API
 *
 * ### These Functions will be added to the public API in 1.7
 *     but because their is some discussion on how they should work
 *     this api has been removed from 1.6 to allow future redesign.
 */

#ifndef SVN_LIBSVN_SUBR_PRIVATE_URI_H
#define SVN_LIBSVN_SUBR_PRIVATE_URI_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Convert @a uri from the local style to the canonical internal style.
 */
const char *
svn_uri_internal_style(const char *uri,
                       apr_pool_t *pool);

/** Convert @a uri from the canonical internal style to the local style.
 */
const char *
svn_uri_local_style(const char *uri,
                    apr_pool_t *pool);

/** Get the dirname of the specified canonicalized @a uri, defined as
 * the dirent with its basename removed.
 *
 * If @a dirent is root  (e.g. "http://server"), it is returned
 * unchanged.
 *
 * The returned dirname will be allocated in @a pool.
 */
char *
svn_uri_dirname(const char *dirent,
                apr_pool_t *pool);

/** Return TRUE if @a uri is considered absolute or is a URL.
 */
svn_boolean_t
svn_uri_is_absolute(const char *dirent);

/** Return TRUE if @a uri is a root path, so starts with '/'.
 *
 * Do not use this function with URLs.
 */
svn_boolean_t
svn_uri_is_root(const char *uri,
                apr_size_t len);


/** Return a new uri like @a uri, but transformed such that some types
 * of uri specification redundancies are removed.
 *
 * This involves collapsing redundant "/./" elements, removing
 * multiple adjacent separator characters, removing trailing
 * separator characters, and possibly other semantically inoperative
 * transformations.
 *
 * This functions supports URLs.
 *
 * The returned uri may be statically allocated, equal to @a uri, or
 * allocated from @a pool.
 */
const char *
svn_uri_canonicalize(const char *uri,
                     apr_pool_t *pool);

/** Return @c TRUE iff @a uri is canonical.  Use @a pool for temporary
 * allocations.
 */
svn_boolean_t
svn_uri_is_canonical(const char *uri,
                     apr_pool_t *pool);

/** Return the longest common path shared by two canonicalized uris,
 * @a uri1 and @a uri2.  If there's no common ancestor, return the
 * empty path.
 *
 * @a path1 and @a path2 may be URLs.  In order for two URLs to have
 * a common ancestor, they must (a) have the same protocol (since two URLs
 * with the same path but different protocols may point at completely
 * different resources), and (b) share a common ancestor in their path
 * component, i.e. 'protocol://' is not a sufficient ancestor.
 */
char *
svn_uri_get_longest_ancestor(const char *path1,
                             const char *path2,
                             apr_pool_t *pool);


/** Test if @a uri2 is a child of @a uri1.
 * If not, return @c NULL.
 * If so, return a copy of the remainder uri, allocated in @a pool.
 * (The remainder is the component which, added to @a uri1, yields
 * @a uri2.  The remainder does not begin with a dir separator.)
 *
 * Both uris must be in canonical form, and must either be absolute,
 * or contain no ".." components.
 *
 * If @a uri2 is the same as @a uri1, it is not considered a child,
 * so the result is @c NULL; an empty string is never returned.
 *
 * If @a pool is @c NULL , a pointer into @a uri2 will be returned to
 *       identify the remainder uri.
 *
 * ### @todo the ".." restriction is unfortunate, and would ideally
 * be lifted by making the implementation smarter.  But this is not
 * trivial: if the uri is "../foo", how do you know whether or not
 * the current directory is named "foo" in its parent?
 */
const char *
svn_uri_is_child(const char *uri1,
                 const char *uri2,
                 apr_pool_t *pool);

/** Return TRUE if @a uri1 is an ancestor of @a uri2 or the uris are
 * equal and FALSE otherwise.
 *
 * This function supports URLs.
 */
svn_boolean_t
svn_uri_is_ancestor(const char *path1,
                    const char *path2);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_PRIVATE_URI_H */
