/*
 * paths.c:   a path manipulation library using svn_stringbuf_t
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



#include <string.h>
#include <assert.h>
#include "svn_string.h"
#include "svn_path.h"
#include "svn_private_config.h"         /* for SVN_PATH_LOCAL_SEPARATOR */


/* todo: Though we have a notion of different types of separators for
 * the local path style, there currently is no logic in place to
 * account for cases where the separator for one system is a valid
 * non-separator character for others.  For example, a backslash (\)
 * character is a legal member of a Unix filename, but is the
 * separator character for Windows platforms (the *file* foo\bar.c on
 * Unix machine runs the risk of being interpreted by a Windows box as
 * file bar.c in a directory foo). */

/* kff todo: hey, it looks like APR may handle some parts of path
   portability for us, and we just get to use `/' everywhere.  Check
   up on this. */

/* Path separator defines. */
/* SVN_PATH_LOCAL_SEPARATOR (the local filesystem path separator)
   _should_ have been defined external this file by the build stuffs */
#define SVN_PATH_REPOS_SEPARATOR '/' /* repository separators */
#define SVN_PATH_URL_SEPARATOR   '/' /* url separators */


static char
get_separator_from_style (enum svn_path_style style)
{
  switch (style)
    {
    case svn_path_local_style:
      /* local style - path separators used by local filesystem */
      return SVN_PATH_LOCAL_SEPARATOR;

    case svn_path_url_style:
      /* url style - path separators used in urls */
      return SVN_PATH_URL_SEPARATOR;

    default:
    case svn_path_repos_style:
      /* repos style - separators used in repository paths */
      return SVN_PATH_REPOS_SEPARATOR;
    }
  /* default case = repos style (we should never hit this...) */
  return SVN_PATH_REPOS_SEPARATOR;
}
 


void
svn_path_canonicalize (svn_stringbuf_t *path, enum svn_path_style style)
{
  char dirsep = get_separator_from_style (style);

  /* At some point this could eliminiate redundant components.
     For now, it just makes sure there is no trailing slash. */

  /* kff todo: maybe should be implemented with a new routine in
     libsvn_string. */

  while ((path->len > 0)
         && (path->data[(path->len - 1)] == dirsep))
    {
      path->data[(path->len - 1)] = '\0';
      path->len--;
    }
}


static void
add_component_internal (svn_stringbuf_t *path,
                        const char *component,
                        size_t len,
                        enum svn_path_style style)
{
  char dirsep = get_separator_from_style (style);

  if (! svn_string_isempty (path))
    svn_string_appendbytes (path, &dirsep, sizeof (dirsep));

  svn_string_appendbytes (path, component, len);
  svn_path_canonicalize (path, style);
}


void
svn_path_add_component_nts (svn_stringbuf_t *path, 
                            const char *component,
                            enum svn_path_style style)
{
  add_component_internal (path, component, strlen (component), style);
}


void
svn_path_add_component (svn_stringbuf_t *path, 
                        const svn_stringbuf_t *component,
                        enum svn_path_style style)
{
  add_component_internal (path, component->data, component->len, style);
}


void
svn_path_remove_component (svn_stringbuf_t *path, enum svn_path_style style)
{
  char dirsep = get_separator_from_style (style);

  svn_path_canonicalize (path, style);

  if (! svn_string_chop_back_to_char (path, dirsep))
    svn_string_setempty (path);
}


svn_stringbuf_t *
svn_path_last_component (const svn_stringbuf_t *path,
                         enum svn_path_style style,
                         apr_pool_t *pool)
{
  char dirsep = get_separator_from_style (style);

  apr_size_t i
    = svn_string_find_char_backward (path, dirsep);

  if (i < path->len)
    {
      i += 1;  /* Get past the separator char. */
      return svn_string_ncreate (path->data + i, (path->len - i), pool);
    }
  else
    return svn_string_dup (path, pool);
}



void
svn_path_split (const svn_stringbuf_t *path, 
                svn_stringbuf_t **dirpath,
                svn_stringbuf_t **basename,
                enum svn_path_style style,
                apr_pool_t *pool)
{
  svn_stringbuf_t *n_dirpath, *n_basename;

  assert (dirpath != basename);

  if (dirpath)
    {
      n_dirpath = svn_string_dup (path, pool);
      svn_path_remove_component (n_dirpath, style);
    }

  if (basename)
    n_basename = svn_path_last_component (path, style, pool);

  if (dirpath)
    *dirpath = n_dirpath;

  if (basename)
    *basename = n_basename;
}


int
svn_path_is_thisdir (const svn_stringbuf_t *path, enum svn_path_style style)
{
  char dirsep = get_separator_from_style (style);

  if ((path->len == 1) && (path->data[0] == '.'))
    return 1;

  if ((path->len == 2) && (path->data[0] == '.') && (path->data[1] == dirsep))
    return 1;

  return 0;
}


int
svn_path_is_empty (const svn_stringbuf_t *path, enum svn_path_style style)
{
  return ((path == NULL)
          || (svn_string_isempty (path))
          || (svn_path_is_thisdir (path, style)));
}


