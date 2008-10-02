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
 * This library makes a clear distinction between directory entries (dirents)
 * and URIs where:
 *  - a dirent is a path on (local) disc or a UNC path (Windows)
 *    examples: "/foo/bar", "X:/temp", "//server/share"
 *  - a URI is a path in a repository or a URL
 *    examples: "/trunk/README", "http://hostname/svn/repos"
 *
 * This distiniction is needed because on Windows we have to handle some 
 * dirents and URIs differently. Since it's not possible to determine from 
 * the path string if it's a dirent or a URI, it's up to the API user to 
 * make this choice. See also issue #2028.
 *
 * ###TODO: add description in line with svn_path.h, once more functions
 * are moved.
 * ###END TODO
 * 
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


/** Join a base dirent (@a base) with a component (@a component), allocated in
 * @a pool.
 *
 * If either @a base or @a component is the empty string, then the other
 * argument will be copied and returned.  If both are the empty string then
 * empty string is returned.
 *
 * If the @a component is an absolute dirent, then it is copied and returned.
 * Exactly one slash character ('/') is used to joined the components,
 * accounting for any trailing slash in @a base, except on Windows when
 * @base is "X:".
 *
 * This function is NOT appropriate for native (local) file
 * dirents. Only for "internal" canonicalized dirents, since it uses '/'
 * for the separator.
 *
 * @since New in 1.6.
 */
char *svn_dirent_join(const char *base,
                      const char *component,
                      apr_pool_t *pool);

/** Join multiple components onto a @a base dirent, allocated in @a pool. The
 * components are terminated by a @c NULL.
 *
 * If any component is the empty string, it will be ignored.
 *
 * If any component is an absolute dirent, then it resets the base and
 * further components will be appended to it.
 *
 * See svn_dirent_join() for further notes about joining dirents.
 *
 * @since New in 1.6.
 */
char *svn_dirent_join_many(apr_pool_t *pool, const char *base, ...);

/** Get the basename of the specified canonicalized @a dirent.  The
 * basename is defined as the last component of the dirent (ignoring any
 * trailing slashes).  If the @a dirent is root ("/", "X:/", "//server/share/"),
 * then that is returned.  Otherwise, the returned value will have no slashes
 * in it.
 *
 * Example: svn_dirent_basename("/foo/bar") -> "bar"
 *
 * The returned basename will be allocated in @a pool.
 *
 * @note If an empty string is passed, then an empty string will be returned.
 *
 * @since New in 1.6.
 */
char *svn_dirent_basename(const char *dirent, apr_pool_t *pool);


/** Get the dirname of the specified canonicalized @a dirent, defined as
 * the dirent with its basename removed.
 *
 * If @a dirent is root  ("/", "X:/", "//server/share/"), it is returned
 * unchanged.
 *
 * The returned dirname will be allocated in @a pool.
 *
 * @since New in 1.6.
 */
char *svn_dirent_dirname(const char *dirent, apr_pool_t *pool);

/** Divide the canonicalized @a dirent into @a *dirpath and @a
 * *base_name, allocated in @a pool.
 *
 * If @a dirpath or @a base_name is NULL, then don't set that one.
 *
 * Either @a dirpath or @a base_name may be @a dirent's own address, but they
 * may not both be the same address, or the results are undefined.
 *
 * If @a dirent has two or more components, the separator between @a dirpath
 * and @a base_name is not included in either of the new names.
 *
 *   examples:
 *             - <pre>"/foo/bar/baz"  ==>  "/foo/bar" and "baz"</pre>
 *             - <pre>"/bar"          ==>  "/"  and "bar"</pre>
 *             - <pre>"/"             ==>  "/"  and "/"</pre>
 *             - <pre>"bar"           ==>  ""   and "bar"</pre>
 *             - <pre>""              ==>  ""   and ""</pre>
 *             - <pre>"X:/"           ==>  "X:/" and "X:/"</pre>
 *             - <pre>"X:/foo"        ==>  "X:/" and "foo"</pre>
 *  posix:     - <pre>"X:foo"         ==>  "X:" and "foo"</pre>
 *  windows:   - <pre>"X:foo"         ==>  ""   and "X:foo"</pre>
 *
 * @since New in 1.6.
 */
