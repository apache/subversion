/*
 * paths.c:   a path manipulation library
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
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
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
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
 * individuals on behalf of Collab.Net.
 */



#include "svn_string.h"
#include "svn_path.h"



/*** A path manipulation library. ***/

/* kff todo: this will need to handle escaping/unescaping, for files
   that have slashes in their names. */

/* Helper for svn_path_path_add_component_*, which see. */
static void
add_component_internal (svn_string_t *path,
                        const char *component,
                        size_t len,
                        int style,
                        apr_pool_t *pool)
{
  /* kff todo: `style' ignored presently. */

  char dirsep = SVN_PATH_REPOS_SEPARATOR;

  if (! svn_string_isempty (path))
    svn_string_appendbytes (path, &dirsep, sizeof (dirsep), pool);

  svn_string_appendbytes (path, component, len, pool);
}


/* Like svn_path_path_add_component(), but COMPONENT is a null-terminated
   c-string ("nts"). */
void
svn_path_add_component_nts (svn_string_t *path, 
                            char *component,
                            int style,
                            apr_pool_t *pool)
{
  add_component_internal (path, component, strlen (component), style, pool);
}


/* Extend PATH with new COMPONENT, destructively. */
void
svn_path_add_component (svn_string_t *path, 
                        svn_string_t *component,
                        int style,
                        apr_pool_t *pool)
{
  add_component_internal (path, component->data, component->len, style, pool);
}


/* Remove PATH's deepest COMPONENT, destructively. */
void
svn_path_remove_component (svn_string_t *path, int style)
{
  /* kff todo: `style' ignored presently. */

  if (! svn_string_chop_back_to_char (path, SVN_PATH_REPOS_SEPARATOR))
    svn_string_setempty (path);
}