int
svn_path_compare_paths (const svn_stringbuf_t *path1,
                        const svn_stringbuf_t *path2,
                        enum svn_path_style style)
{
  size_t min_len = ((path1->len) < (path2->len)) ? path1->len : path2->len;
  size_t i;
  char dirsep = get_separator_from_style (style);
  
  /* Skip past common prefix. */
  for (i = 0; (i < min_len) && (path1->data[i] == path2->data[i]); i++)
    ;

  if ((path1->len == path2->len) && (i >= min_len))
    return 0;     /* the paths are the same */
  else if (path1->data[i] == dirsep)
    return 1;     /* path1 child of path2, parent always comes before child */
  else if (path2->data[i] == dirsep)
    return -1;    /* path2 child of path1, parent always comes before child */
  else
    return strncmp (path1->data + i, path2->data + i, (min_len - i));
}



svn_stringbuf_t *
svn_path_get_longest_ancestor (const svn_stringbuf_t *path1,
                               const svn_stringbuf_t *path2,
                               enum svn_path_style style,
                               apr_pool_t *pool)
{
  svn_stringbuf_t *common_path;
  char dirsep = get_separator_from_style (style);
  int i = 0;
  int last_dirsep = 0;

  /* If either string is NULL or empty, we must go no further. */
  
  if ((! path1) || (! path2)
      || (svn_string_isempty (path1)) || (svn_string_isempty (path2)))
    return NULL;
  
  while (path1->data[i] == path2->data[i])
    {
      /* Keep track of the last directory separator we hit. */
      if (path1->data[i] == dirsep)
        last_dirsep = i;

      i++;

      /* If we get to the end of either path, break out. */
      if ((i == path1->len) || (i == path2->len))
        break;
    }

  /* last_dirsep is now the offset of the last directory separator we
     crossed before reaching a non-matching byte.  i is the offset of
     that non-matching byte. */
  if (((i == path1->len) && (path2->data[i] == dirsep)) 
      || ((i == path2->len) && (path1->data[i] == dirsep))
      || ((i == path1->len) && (i == path2->len)))
    common_path = svn_string_ncreate (path1->data, i, pool);
  else
    common_path = svn_string_ncreate (path1->data, last_dirsep, pool);
    
  svn_path_canonicalize (common_path, svn_path_local_style);

  return common_path;
}


/* Test if PATH2 is a child of PATH1.

   If not, return NULL.
   If so, return the "remainder" path.  (The substring which, when
   appended to PATH1, yields PATH2.) */
svn_stringbuf_t *
svn_path_is_child (const svn_stringbuf_t *path1,
                   const svn_stringbuf_t *path2,
                   enum svn_path_style style,
                   apr_pool_t *pool)
{
  char dirsep = get_separator_from_style (style);
  int i = 0;
      
  /* If either path is empty, return NULL. */
  if ((! path1) || (! path2)
      || (svn_string_isempty (path1)) || (svn_string_isempty (path2)))
    return NULL;
  
  /* If path2 isn't longer than path1, return NULL.  */
  if (path2->len <= path1->len)
    return NULL;

  /* Run through path1, and if it ever differs from path2, return
     NULL. */
  while (i < path1->len)
    {
      if (path1->data[i] != path2->data[i])
        return NULL;

      i++;
    }

  /* If we get all the way to the end of path1 with the contents the
     same as in path2, and either path1 ends in a directory separator,
     or path2's next character is a directory separator followed by
     more pathy stuff, then path2 is a child of path1. */
  if (i == path1->len)
    {
      if (path1->data[i - 1] == dirsep)
        return svn_string_ncreate (path2->data + i, 
                                   path2->len - i, 
                                   pool);
      else if (path2->data[i] == dirsep)
        return svn_string_ncreate (path2->data + i + 1,
                                   path2->len - i - 1,
                                   pool);
    }

  return NULL;
}


/* helper for svn_path_decompose, because apr arrays are so darn ugly. */
static void
store_component (apr_array_header_t *array,
                 char *bytes,
                 apr_size_t len,
                 apr_pool_t *pool)
{
  svn_stringbuf_t **receiver;
  
  svn_stringbuf_t *component = svn_string_ncreate (bytes, len, pool);

  receiver = (svn_stringbuf_t **) apr_array_push (array);
  *receiver = component;
}


apr_array_header_t *
svn_path_decompose (const svn_stringbuf_t *path,
                    enum svn_path_style style,
                    apr_pool_t *pool)
{
  int i, oldi;

  apr_array_header_t *components = 
    apr_array_make (pool, 1, sizeof(svn_stringbuf_t *));

  char dirsep = get_separator_from_style (style);
  i = oldi = 0;

  if (svn_path_is_empty (path, style))
    return components;

  /* If PATH is absolute, store the '/' as the first component. */
  if (path->data[i] == dirsep)
    {
      store_component (components, path->data + i, 1, pool);
      i++;
      oldi++;
    }

  while (i <= path->len)
    {
      if ((path->data[i] == dirsep) || (path->data[i] == '\0'))
        {
          store_component (components, path->data + oldi, i - oldi, pool);
          i++;
          oldi = i;  /* skipping past the dirsep */
          continue;
        }
      i++;
    }

  return components;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */


