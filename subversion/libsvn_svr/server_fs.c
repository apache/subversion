/*
 * server_fs.c :  wrappers around filesystem calls
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


/* **************************************************************
   
   The main idea here is that filesystem calls are "wrappered", giving
   the server library the chance to check for authorization and
   execute any policies that may supercede the request.

****************************************************************/


#include <svn_types.h>   /* all of the basic data types */
#include <svn_svr.h>     /* declarations for this library */
#include <svn_fs.h>      /* the Subversion filesystem API */



/* 
   This routine is called by each "wrappered" filesystem call in this
   library.

   If any authorization hooks return NULL, then return NULL.
   Else return success (non-NULL).  
*/

char
call_authorization_hooks (svn_string_t *repos, svn_user_t *user)
{
  /* loop through our plugins, calling authorization hooks */

  /* at the end, if all plugins are successful, make sure that
     user->svn_username is actually filled in! */
}



/* Called by network layer.  Returns latest version of the repository,
   or NULL if authorization failed.  */

svn_ver_t * 
svn_svr_latest (svn_string_t *repos, svn_user_t *user)
{
  svn_ver_t *latest_version;
  char authorized = NULL;

  authorized = call_authorization_hooks (repos, user);

  if (! authorized)
    {
      /* play sad music */

      /* we need some kind of graceful authorization failure mechanism.
         what do we return?  NULL?   */
    }
  else
    {
      latest_version = svn_fs_latest (repos, user->svn_username);
      return latest_version;
    }
}



svn_string_t * 
svn_svr_get_ver_prop (svn_string_t *repos, svn_string_t *user, 
                      svn_ver_t *ver, svn_string_t *propname)
{

}


svn_proplist_t * 
svn_svr_get_ver_proplist (svn_string_t *repos, svn_string_t *user, 
                          svn_ver_t *ver)
{

}



svn_proplist_t * 
svn_svr_get_ver_propnames (svn_string_t *repos, svn_string_t *user, 
                           svn_ver_t *ver)
{

}













/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
