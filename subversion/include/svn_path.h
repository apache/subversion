/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * @file svn_path.h
 * @brief A path manipulation library
 *
 * All incoming and outgoing paths are in UTF-8.
 * 
 * No result path ever ends with a separator, no matter whether the
 * path is a file or directory, because we always canonicalize() it.
 *
 * All paths passed to the @c svn_path_xxx functions, with the exceptions of
 * the @c svn_path_canonicalize and @c svn_path_internal_style functions, must
 * be in canonical form.
 *
 * todo: this library really needs a test suite!
 */

#ifndef SVN_PATH_H
#define SVN_PATH_H


#include <apr_pools.h>
#include <apr_tables.h>
#include "svn_string.h"
#include "svn_error.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/** Convert @a path from the local style to the canonical internal style. */
const char *svn_path_internal_style (const char *path, apr_pool_t *pool);

/** Convert @a path from the canonical internal style to the local style. */
const char *svn_path_local_style (const char *path, apr_pool_t *pool);


/** Join a base path (@a base) with a component (@a component), allocated in 
 * @a pool.
 *
 * If either @a base or @a component is the empty path, then the other 
 * argument will be copied and returned.  If both are the empty path the 
 * empty path is returned.
 *
 * If the @a component is an absolute path, then it is copied and returned.
 * Exactly one slash character ('/') is used to joined the components,
 * accounting for any trailing slash in @a base.
 *
 * Note that the contents of @a base are not examined, so it is possible to
 * use this function for constructing URLs, or for relative URLs or
 * repository paths.
 *
 * This function is NOT appropriate for native (local) file paths. Only
 * for "internal" paths, since it uses '/' for the separator. Further,
 * an absolute path (for @a component) is based on a leading '/' character.
 * Thus, an "absolute URI" for the @a component won't be detected. An
 * absolute URI can only be used for the base.
 */
char *svn_path_join (const char *base,
                     const char *component,
                     apr_pool_t *pool);

/** Join multiple components onto a @a base path, allocated in @a pool.
 *
 * Join multiple components onto a @a base path, allocated in @a pool. The
 * components are terminated by a @c NULL.
 *
 * If any component is the empty string, it will be ignored.
 *
 * If any component is an absolute path, then it resets the base and
 * further components will be appended to it.
 *
 * See @c svn_path_join() for further notes about joining paths.
 */
char *svn_path_join_many (apr_pool_t *pool, const char *base, ...);


/** Get the basename of the specified PATH.
 *
 * Get the basename of the specified PATH.  The basename is defined as
 * the last component of the path (ignoring any trailing slashes).  If
 * the PATH is root ("/"), then that is returned.  Otherwise, the
 * returned value will have no slashes in it.
 *
 * Example: svn_path_basename("/foo/bar") -> "bar"
 *
 * The returned basename will be allocated in POOL.
 *
 * Note: if an empty string is passed, then an empty string will be returned.
 */
char *svn_path_basename (const char *path, apr_pool_t *pool);

/** Get the dirname of the specified @a path, defined as the path with its
 * basename removed.
 *
 * Get the dirname of the specified @a path, defined as the path with its
 * basename removed.  If @a path is root ("/"), it is returned unchanged.
 *
 * The returned basename will be allocated in @a pool.
 */
char *svn_path_dirname (const char *path, apr_pool_t *pool);

/** Add a @a component (a null-terminated C-string) to @a path.
 *
 * Add a @a component (a null-terminated C-string) to @a path.  @a component 
 * is allowed to contain directory separators.
 *
 * If @a path is non-empty, append the appropriate directory separator
 * character, and then @a component.  If @a path is empty, simply set it to
 * @a component; don't add any separator character.
 *
 * If the result ends in a separator character, then remove the separator.
 */
void svn_path_add_component (svn_stringbuf_t *path, 
                             const char *component);

/** Remove one component off the end of @a path. */
void svn_path_remove_component (svn_stringbuf_t *path);


/** Divide @a path into @a *dirpath and @a *base_name, allocated in @a pool.
 *
 * Divide @a path into @a *dirpath and @a *base_name, allocated in @a pool.
 *
 * If @a dirpath or @a base_name is null, then don't set that one.
 *
 * Either @a dirpath or @a base_name may be @a path's own address, but they 
 * may not both be the same address, or the results are undefined.
 *
 * If @a path has two or more components, the separator between @a dirpath
 * and @a base_name is not included in either of the new names.
 *
 *   examples:
 *             - <pre>"/foo/bar/baz"  ==>  "/foo/bar" and "baz"</pre>
 *             - <pre>"/bar"          ==>  "/"  and "bar"</pre>
 *             - <pre>"/"             ==>  "/"  and ""</pre>
 *             - <pre>"bar"           ==>  ""   and "bar"</pre>
 *             - <pre>""              ==>  ""   and ""</pre>
 */
