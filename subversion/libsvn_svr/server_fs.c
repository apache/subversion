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
#include <svn_string.h>  /* Subversion bytestring routines */


/* 
   This routine is called by each "wrappered" filesystem call in this
   library; it loops through all plugins and checks to see if an
   action is authorized.

   Input:  a {repos, user, action, path} group

   Returns: TRUE (action is authorized by *all* authorization hooks) 
             or FALSE (denied by at least one authorization hook) */

svn_boolean_t
svr__call_authorization_hooks (svn_svr_policies_t *policy, 
                               svn_string_t *repos, 
                               svn_user_t *user, 
                               svn_svr_action_t *action,
                               svn_string_t *path)
{
  int i;
  svn_svr_plugin_t *current_plugin;
  char (* current_auth_hook) (svn_string_t *r, svn_user_t *u,
                              svn_svr_action_t *a, svr_string_t *p);

  /* start off assuming we're authorized */
  svn_boolean_t authorized = TRUE;  

  /* loop through our policy's array of plugins */
  for (i = 0; i < (policy->plugins->nelts); i++)
    {
      /* grab a plugin from the list of plugins */
      current_plugin = AP_ARRAY_GET_ITEM (policy->plugins, i,
                                          (svn_svr_plugin_t *));

      /* grab the authorization routine from this plugin */
      current_auth_hook = current_plugin->authorization_hook;
      
      if (current_auth_hook != NULL)
        {
          /* Call the authorization routine, giving it a chance to
             kill our authorization assumption */
          authorized = (*my_hook) (repos, user, action, path);
        }

      if (! authorized)  /* bail out if we fail at any point in the loop */
        {
          return FALSE;
        }
    }

  /* If all auth_hooks are successful, double-check that
     user->svn_username is actually filled in! 
     (A good auth_hook should fill it in automatically, though.)
  */

  if (svn_string_isempty (user->svn_username))
    {
      /* Using the policy's memory pool, duplicate the auth_username
         string and assign it to svn_username */
      user->svn_username = svn_string_dup (user->auth_username,
                                           policy->pool);
    }
  
  return TRUE;  /* successfully authorized to perform the action! */
}


/*----------------------------------------------------------------
   READING HISTORY.

   These routines retrieve info from a repository's history array.
   They return FALSE if authorization fails.
*/


/* Returns latest version of the repository */

svn_ver_t * 
svn_svr_latest (svn_svr_policies_t *policy, 
                svn_string_t *repos, 
                svn_user_t *user)
{
  svn_ver_t *latest_version;
  svn_boolean_t authorized = FALSE;

  svn_svr_action_t my_action = foo;  /* TODO:  fix this */

  authorized = svr__call_authorization_hooks (policy, 
                                              repos, 
                                              user,
                                              my_action,
                                              NULL);

  if (! authorized)
    {
      /* play sad music, and generate CUSTOM Subversion errno: */

      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* do filesystem call with "canonical" username */
      latest_version = svn_fs_latest (repos, user->svn_username);
      return latest_version;
    }
}



/* Return the value of a property for a specific version */

svn_string_t * 
svn_svr_get_ver_prop (svn_string_t *repos, 
                      svn_string_t *user, 
                      svn_ver_t *ver, 
                      svn_string_t *propname)
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
