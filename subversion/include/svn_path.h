/*  svn_path.h: a path manipulation library
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_PATHS_H
#define SVN_PATHS_H



#include <apr_pools.h>
#include <apr_tables.h>
#include "svn_string.h"
#include "svn_error.h"


/*** Notes:
 * 
 * No result path ever ends with a separator, no matter whether the
 * path is a file or directory, because we always canonicalize() it.
 *
 * todo: this library really needs a test suite!
 ***/

enum svn_path_style {
  svn_path_local_style = 1,  /* parse path using local (client) conventions */
  svn_path_repos_style,      /* parse path using repository conventions */
  svn_path_url_style         /* parse path using URL conventions */
};

/* Add a COMPONENT (a null-terminated C-string) to PATH.

   If PATH is non-empty, append the appropriate directory separator
   character, and then COMPONENT.  If PATH is empty, simply set it to
   COMPONENT; don't add any separator character.

   If the result ends in a separator character, then remove the separator.

   The separator character is chosen according to STYLE.  For
   svn_path_repos_style, it would be '/'.  For svn_path_local_style on
   a Unix system, it would also be '/'.  */
void svn_path_add_component (svn_string_t *path,
                             const svn_string_t *component,
                             enum svn_path_style style);

/* Same as `svn_path_add_component', except that the COMPONENT argument is 
   a C-style '\0'-terminated string, not an svn_string_t.  */
void svn_path_add_component_nts (svn_string_t *path, 
                                 const char *component,
                                 enum svn_path_style style);

/* Remove one component off the end of PATH. */
void svn_path_remove_component (svn_string_t *path,
                                enum svn_path_style style);


/* Duplicate and return PATH's last component, w/o separator. 
 *
 * If PATH is the root directory, then its last component is still the
 * root directory.  Else if PATH ends with a separator, then PATH's
 * last component is the empty string.
 */
svn_string_t *svn_path_last_component (const svn_string_t *path,
                                       enum svn_path_style style,
                                       apr_pool_t *pool);

/* Divide PATH into *DIRPATH and *BASENAME, return them by reference,
 * in their own storage in POOL.
 *
 * If DIRPATH or BASENAME is null, then that one will not be used.
 *
 * DIRPATH or BASENAME may be PATH's own address, but they may not
 * both be PATH's address; in fact, in general they must not be the
 * same, or the results are undefined.
 *
 * The separator between DIRPATH and BASENAME is not included in
 * either of the new names.
 */
void svn_path_split (const svn_string_t *path,
                     svn_string_t **dirpath,
                     svn_string_t **basename,
                     enum svn_path_style style,
                     apr_pool_t *pool);


/* Return non-zero iff PATH represents the current directory */
int svn_path_is_thisdir (const svn_string_t *path, enum svn_path_style style);

/* Return non-zero iff PATH is empty or represents the current
   directory -- that is, if it is NULL or if prepending it as a
   component to an existing path would result in no meaningful
   change. */
int svn_path_is_empty (const svn_string_t *path, enum svn_path_style style);


/* Remove trailing separators that don't affect the meaning of the path.
   (At some future point, this may make other semantically inoperative
   transformations.) */
void svn_path_canonicalize (svn_string_t *path,
                            enum svn_path_style style);


/* Return an integer greater than, equal to, or less than 0, according
   as PATH1 is greater than, equal to, or less than PATH2. */
int svn_path_compare_paths (const svn_string_t *path1,
                            const svn_string_t *path2,
                            enum svn_path_style style);


/* Return the longest common path shared by both PATH1 and PATH2.  If
   there's no common ancestor, return NULL.  */
svn_string_t *svn_path_get_longest_ancestor (const svn_string_t *path1,
                                             const svn_string_t *path2,
                                             apr_pool_t *pool);

/* Convert RELATIVE path to an absolute path and return the results in
   *PABSOLUTE. */
svn_error_t *
svn_path_get_absolute(svn_string_t **pabsolute,
                      const svn_string_t *relative,
                      apr_pool_t *pool);

/* Return the path part of PATH in *PDIRECTORY, and the file part in *PFILE.
   If PATH is a directory, it will be returned through PDIRECTORY, and *PFILE
   will be the empty string (not NULL). */
svn_error_t *
svn_path_split_if_file(svn_string_t *path,
                       svn_string_t **pdirectory, 
                       svn_string_t **pfile,
                       apr_pool_t *pool);

/* Find the common part of all the paths in TARGETS.  The elements in 
   TARGETS must be existing files or directories, in local path style.
   PBASEDIR will be set to the absolute path that is common to all of the
   items.  Additionally, if PCONDENSED_TARGETS is non-null, it will be 
   set to a list of targets relative to *PBASEDIR, with no overlapping
   targets. If there are no items in TARGETS, *PBASENAME and (if applicable)
   *PCONDENSED_TARGETS will be NULL.  If an item in TARGETS is equal to
   *PBASENAME, it will be returned as an empty string.

    NOTE: There is no guarantee that *PBASENAME is within a working copy. */
svn_error_t *
svn_path_condense_targets(svn_string_t **pbasedir,
                          apr_array_header_t **pcondensed_targets,
                          const apr_array_header_t *targets,
                          apr_pool_t *pool);

/* Decompose PATH into an array of svn_string_t components, allocated
   in POOL.  STYLE indicates the dir separator to split the string on.
   If PATH is absolute, the first component will be a lone dir
   separator (the root directory). */
apr_array_header_t *svn_path_decompose (const svn_string_t *path,
                                        enum svn_path_style style,
                                        apr_pool_t *pool);



/* Test if PATH2 is a child of PATH1.
   If not, return NULL.
   If so, return the "remainder" path.  (The substring which, when
   appended to PATH1, yields PATH2 -- minus the dirseparator. ) */
svn_string_t * svn_path_is_child (const svn_string_t *path1,
                                  const svn_string_t *path2,
                                  apr_pool_t *pool);


#endif /* SVN_PATHS_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
