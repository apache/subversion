/*  svn_path.h: a path manipulation library
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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


/*** Notes:
 *
 * All incoming and outgoing paths are in UTF-8.
 * 
 * No result path ever ends with a separator, no matter whether the
 * path is a file or directory, because we always canonicalize() it.
 *
 * All paths passed to the svn_path_xxx functions, with the exceptions of
 * the svn_path_canonicalize and svn_path_internal_style functions, must
 * be in canonical form.
 *
 * todo: this library really needs a test suite!
 ***/

/* Convert PATH from the local style to the canonical internal style. */
const char *svn_path_internal_style (const char *path, apr_pool_t *pool);

/* Convert PATH from the canonical internal style to the local style. */
const char *svn_path_local_style (const char *path, apr_pool_t *pool);


/* Join a base path (BASE) with a component (COMPONENT), allocated in POOL.

   If either BASE or COMPONENT is the empty path, then the other argument
   will be copied and returned.  If both are the empty path the empty
   path is returned.

   If the COMPONENT is an absolute path, then it is copied and returned.
   Exactly one slash character ('/') is used to joined the components,
   accounting for any trailing slash in BASE.

   Note that the contents of BASE are not examined, so it is possible to
   use this function for constructing URLs, or for relative URLs or
   repository paths.

   This function is NOT appropriate for native (local) file paths. Only
   for "internal" paths, since it uses '/' for the separator. Further,
   an absolute path (for COMPONENT) is based on a leading '/' character.
   Thus, an "absolute URI" for the COMPONENT won't be detected. An
   absolute URI can only be used for the base.
*/
char *svn_path_join (const char *base,
                     const char *component,
                     apr_pool_t *pool);

/* Join multiple components onto a BASE path, allocated in POOL. The
   components are terminated by a NULL.

   If any component is the empty string, it will be ignored.

   If any component is an absolute path, then it resets the base and
   further components will be appended to it.

   See svn_path_join() for further notes about joining paths.
*/
char *svn_path_join_many (apr_pool_t *pool, const char *base, ...);


/* Get the basename of the specified PATH.  The basename is defined as
   the last component of the path (ignoring any trailing slashes).  If
   the PATH is root ("/"), then that is returned.  Otherwise, the
   returned value will have no slashes in it.

   Example: svn_path_basename("/foo/bar") -> "bar"

   The returned basename will be allocated in POOL.

   Note: if an empty string is passed, then an empty string will be returned.
*/
char *svn_path_basename (const char *path, apr_pool_t *pool);

/* Get the dirname of the specified PATH, defined as the path with its
   basename removed.  If PATH is root ("/"), it is returned unchanged.

   The returned basename will be allocated in POOL.
*/

char *svn_path_dirname (const char *path, apr_pool_t *pool);

/* Add a COMPONENT (a null-terminated C-string) to PATH.  COMPONENT is
   allowed to contain directory separators.

   If PATH is non-empty, append the appropriate directory separator
   character, and then COMPONENT.  If PATH is empty, simply set it to
   COMPONENT; don't add any separator character.

   If the result ends in a separator character, then remove the separator. */
void svn_path_add_component (svn_stringbuf_t *path, 
                             const char *component);

/* Remove one component off the end of PATH. */
void svn_path_remove_component (svn_stringbuf_t *path);


/* Divide PATH into *DIRPATH and *BASE_NAME, allocated in POOL.
 *
 * If DIRPATH or BASE_NAME is null, then don't set that one.
 *
 * Either DIRPATH or BASE_NAME may be PATH's own address, but they may
 * not both be the same address, or the results are undefined.
 *
 * If PATH has two or more components, the separator between DIRPATH
 * and BASE_NAME is not included in either of the new names.
 *
 *   examples:  "/foo/bar/baz"  ==>  "/foo/bar" and "baz" 
 *              "/bar"          ==>  "/"  and "bar"
 *              "/"             ==>  "/"  and ""
 *              "bar"           ==>  ""   and "bar"
 *              ""              ==>  ""   and ""
 */
void svn_path_split (const char *path, 
                     const char **dirpath,
                     const char **base_name,
                     apr_pool_t *pool);


/* Return non-zero iff PATH is empty ("") or represents the current
   directory -- that is, if prepending it as a component to an existing
   path would result in no meaningful change.  */
int svn_path_is_empty (const char *path);


/* Return a new path like PATH, but with any trailing separators that don't
   affect PATH's meaning removed. Will convert a "." path to "".  Allocate
   the new path in POOL if anything changed, else just return PATH.

   (At some future point, this may make other semantically inoperative
   transformations.) */
const char *svn_path_canonicalize (const char *path, apr_pool_t *pool);


/* Return an integer greater than, equal to, or less than 0, according
   as PATH1 is greater than, equal to, or less than PATH2. */
int svn_path_compare_paths (const char *path1, const char *path2);


/* Return the longest common path shared by both PATH1 and PATH2.  If
   there's no common ancestor, return NULL.  */
char *svn_path_get_longest_ancestor (const char *path1,
                                     const char *path2,
                                     apr_pool_t *pool);

/* Convert RELATIVE path to an absolute path and return the results in
   *PABSOLUTE, allocated in POOL. */
svn_error_t *
svn_path_get_absolute (const char **pabsolute,
                       const char *relative,
                       apr_pool_t *pool);

/* Return the path part of PATH in *PDIRECTORY, and the file part in *PFILE.
   If PATH is a directory, set *PDIRECTORY to PATH, and *PFILE to the
   empty string.  If PATH does not exist it is treated as if it is a file,
   since directories do not normally vanish. */
