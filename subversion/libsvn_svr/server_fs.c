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

   NOTE: The "repos" argument in exported routines can be either a
   nickname (specified in the svn.conf file) or the full pathname of a
   repository.

****************************************************************/


#include <svn_svr.h>     /* declarations for this library */
#include <svn_fs.h>      /* the Subversion filesystem API */
#include <svn_string.h>  /* Subversion bytestring routines */





/* svr__expand_repos_name : NOT EXPORTED.

   Input: a policy and a repository name.  Repository name *might* be
   an abbreviated nickname (listed in `svn.conf' and in the policy
   structure)

   Returns:  the full (proper) repository pathname.

 */

svn_string_t *
svr__expand_repos_name (svn_svr_policy_t *policy,
                        svn_string_t *repos)
{
  /* Loop through policy->repos_aliases hash.
     If there's a match, return new bytestring containing hash value.
     If there's no match, return original string pointer.
  */

  return repos;
}


/* svr__policy_authorize  :  NOT EXPORTED.

   See if general server `policy' allows an action.

   Input:  policy + {repos, user, action, ver, path} group

   Returns:  TRUE (action is authorized by server policy)
             FALSE (action is not allowed)

       This routine is called first in svr__call_authorization_hooks().
 */

svn_boolean_t
svr__policy_authorize (svn_svr_policies_t *policy,
                       svn_string_t *repos,
                       svn_user_t *user,
                       svn_svr_action_t *action,
                       unsigned long ver,
                       svn_string_t *path)
{
  /* TODO: loop through policy->global_restrictions array,
     interpreting each restriction and checking authorization */

  return TRUE;
}


/* 
   svr__authorize :  NOT EXPORTED.

   This routine is called by each "wrappered" filesystem call in this
   library; it first checks global server policy for authorization
   (see svr__policy_authorize()), and then loops through all
   authorization plugins.

   Input:  policy + {repos, user, action, path} group

   Returns: TRUE (action is authorized)
             or FALSE (action is denied)
*/

svn_boolean_t
svr__authorize (svn_svr_policies_t *policy, 
                svn_string_t *repos, 
                svn_user_t *user, 
                svn_svr_action_t *action,
                unsigned long ver,
                svn_string_t *path)
{
  int i;
  svn_svr_plugin_t *current_plugin;
  char (* current_auth_hook) (svn_string_t *r, svn_user_t *u,
                              svn_svr_action_t *a, unsigned long v,
                              svr_string_t *p);

  /* Start off assuming we're authorized! */
  svn_boolean_t authorized = TRUE;  

  /* First: see if our server policy allows the action.  This is a
     kind of "uber" authorization hook that subsumes all authorization
     plugins. */
  authorized = svr__policy_authorize (policy, repos, user, action, ver, path);

  if (! authorized)
    {
      return FALSE;
    }

  /* Next:  loop through our policy's array of plugins... */
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
          authorized = (*my_hook) (repos, user, action, ver, path);
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


/*========================================================================

   READING HISTORY ARRAY.

   These routines retrieve info from a repository's history array.
   They return FALSE if authorization fails.

*/


/* Returns latest version of the repository */

svn_ver_t * 
svn_svr_latest (svn_svr_policies_t *policy, 
                svn_string_t *repos, 
                svn_user_t *user)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = latest;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, NULL, NULL);

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_latest (repository, 
                             user->svn_username));
    }
}



/* Given a version, return a certain property value */

svn_string_t * 
svn_svr_get_ver_prop (svn_svr_policies_t *policy,
                      svn_string_t *repos, 
                      svn_string_t *user, 
                      unsigned long ver, 
                      svn_string_t *propname)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = get_ver_prop;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver, NULL);

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_get_ver_prop (repository, 
                                   user->svn_username,
                                   ver,
                                   propname));
    }
}



/* Retrieve entire proplist of a version */

svn_proplist_t * 
svn_svr_get_ver_proplist (svn_svr_policies_t *policy,
                          svn_string_t *repos, 
                          svn_string_t *user, 
                          unsigned long ver)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = get_ver_proplist;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver, NULL);

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_get_ver_proplist (repository, 
                                       user->svn_username,
                                       ver));
    }
}



/* Return the property names of a version.
   TODO:  Should this return something other than a proplist?
          If not, how is it any different than get_ver_proplist()? 
*/