void svn_path_split (const char *path, 
                     const char **dirpath,
                     const char **base_name,
                     apr_pool_t *pool);


/** Return non-zero iff PATH is empty ("") or represents the current
 * directory -- that is, if prepending it as a component to an existing
 * path would result in no meaningful change.
 */
int svn_path_is_empty (const char *path);


/** Return a new path like @a path, but with any trailing separators that don't
 * affect @a path's meaning removed.
 *
 * Return a new path like @a path, but with any trailing separators that don't
 * affect @a path's meaning removed. Will convert a "." path to "".  Allocate
 * the new path in @a pool if anything changed, else just return @a path.
 *
 * (At some future point, this may make other semantically inoperative
 * transformations.)
 */
const char *svn_path_canonicalize (const char *path, apr_pool_t *pool);


/** Return an integer greater than, equal to, or less than 0, according
 * as @a path1 is greater than, equal to, or less than @a path2.
 */
int svn_path_compare_paths (const char *path1, const char *path2);


/** Return the longest common path shared by both @a path1 and @a path2.
 *
 * Return the longest common path shared by both @a path1 and @a path2.  If
 * there's no common ancestor, return @c NULL.
 */
char *svn_path_get_longest_ancestor (const char *path1,
                                     const char *path2,
                                     apr_pool_t *pool);

/** Convert @a relative path to an absolute path and return the results in
 * @a *pabsolute, allocated in @a pool.
 */
svn_error_t *
svn_path_get_absolute (const char **pabsolute,
                       const char *relative,
                       apr_pool_t *pool);

/** Return the path part of @a path in @a *pdirectory, and the file part in 
 * @a *pfile.
 *
 * Return the path part of @a path in @a *pdirectory, and the file part in 
 * @a *pfile.  If @a path is a directory, set @a *pdirectory to @a path, and 
 * @a *pfile to the empty string.  If @a path does not exist it is treated 
 * as if it is a file, since directories do not normally vanish.
 */
svn_error_t *
svn_path_split_if_file(const char *path,
                       const char **pdirectory, 
                       const char **pfile,
                       apr_pool_t *pool);

/** Find the common prefix of the paths in @a targets, and remove redundancies.
 *
 * Find the common prefix of the paths in @a targets, and remove redundancies.
 *
 * The elements in @a targets must be existing files or directories (as
 * const char *).
 *
 * If there are multiple targets, or exactly one target and it's not a
 * directory, then 
 *
 *   - @a *pbasename is set to the absolute path of the common parent
 *     directory of all of those targets, and
 *
 *   - If @a pcondensed_targets is non-null, @a *pcondensed_targets is set
 *     to an array of targets relative to @a *pbasename, with
 *     redundancies removed (meaning that none of these targets will
 *     be the same as, nor have an ancestor/descendant relationship
 *     with, any of the other targets; nor will any of them be the
 *     same as @a *pbasename).  Else if @a pcondensed_targets is null, it is
 *     left untouched.
 *
 * Else if there is exactly one directory target, then
 *
 *   - @a *pbasename is set to that directory, and
 *
 *   - If @a pcondensed_targets is non-null, @a *pcondensed_targets is set
 *     to an array containing zero elements.  Else if
 *     @a pcondensed_targets is null, it is left untouched.
 *
 * If there are no items in @a targets, @a *pbasename and (if applicable)
 * @a *pcondensed_targets will be @c NULL.
 *
 * NOTE: There is no guarantee that @a *pbasename is within a working
 * copy.
 */
svn_error_t *
svn_path_condense_targets (const char **pbasename,
                           apr_array_header_t **pcondensed_targets,
                           const apr_array_header_t *targets,
                           apr_pool_t *pool);


