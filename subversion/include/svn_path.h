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


/* Duplicate and return PATH's last component, w/o separator. */
svn_string_t *svn_path_last_component (svn_string_t *path,
                                       enum svn_path_style style,
                                       apr_pool_t *pool);

/* Divide PATH into DIRPATH and BASENAME, return them by reference,
   in their own storage in POOL.  The separator between DIRPATH and
   BASENAME is not included in either of the new names. */
void svn_path_split (const svn_string_t *path,
                     svn_string_t **dirpath,
                     svn_string_t **basename,
                     enum svn_path_style style,
                     apr_pool_t *pool);


/* Return non-zero iff PATH is empty or represents the current
   directory -- that is, if it is NULL or if prepending it as a
   component to an existing path would result in no meaningful
   change. */
int svn_path_isempty (const svn_string_t *path, enum svn_path_style style);


/* Remove trailing slashes that don't affect the meaning of the path.
   (At some future point, this may make other semantically inoperative
   transformations.) */
void svn_path_canonicalize (svn_string_t *path,
                            enum svn_path_style style);


/* Return an integer greater than, equal to, or less than 0, according
   as PATH1 is greater than, equal to, or less than PATH2. */
int svn_path_compare_paths (const svn_string_t *path1,
                            const svn_string_t *path2,
                            enum svn_path_style style);


#endif /* SVN_PATHS_H */