svn_proplist_t * 
svn_svr_get_ver_propnames (svn_svr_policies_t *policy,
                           svn_string_t *repos, 
                           svn_string_t *user, 
                           unsigned long ver)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = get_ver_propnames;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver, NULL);

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_get_ver_propnames (repository, 
                                        user->svn_username,
                                        ver));
    }
}



/*========================================================================

   READING NODES.

   These routines retrieve info from a node in the filesystem.
   They return FALSE if authorization fails.

*/



/* Return the entire contents of a node */

svn_node_t * 
svn_svr_read (svn_svr_policies_t *policy,
              svn_string_t *repos, 
              svn_user_t *user, 
              unsigned long ver, 
              svn_string_t *path)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = read;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver, path);

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_read (repository, 
                           user->svn_username,
                           ver,
                           path));
    }
}



/* Return the value of a node's propery */

svn_string_t * 
svn_svr_get_node_prop (svn_svr_policies_t *policy,
                       svn_string_t *repos, 
                       svn_user_t *user, 
                       unsigned long ver, 
                       svn_string_t *path, 
                       svn_string_t *propname)

{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = get_node_prop;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver, path);

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_get_node_prop (repository, 
                                    user->svn_username,
                                    ver,
                                    path,
                                    propname));
    }  
}


/* Get the value of a dirent's property */

svn_string_t * 
svn_svr_get_dirent_prop (svn_svr_policies_t *policy,
                         svn_string_t *repos, 
                         svn_user_t *user, 
                         unsigned long ver, 
                         svn_string_t *path, 
                         svn_string_t *propname)
{
    /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = get_dirent_prop;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver, path);

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_get_dirent_prop (repository, 
                                      user->svn_username,
                                      ver,
                                      path,
                                      propname));
    }  
}
 


/* Get a node's entire proplist */

svn_proplist_t * 
svn_svr_get_node_proplist (svn_svr_policies_t *policy,
                           svn_string_t *repos,
                           svn_user_t *user,
                           unsigned long ver, 
                           svn_string_t *path)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = get_node_proplist;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver, path);

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_get_node_proplist (repository, 
                                        user->svn_username,
                                        ver,
                                        path));
    }
}
 



/* Get a dirent's entire proplist */

svn_proplist_t * 
svn_svr_get_dirent_proplist (svn_svr_policies_t *policy,
                             svn_string_t *repos, 
                             svn_user_t *user, 
                             unsigned long ver, 
                             svn_string_t *path)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = get_dirent_proplist;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver, path);

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_get_dirent_proplist (repository, 
                                          user->svn_username,
                                          ver,
                                          path));
    }
}
 


/* Get a list of a node's property names */

svn_proplist_t * 
svn_svr_get_node_propnames (svn_svr_policies_t *policy,
                            svn_string_t *repos, 
                            svn_user_t *user, 
                            unsigned long ver, 
                            svn_string_t *path)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = get_node_propnames;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver, path);

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_get_node_propnames (repository, 
                                         user->svn_username,
                                         ver,
                                         path));
    }
}
 


/* Get a list of a dirent's property names */
     
svn_proplist_t * 
svn_svr_get_dirent_propnames (svn_svr_policies_t *policy,
                              svn_string_t *repos, 
                              svn_user_t *user, 
                              unsigned long ver, 
                              svn_string_t *path)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = get_dirent_propnames;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver, path);

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_get_dirent_propnames (repository, 
                                           user->svn_username,
                                           ver,
                                           path));
    }
}


/*========================================================================

   WRITING.

   These routines for writing deltas into the filesystem.
   They return FALSE if authorization fails.

*/



/* Submit a skelta for approval; on success, returns a transaction
   token. */

svn_token_t 
svn_svr_submit (svn_svr_policies_t *policy,
                svn_string_t *repos, 
                svn_user_t *user, 
                svn_skelta_t *skelta)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = submit;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, NULL, NULL);
  /* TODO: perhaps the "path" argument to svr__authorize should be
     somehow read out of the skelta?  */

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_submit (repository,
                             user->svn_username,
                             skelta));
    }
}
 


/* Use the token to apply the delta to the filesystem.  
   On success, returns the new version number of the repository. */

