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


#include <svn_svr.h>     /* declarations for this library */
#include <svn_fs.h>      /* the Subversion filesystem API */



/* 
   This routine is called by each "wrappered" filesystem call in this
   library.

   If any authorization hooks return NULL, then return NULL (failure).
   Else return success (1).
*/

char
call_authorization_hooks (svn_svr_policies_t *policy, svn_string_t *repos, 
                          svn_user_t *user, svn_svr_action_t *action,
                          svn_string_t *path)
{
  int i;
  char (* my_hook) (svn_string_t *r, svn_user_t *u,
                    svn_svr_action_t *a, svr_string_t *p);
  char authorized = 1;  /* start off assuming we're authorized */

  /* loop through our plugins */
  for (i = 0; i < (policy->plugin_len); i++)
    {
      /* grab an authorization routine from the plugin */
      my_hook = policy->plugins[i]->authorization_hook;
      
      if (my_hook != NULL)
        {
          /* Call the authorization routine, giving it a chance to
             kill authorization */
          authorized = (*my_hook) (repos, user, action, path);
        }

      if (! authorized) 
        {
          return NULL;  /* no point in calling more auth_hooks! */
          /* TODO: return a more detailed description of failure? */
        }
    }

  /* if all auth_hooks are successful, make sure that
     user->svn_username is actually filled in! */

  if (user->svn_username == NULL)  /* TODO: clarify this test! */
    {
      svn_string_t canonical_name;
      /* TODO:  set_value(&canonical_name, user->auth_username) */
      user->svn_username = &canonical_name;
    }
  
  return 1;  /* successfully authorized to perform the action */
}



/* Called by network layer.  Returns latest version of the repository,
   or NULL if authorization failed.  */

svn_ver_t * 
svn_svr_latest (svn_svr_policies_t *policy, svn_string_t *repos, 
                svn_user_t *user)
{
  svn_ver_t *latest_version;
  char authorized = NULL;

  svn_svr_action_t my_action = foo;  /* TODO:  fix this */

  authorized = call_authorization_hooks (policy, 
                                         repos, 
                                         user,
                                         my_action,
                                         NULL);

  if (! authorized)
    {
      /* play sad music */

      /* we need some kind of graceful authorization failure mechanism.
         what do we return?  NULL?   */
    }
  else
    {
      /* do filesystem call with "canonical" username */
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