void svn_dirent_split(const char *dirent,
                      const char **dirpath,
                      const char **base_name,
                      apr_pool_t *pool);

/** Return TRUE if @a dirent is considered absolute on the platform at 
 * hand, amongst which '/foo' on all platforms or 'X:/foo', '\\\\?\\X:/foo',
 * '\\\\server\\share\\foo' on Windows.
 *
 * @since New in 1.6.
 */
svn_boolean_t svn_dirent_is_absolute(const char *dirent);

/** Return TRUE if @a uri is considered absolute or is a URL.
 *
 * @since New in 1.6.
 */
svn_boolean_t svn_uri_is_absolute(const char *dirent);

/** Return TRUE if @a dirent is considered a root directory on the platform
 * at hand, amongst which '/' on all platforms or 'X:/', '\\\\?\\X:/',
 * '\\\\.\\..', '\\\\server\\share' on Windows.
 *
 * @since New in 1.5.
 */
svn_boolean_t
svn_dirent_is_root(const char *dirent, apr_size_t len);

/** Return TRUE if @a uri is a root path, so starts with '/'. 
 *
 * Do not use this function with URLs.
 * 
 * @since New in 1.6
 */
svn_boolean_t
svn_uri_is_root(const char *uri, apr_size_t len);

/** Return a new dirent like @a dirent, but transformed such that some types
 * of dirent specification redundancies are removed.
 *
 * This involves collapsing redundant "/./" elements, removing
 * multiple adjacent separator characters, removing trailing
 * separator characters, and possibly other semantically inoperative
 * transformations.
 *
 * Convert the server name of UNC paths lowercase on Windows.
 *
 * The returned dirent may be statically allocated, equal to @a dirent, or
 * allocated from @a pool.
 *
 * @since New in 1.6.
 */
const char *svn_dirent_canonicalize(const char *dirent, apr_pool_t *pool);

/** Return @c TRUE iff dirent is canonical.  Use @a pool for temporary
 * allocations.
 *
 * @note The test for canonicalization is currently defined as
 * "looks exactly the same as @c svn_dirent_canonicalize() would make
 * it look".
 *
 * @since New in 1.6.
 */
svn_boolean_t svn_dirent_is_canonical(const char *dirent, apr_pool_t *pool);

/** Return the longest common dirent shared by two canonicalized dirents,
 * @a dirent1 and @a dirent2.  If there's no common ancestor, return the
 * empty path.
 *
 * @since New in 1.6.
 */
char *
svn_dirent_get_longest_ancestor(const char *dirent1,
                                const char *dirent2,
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
 *
 * @since New in 1.6.
 */
char *
svn_uri_get_longest_ancestor(const char *path1,
                             const char *path2,
                             apr_pool_t *pool);

/** Convert @a relative canonicalized dirent to an absolute dirent and
 * return the results in @a *pabsolute, allocated in @a pool.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_dirent_get_absolute(const char **pabsolute,
                        const char *relative,
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
 * ### todo: the ".." restriction is unfortunate, and would ideally
 * be lifted by making the implementation smarter.  But this is not
 * trivial: if the uri is "../foo", how do you know whether or not
 * the current directory is named "foo" in its parent?
 *
 * @since New in 1.6.
 */
const char *
svn_uri_is_child(const char *uri1, const char *uri2,
                 apr_pool_t *pool);

/**
 * This function is similar as @c svn_uri_is_child, except that it supports
 * Windows dirents and UNC paths on Windows.
 *
 * @since New in 1.6.
 */
const char *
svn_dirent_is_child(const char *dirent1, const char *dirent2,
                    apr_pool_t *pool);

/** Return TRUE if @a dirent1 is an ancestor of @a dirent2 or the dirents are
 * equal and FALSE otherwise.
 *
 * @since New in 1.6.
 */
svn_boolean_t
svn_dirent_is_ancestor(const char *path1, const char *path2);

/** Return TRUE if @a uri1 is an ancestor of @a uri2 or the uris are
 * equal and FALSE otherwise.
 *
 * This function supports URLs.
 *
 * @since New in 1.6.
 */
svn_boolean_t
svn_uri_is_ancestor(const char *path1, const char *path2);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DIRENT_URI_H */