unsigned long
svn_svr_write (svn_svr_policies_t *policy,
               svn_string_t *repos, 
               svn_user_t *user, 
               svn_delta_t *delta, 
               svn_token_t token)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = write;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, NULL, NULL);
  /* TODO: perhaps the "path" argument to svr__authorize should be
     somehow read out of the delta?  

     Actually, nobody can call this routine without a token, which
     means they've already been authorized to submit().  Is there any
     point in differentiating authorization between submit() and
     write()?  */

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_write (repository,
                            user->svn_username,
                            delta,
                            token));
    }
}
 


/* Abandon an approved, pending token */

svn_boolean_t
svn_svr_abandon (svn_svr_policies_t *policy,
                 svn_string_t *repos, 
                 svn_user_t *user, 
                 svn_token_t token)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = abandon;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, NULL, NULL);
  /* TODO: 
     
     What does it mean to have (or *not* have) permission to abandon an
     approved token?  :)
  */

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_abandon (repository,
                              user->svn_username,
                              token));
    }  
}


/*========================================================================

  DIFFERENCE QUERIES.

  Report information on differences between objects in the repository.
  They return FALSE if authorization fails.

*/


/* Return a delta that describes the difference between two trees in
   the repository.  */

svn_delta_t * 
svn_svr_get_delta (svn_svr_policies_t *policy,
                   svn_string_t *repos, 
                   svn_user_t *user, 
                   unsigned long ver1, 
                   svn_string_t *path1, 
                   unsigned long ver2, 
                   svn_string_t *path2)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = get_delta;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver1, path1);
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver2, path2);
  /* 
     TODO: notice I'm calling the authorize routine twice, checking
     *both* paths and versions.  Is this right?
  */

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_get_delta (repository,
                                user->svn_username,
                                ver1, path1, ver2, path2));
    }  
}
 


/* Return a GNU diff describing the difference between two files */

svn_diff_t * 
svn_svr_get_diff (svn_svr_policies_t *policy,
                  svn_string_t *repos, 
                  svn_user_t *user, 
                  unsigned long ver1, 
                  svn_string_t *path1, 
                  unsigned long ver2, 
                  svn_string_t *path2)
{
  /* Convert "repos" into real pathname */
  svn_string_t *repository = svr__expand_repos_name (policy, repos);

  /* Check authorization, both server policy & auth hooks */
  svn_boolean_t authorized = FALSE;
  svn_svr_action_t my_action = get_diff;
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver1, path1);
  authorized = svr__authorize (policy, repository, user, 
                               my_action, ver2, path2);
  /* 
     TODO: notice I'm calling the authorize routine twice, checking
     *both* paths and versions.  Is this right?
  */

  if (! authorized)
    {
      /* Generate CUSTOM Subversion errno: */
      svn_handle_error (svn_create_error (SVN_ERR_NOT_AUTHORIZED,
                                          FALSE,
                                          policy->pool));
      return FALSE;
    }
  else
    {
      /* Do filesystem call with "canonical" username */
      return (svn_fs_get_diff (repository,
                               user->svn_username,
                               ver1, path1, ver2, path2));
    }  
}


/*========================================================================

  STATUS / UPDATE

  The status() and update() routines are the only ones which aren't
  simple wrappers for the filesystem API.  They make repeated small
  calls to svn_fs_cmp() and svn_fs_get_delta() respectively (see
  <svn_fs.h>)

  They return FALSE if authorization fails.

*/



/* Input:  a skelta describing working copy's current tree

   Output: a skelta describing exactly how the tree is out of date 

*/

svn_skelta_t * 
svn_svr_get_status (svn_svr_policies_t *policy,
                    svn_string_t *repos, 
                    svn_user_t *user, 
                    svn_skelta_t *skelta)
{
  /* Can't do anything here till we have a working delta/skelta library.  

     We would iterate over the skelta and call svn_fs_cmp() on each
     file to check for up-to-date-ness.  Then we'd built a new skelta
     to send back the results.  */
}
 

/* Input: a skelta describing working copy's current tree.

   Output: a delta which, when applied, will actually update working
   copy's tree to latest version.
*/

svn_delta_t * 
svn_svr_get_update (svn_svr_policies_t *policy,
                    svn_string_t *repos, 
                    svn_user_t *user, 
                    svn_skelta_t *skelta)
{
  /* Can't do anything here till we have a working delta/skelta library.  

     We would iterate over the skelta and call svn_fs_get_delta() on
     each file.  Then we'd built a new composite delta to send back. 
  */
}





/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
