/*
 * paths.c:   a path manipulation library using svn_string_t
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <string.h>
#include "svn_string.h"
#include "svn_path.h"



/* kff todo: hey, it looks like APR may handle some parts of path
   portability for us, and we just get to use `/' everywhere.  Check
   up on this. */
#define SVN_PATH__REPOS_SEPARATOR '/'

void
svn_path_canonicalize (svn_string_t *path, enum svn_path_style style)
{
  /* kff todo: `style' ignored presently. */

  /* At some point this could eliminiate redundant components.
     For now, it just makes sure there is no trailing slash. */

  /* kff todo: maybe should be implemented with a new routine in
     libsvn_string. */

  while ((path->len > 0)
         && (path->data[(path->len - 1)] == SVN_PATH__REPOS_SEPARATOR))
    {
      path->data[(path->len - 1)] = '\0';
      path->len--;
    }
}


static void
add_component_internal (svn_string_t *path,
                        const char *component,
                        size_t len,
                        enum svn_path_style style)
{
  /* kff todo: `style' ignored presently. */

  char dirsep = SVN_PATH__REPOS_SEPARATOR;

  if (! svn_string_isempty (path))
    svn_string_appendbytes (path, &dirsep, sizeof (dirsep));

  svn_string_appendbytes (path, component, len);
  svn_path_canonicalize (path, style);
}


void
svn_path_add_component_nts (svn_string_t *path, 
                            const char *component,
                            enum svn_path_style style)
{
  add_component_internal (path, component, strlen (component), style);
}


void
svn_path_add_component (svn_string_t *path, 
                        const svn_string_t *component,
                        enum svn_path_style style)
{
  add_component_internal (path, component->data, component->len, style);
}


void
svn_path_remove_component (svn_string_t *path, enum svn_path_style style)
{
  /* kff todo: `style' ignored presently. */

  svn_path_canonicalize (path, style);

  if (! svn_string_chop_back_to_char (path, SVN_PATH__REPOS_SEPARATOR))
    svn_string_setempty (path);
}


svn_string_t *
svn_path_last_component (svn_string_t *path,
                         enum svn_path_style style,
                         apr_pool_t *pool)
{
  /* kff todo: `style' ignored presently. */

  apr_size_t i;

  /* kff todo: is canonicalizing the source path lame?  This function
     forces its argument into canonical form, for local convenience.
     But maybe it shouldn't have any effect on its argument.  Can be
     fixed without involving too much allocation, by skipping
     backwards past separators & building the returned component more
     carefully. */
  svn_path_canonicalize (path, style);

  i = svn_string_find_char_backward (path, SVN_PATH__REPOS_SEPARATOR);

  if (i < path->len)
    {
      i += 1;  /* Get past the separator char. */
      return svn_string_ncreate (path->data + i, (path->len - i), pool);
    }
  else
    return svn_string_dup (path, pool);
}


void
svn_path_split (const svn_string_t *path, 
                svn_string_t **dirpath,
                svn_string_t **basename,
                enum svn_path_style style,
                apr_pool_t *pool)
{
  *dirpath = svn_string_dup (path, pool);
  *basename = svn_path_last_component (*dirpath, style, pool);
  svn_path_remove_component (*dirpath, style);
}


int
svn_path_is_empty (const svn_string_t *path, enum svn_path_style style)
{
  /* kff todo: `style' ignored presently. */

  char buf[3];
  buf[0] = '.';
  buf[0] = SVN_PATH__REPOS_SEPARATOR;
  buf[0] = '\0';

  return ((path == NULL)
          || (svn_string_isempty (path))
          || (strcmp (path->data, buf) == 0));
}


int svn_path_compare_paths (const svn_string_t *path1,
                            const svn_string_t *path2,
                            enum svn_path_style style)
{
  size_t min_len = ((path1->len) < (path2->len)) ? path1->len : path2->len;
  size_t i;
  
  /* Skip past common prefix. */
  for (i = 0; (i < min_len) && (path1->data[i] == path2->data[i]); i++)
    ;

  if ((path1->len == path2->len) && (i >= min_len))
    return 0;     /* the paths are the same */
  else if (path1->data[i] == SVN_PATH__REPOS_SEPARATOR)
    return 1;     /* path1 child of path2, parent always comes before child */
  else if (path2->data[i] == SVN_PATH__REPOS_SEPARATOR)
    return -1;    /* path2 child of path1, parent always comes before child */
  else
    return strncmp (path1->data + i, path2->data + i, (min_len - i));
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