svn_error_t *
svn_path_split_if_file(const char *path,
                       const char **pdirectory, 
                       const char **pfile,
                       apr_pool_t *pool);

/* Find the common prefix of the paths in TARGETS, and remove redundancies.
 *
 * The elements in TARGETS must be existing files or directories (as
 * const char *).
 *
 * If there are multiple targets, or exactly one target and it's not a
 * directory, then 
 *
 *   - *PBASENAME is set to the absolute path of the common parent
 *     directory of all of those targets, and
 *
 *   - If PCONDENSED_TARGETS is non-null, *PCONDENSED_TARGETS is set
 *     to an array of targets relative to *PBASENAME, with
 *     redundancies removed (meaning that none of these targets will
 *     be the same as, nor have an ancestor/descendant relationship
 *     with, any of the other targets; nor will any of them be the
 *     same as *PBASENAME).  Else if PCONDENSED_TARGETS is null, it is
 *     left untouched.
 *
 * Else if there is exactly one directory target, then
 *
 *   - *PBASENAME is set to that directory, and
 *
 *   - If PCONDENSED_TARGETS is non-null, *PCONDENSED_TARGETS is set
 *     to an array containing zero elements.  Else if
 *     PCONDENSED_TARGETS is null, it is left untouched.
 *
 * If there are no items in TARGETS, *PBASENAME and (if applicable)
 * *PCONDENSED_TARGETS will be NULL.
 *
 * NOTE: There is no guarantee that *PBASENAME is within a working
 * copy.
 */
svn_error_t *
svn_path_condense_targets (const char **pbasename,
                           apr_array_header_t **pcondensed_targets,
                           const apr_array_header_t *targets,
                           apr_pool_t *pool);


/* Copy a list of TARGETS, one at a time, into PCONDENSED_TARGETS,
   omitting any targets that are found earlier in the list, or whose
   ancestor is found earlier in the list.  Ordering of targets in the
   original list is preserved in the condensed list of targets.  Use
   POOL for any allocations.  

   How does this differ in functionality from svn_path_condense_targets?

   Here's the short version:
   
   1.  Disclaimer: if you wish to debate the following, talk to Karl. :-)
       Order matters for updates because a multi-arg update is not
       atomic, and CVS users are used to, when doing 'cvs up targetA
       targetB' seeing targetA get updated, then targetB.  I think the
       idea is that if you're in a time-sensitive or flaky-network
       situation, a user can say, "I really *need* to update
       wc/A/D/G/tau, but I might as well update my whole working copy if
       I can."  So that user will do 'svn up wc/A/D/G/tau wc', and if
       something dies in the middles of the 'wc' update, at least the
       user has 'tau' up-to-date.
   
   2.  Also, we have this notion of an anchor and a target for updates
       (the anchor is where the update editor is rooted, the target is
       the actual thing we want to update).  I needed a function that
       would NOT screw with my input paths so that I could tell the
       difference between someone being in A/D and saying 'svn up G' and
       being in A/D/G and saying 'svn up .' -- believe it or not, these
       two things don't mean the same thing.  svn_path_condense_targets
       plays with absolute paths (which is fine, so does
       svn_path_remove_redundancies), but the difference is that it
       actually tweaks those targets to be relative to the "grandfather
       path" common to all the targets.  Updates don't require a
       "grandfather path" at all, and even if it did, the whole
       conversion to an absolute path drops the crucial difference
       between saying "i'm in foo, update bar" and "i'm in foo/bar,
       update '.'"
*/
svn_error_t *
svn_path_remove_redundancies (apr_array_header_t **pcondensed_targets,
                              const apr_array_header_t *targets,
                              apr_pool_t *pool);


/* Decompose PATH into an array of const char * components, allocated
   in POOL.  STYLE indicates the dir separator to split the string on.
   If PATH is absolute, the first component will be a lone dir
   separator (the root directory). */
apr_array_header_t *svn_path_decompose (const char *path,
                                        apr_pool_t *pool);


/* Test that NAME is a single path component, that is:
     - not NULL or empty.
     - not a `/'-separated directory path
     - not empty or `..'  
*/
svn_boolean_t svn_path_is_single_path_component (const char *name);


/* Test if PATH2 is a child of PATH1.
   If not, return NULL.
   If so, return a copy of the remainder path, allocated in POOL.
   (The remainder is the component which, added to PATH1, yields
   PATH2.  The remainder does not begin with a dir separator.)  

   Both paths must be in canonical form. */
const char *svn_path_is_child (const char *path1,
                               const char *path2,
                               apr_pool_t *pool);


/*** URI/URL stuff ***/

/* Return TRUE iff PATH looks like a valid URL, FALSE otherwise. */
svn_boolean_t svn_path_is_url (const char *path);

/* Return TRUE iff PATH is URI-safe, FALSE otherwise. */
svn_boolean_t svn_path_is_uri_safe (const char *path);

/* Return a URI-encoded copy of PATH, allocated in POOL. */
const char *svn_path_uri_encode (const char *path, apr_pool_t *pool);

/* Return a URI-decoded copy of PATH, allocated in POOL. */
const char *svn_path_uri_decode (const char *path, apr_pool_t *pool);

/* Extend URL by a single COMPONENT, URI-encoding that COMPONENT
   before adding it to the URL.  Return the new URL, allocated in
   POOL.  Notes: if COMPONENT is NULL, just return a copy or URL
   allocated in POOL; if COMPONENT is already URI-encoded, calling
   code should just use svn_path_join (url, component, pool).  URL
   does not need to be a canonical path, it may have trailing '/'. */
const char *svn_path_url_add_component (const char *url,
                                        const char *component,
                                        apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* SVN_PATH_H */
