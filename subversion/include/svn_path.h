/*  svn_paths.h: a path manipulation library
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
 * This software may consist of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */


#ifndef SVN_PATHS_H
#define SVN_PATHS_H



#include <apr_pools.h>
#include "svn_string.h"



/*** Notes:
 * 
 * No result path ever ends with a separator, no matter whether the
 * path is a file or directory, because we always canonicalize() it.
 *
 * todo: this library really needs a test suite!
 *
 ***/

/* kff todo: hey, it looks like APR may handle some parts of path
   portability for us, and we just get to use `/' everywhere.  Check
   up on this. */

#define SVN_PATH_REPOS_SEPARATOR '/'

/* Pass this when you want a component added using the local
   pathname conventions. */
#define SVN_PATH_LOCAL_STYLE 1

/* Pass this when you want a component added using repository pathname
   conventions. */
#define SVN_PATH_REPOS_STYLE 2

/* Pass this when you want a component added using URL conventions ('/'). */
#define SVN_PATH_URL_STYLE 3


/* Add a COMPONENT (a null-terminated C-string) to PATH. */
void svn_path_add_component_nts (svn_string_t *path, 
                                 char *component,
                                 int style,
                                 apr_pool_t *pool);

/* Add COMPONENT to PATH. */
void svn_path_add_component (svn_string_t *path,
                             svn_string_t *component,
                             int style,
                             apr_pool_t *pool);

/* Remove one component off the end of PATH. */
void svn_path_remove_component (svn_string_t *path, int style);


/* Duplicate and return PATH's last component, w/o separator. */
svn_string_t *svn_path_last_component (svn_string_t *path,
                                       int style,
                                       apr_pool_t *pool);

/* Return non-zero iff PATH is empty or represents the current
   directory -- that is, if it is NULL or if prepending it as a
   component to an existing path would result in no meaningful
   change. */
int svn_path_isempty (svn_string_t *path, int style);

#endif /* SVN_PATHS_H */