/** Copy a list of @a targets, one at a time, into @a pcondensed_targets,
 * omitting any targets that are found earlier in the list, or whose
 * ancestor is found earlier in the list.
 *
 * Copy a list of @a targets, one at a time, into @a pcondensed_targets,
 * omitting any targets that are found earlier in the list, or whose
 * ancestor is found earlier in the list.  Ordering of targets in the
 * original list is preserved in the condensed list of targets.  Use
 * @a pool for any allocations.  
 *
 * How does this differ in functionality from @c svn_path_condense_targets?
 *
 * Here's the short version:
 * 
 * 1.  Disclaimer: if you wish to debate the following, talk to Karl. :-)
 *     Order matters for updates because a multi-arg update is not
 *     atomic, and CVS users are used to, when doing 'cvs up targetA
 *     targetB' seeing targetA get updated, then targetB.  I think the
 *     idea is that if you're in a time-sensitive or flaky-network
 *     situation, a user can say, "I really *need* to update
 *     wc/A/D/G/tau, but I might as well update my whole working copy if
 *     I can."  So that user will do 'svn up wc/A/D/G/tau wc', and if
 *     something dies in the middles of the 'wc' update, at least the
 *     user has 'tau' up-to-date.
 * 
 * 2.  Also, we have this notion of an anchor and a target for updates
 *     (the anchor is where the update editor is rooted, the target is
 *     the actual thing we want to update).  I needed a function that
 *     would NOT screw with my input paths so that I could tell the
 *     difference between someone being in A/D and saying 'svn up G' and
 *     being in A/D/G and saying 'svn up .' -- believe it or not, these
 *     two things don't mean the same thing.  @c svn_path_condense_targets
 *     plays with absolute paths (which is fine, so does
 *     @c svn_path_remove_redundancies), but the difference is that it
 *     actually tweaks those targets to be relative to the "grandfather
 *     path" common to all the targets.  Updates don't require a
 *     "grandfather path" at all, and even if it did, the whole
 *     conversion to an absolute path drops the crucial difference
 *     between saying "i'm in foo, update bar" and "i'm in foo/bar,
 *     update '.'"
 */
svn_error_t *
svn_path_remove_redundancies (apr_array_header_t **pcondensed_targets,
                              const apr_array_header_t *targets,
                              apr_pool_t *pool);


/** Decompose @a path into an array of <tt>const char *</tt> components, 
 * allocated in @a pool.
 *
 * Decompose @a path into an array of <tt>const char *</tt> components, 
 * allocated in @a pool.  @a style indicates the dir separator to split the 
 * string on.  If @a path is absolute, the first component will be a lone dir
 * separator (the root directory).
 */
apr_array_header_t *svn_path_decompose (const char *path,
                                        apr_pool_t *pool);


/** Test that @a name is a single path component.
 *
 * Test that @a name is a single path component, that is:
 *   - not @c NULL or empty.
 *   - not a `/'-separated directory path
 *   - not empty or `..'  
 */
svn_boolean_t svn_path_is_single_path_component (const char *name);


/** Test if @a path2 is a child of @a path1.
 *
 * Test if @a path2 is a child of @a path1.
 * If not, return @c NULL.
 * If so, return a copy of the remainder path, allocated in @a pool.
 * (The remainder is the component which, added to @a path1, yields
 * @a path2.  The remainder does not begin with a dir separator.)  
 *
 * Both paths must be in canonical form.
 */
const char *svn_path_is_child (const char *path1,
                               const char *path2,
                               apr_pool_t *pool);


/** URI/URL stuff
 *
 * @defgroup svn_path_uri_stuff URI/URL stuff
 * @{
 */

/** Return @c TRUE iff @a path looks like a valid URL, @c FALSE otherwise. */
svn_boolean_t svn_path_is_url (const char *path);

/** Return @c TRUE iff @a path is URI-safe, @c FALSE otherwise. */
svn_boolean_t svn_path_is_uri_safe (const char *path);

/** Return a URI-encoded copy of @a path, allocated in @a pool. */
const char *svn_path_uri_encode (const char *path, apr_pool_t *pool);

/** Return a URI-decoded copy of @a path, allocated in @a pool. */
const char *svn_path_uri_decode (const char *path, apr_pool_t *pool);

/** Extend @a url by a single @a component, URI-encoding that @a component
 * before adding it to the @a url.
 *
 * Extend @a url by a single @a component, URI-encoding that @a component
 * before adding it to the @a url.  Return the new @a url, allocated in
 * @a pool.  Notes: if @a component is @c NULL, just return a copy or @a url
 * allocated in @a pool; if @a component is already URI-encoded, calling
 * code should just use <tt>svn_path_join (url, component, pool)</tt>.  @a url
 * does not need to be a canonical path, it may have trailing '/'.
 */
const char *svn_path_url_add_component (const char *url,
                                        const char *component,
                                        apr_pool_t *pool);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* SVN_PATH_H */
