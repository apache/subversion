/*
 * paths.c:   a path manipulation library using svn_string_t
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
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


/* See ../include/svn_path.h for details. */
void
svn_path_add_component_nts (svn_string_t *path, 
                            const char *component,
                            enum svn_path_style style)
{
  add_component_internal (path, component, strlen (component), style);
}


/* See ../include/svn_path.h for details. */
void
svn_path_add_component (svn_string_t *path, 
                        const svn_string_t *component,
                        enum svn_path_style style)
{
  add_component_internal (path, component->data, component->len, style);
}


/* See ../include/svn_path.h for details. */
void
svn_path_remove_component (svn_string_t *path, enum svn_path_style style)
{
  /* kff todo: `style' ignored presently. */

  svn_path_canonicalize (path, style);

  if (! svn_string_chop_back_to_char (path, SVN_PATH__REPOS_SEPARATOR))
    svn_string_setempty (path);
}


/* See ../include/svn_path.h for details. */
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


/* See ../include/svn_path.h for details. */
void
svn_path_split (svn_string_t *path, 
                svn_string_t **dirpath,
                svn_string_t **basename,
                enum svn_path_style style,
                apr_pool_t *pool)
{
  *dirpath = svn_string_dup (path, pool);
  *basename = svn_path_last_component (*dirpath, style, pool);
  svn_path_remove_component (*dirpath, style);
}


/* See ../include/svn_path.h for details. */
int
svn_path_isempty (const svn_string_t *path, enum svn_path_style style)
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
