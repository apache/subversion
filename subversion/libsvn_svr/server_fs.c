/*
 * server_fs.c :  wrappers around filesystem calls, and other things
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


#include "svn_svr.h"     /* declarations for this library */
#include "svn_fs.h"      /* the Subversion filesystem API */
#include "svn_string.h"  /* Subversion bytestring routines */





/* svn__svr_expand_repos_name(): 
   Looks up repos alias, returns "true" name.
*/

svn_string_t *
svn__svr_expand_repos_name (svn_svr_policy_t *policy,
                            svn_string_t *repos)
{
  void *val;

  /* Look up the alias name in our repos_aliases hash */
  val = apr_hash_get (policy->repos_aliases, repos->data, repos->len);

  if (val == NULL)   /* If no expansion exists, */
    return repos;    /*   return the original string */
  else
    return (svn_string_t *) val;
}




/* svr_plugin_authorize():
   Loops through all authorization plugins, checking for success.
*/

svn_error_t *
svn_svr_plugin_authorize (svn_fsrequest_t *request)
{
  int i;
  svn_error_t *err;
  svn_svr_plugin_t *current_plugin;
  apr_hash_index_t *hash_index;
  void *key, *val;
  size_t keylen;
  (svn_error_t *) (* current_auth_hook) (svn_string_t *r, svn_user_t *u,
                                         svn_svr_action_t *a, unsigned long v,
                                         svr_string_t *p);

  /* Next:  loop through our policy's array of plugins... */

  for (hash_index = 
         apr_hash_first (request->policy->plugins); /* get first hash entry */
       hash_index;                                 /* NULL if out of entries */
       hash_index = apr_hash_next (hash_index))     /* get next hash entry */
    {
      /* grab a plugin from the list of plugins */
      apr_hash_this (hash_index, &key, &keylen, &val);

      current_plugin = (svn_svr_plugin_t *) val;

      /* grab the authorization routine from this plugin */
      current_auth_hook = current_plugin->authorization_hook;
      
      if (current_auth_hook != NULL)
        {
          /* Call the authorization routine, giving it a chance to
             kill our authorization assumption */
          err = (*my_hook) (request);
          if (err)
            return (err);
        }
    }


  /* If all auth_hooks are successful, double-check that
     user->svn_username is actually filled in! 
     (A good auth_hook should fill it in automatically, though.)
  */

  if (svn_string_isempty (request->user->svn_username))
    {
      /* Using the policy's memory pool, duplicate the auth_username
         string and assign it to svn_username */
      request->user->svn_username = 
        svn_string_dup (request->user->auth_username, request->policy->pool);
    }
  
  return SVN_NO_ERROR;  /* successfully authorized to perform the action! */
}





/* svn__svr_wrap_logic():
   Common logic called by all filesystem wrappers.

       - replaces repository name with "expanded" name in fsrequest struct
       - passes fsrequest struct to plugin authorization routines

   If authorization fails at any level, return the error.
*/

svn_error_t *
svn__svr_wrap_logic (svn_fsrequest_t *request)
{
  svn_error_t *error;

  /* NOTE that irrelevant fields in the request structure (depending
     on the action field) are guaranteed to be NULL.  */

  /* Look up the repos alias, replace with true repository name */
  request->repos = svn__svr_expand_repos_name (request->policy, 
                                               request->repos);

  /* Validate username in request->user->svn_username */
  if (request->user->svn_username == NULL)
    {
      if (request->user->auth_username == NULL)
        {
          request->user->auth_username =
            svn_string_create ("anonymous", request->policy->pool);
        }
      request->user->svn_username = request->user->auth_username;
    }
  
  /* Check authorization hooks within plugins */
  error = svn_svr_plugin_authorize (request);
  if (error)
    return svn_quick_wrap_error (error, "svn_svr_plugin_authorize() failed.");
}




/* 
 *
 *
 * FILESYSTEM WRAPPERS ==================================================
 *
 *
 */


/* Retrieve the latest `svn_ver_t' object in a repository */

svn_error_t *
svn_svr_latest (svn_ver_t **latest_ver,
                svn_svr_policies_t *policy,
                svn_string_t *repos,
                svn_user_t *user)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user, 
                                svn_action_latest,
                                NULL, NULL, NULL, NULL,
                                NULL, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_fs_latest (latest_ver,
                          my_request.repos,                    
                          user->svn_username); 
}




/* Retrieve an entire `node' object from the repository */

svn_error_t *
svn_svr_read (svn_node_t **node,
              svn_svr_policies_t *policy,
              svn_string_t *repos,
              svn_user_t *user,
              unsigned long ver,
              svn_string_t *path)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user,
                                svn_action_read,
                                ver, path, NULL, NULL,
                                NULL, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_fs_read (node,
                        my_request.repos,                    
                        user->svn_username,
                        ver,
                        path); 
}




/* Submit a skelta for approval, get back a token if SVN_NO_ERROR */

svn_error_t *
svn_svr_submit (svn_token_t **token,
                svn_svr_policies_t *policy,
                svn_string_t *repos,
                svn_user_t *user,
                svn_skelta_t *skelta)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user,
                                svn_action_submit,
                                NULL, NULL,   NULL, NULL,
                                NULL, skelta, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_fs_submit (token,
                          my_request.repos,                    
                          user->svn_username,
                          skelta);
}




/* Write an approved delta, using token from submit(). */

svn_error_t *
svn_svr_submit (unsigned long *new_version,
                svn_svr_policies_t *policy,
                svn_string_t *repos,
                svn_user_t *user,
                svn_delta_t *delta,
                svn_token_t *token)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user,
                                svn_action_write,
                                NULL, NULL, NULL, NULL,
                                NULL, NULL, delta, token};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_fs_write (new_version,
                         my_request.repos,                    
                         user->svn_username,
                         delta,
                         token);
}



/* Abandon an already approved skelta, using token. 
   NOTICE that it has no argument-return value, just plain old svn_error_t *.
 */

svn_error_t *
svn_svr_abandon (svn_svr_policies_t *policy,
                 svn_string_t *repos,
                 svn_user_t *user,
                 svn_token_t *token)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user,
                                svn_action_abandon,
                                NULL, NULL, NULL, NULL,
                                NULL, NULL, NULL, token};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_fs_abandon (my_request.repos,                    
                           user->svn_username,
                           token);
}


/* DIFFERENCE QUERIES ---------------------------------------------- */


/* Retrieve a delta describing the difference between two trees in the
   repository */

svn_error_t *
svn_svr_get_delta (svn_delta_t **delta,
                   svn_svr_policies_t *policy,
                   svn_string_t *repos,
                   svn_user_t *user,
                   unsigned long ver1,
                   svn_string_t *path1,
                   unsigned long ver2,
                   svn_string_t *path2)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user,
                                svn_action_get_delta,
                                ver1, path1, ver2, path2,
                                NULL, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_fs_get_delta (delta,
                             my_request.repos,
                             user->svn_username,
                             ver1, path1,
                             ver2, path2);
}



/* Retrieve a GNU-style diff describing the difference between two
   files in the repository */

svn_error_t *
svn_svr_get_diff (svn_diff_t **diff,
                  svn_svr_policies_t *policy,
                  svn_string_t *repos,
                  svn_user_t *user,
                  unsigned long ver1,
                  svn_string_t *path1,
                  unsigned long ver2,
                  svn_string_t *path2)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user,
                                svn_action_get_diff,
                                ver1, path1, ver2, path2,
                                NULL, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_fs_get_diff (diff,
                            my_request.repos,
                            user->svn_username,
                            ver1, path1,
                            ver2, path2);
}





/* PROPERTIES:   Getting individual values ------------------------- */


/* Retrieve the value of a property attached to a version 
   (such as a log message) 
*/

svn_error_t *
svn_svr_get_ver_prop (svn_string_t **propvalue,
                      svn_svr_policies_t *policy,
                      svn_string_t *repos,
                      svn_user_t *user,
                      unsigned long ver,
                      svn_string_t *propname)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user, 
                                svn_action_get_ver_prop,
                                ver, NULL, NULL, NULL,
                                propname, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_get_ver_prop (latest_ver,
                             my_request.repos,                    
                             user->svn_username,
                             ver,
                             propname); 
}


/* Retrieve the value of a node's property */

svn_error_t *
svn_svr_get_node_prop (svn_string_t **propvalue,
                       svn_svr_policies_t *policy,
                       svn_string_t *repos,
                       svn_user_t *user,
                       unsigned long ver,
                       svn_string_t *path,
                       svn_string_t *propname)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user, 
                                svn_action_get_node_prop,
                                ver, path, NULL, NULL,
                                propname, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_get_node_prop (latest_ver,
                              my_request.repos,                    
                              user->svn_username,
                              ver,
                              path,
                              propname); 
}


/* Retrieve the value of a dirent's property */

svn_error_t *
svn_svr_get_dirent_prop (svn_string_t **propvalue,
                         svn_svr_policies_t *policy,
                         svn_string_t *repos,
                         svn_user_t *user,
                         unsigned long ver,
                         svn_string_t *path,
                         svn_string_t *propname)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user, 
                                svn_action_get_dirent_prop,
                                ver, path, NULL, NULL,
                                propname, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_get_dirent_prop (latest_ver,
                                my_request.repos,                    
                                user->svn_username,
                                ver,
                                path,
                                propname); 
}



/* PROPERTIES:   Getting whole property lists  ------------------------- */


/* Retrieve the entire property list of a version. */

svn_error_t *
svn_svr_get_ver_proplist (apr_hash_t **proplist,
                          svn_svr_policies_t *policy,
                          svn_string_t *repos,
                          svn_user_t *user,
                          unsigned long ver)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user,
                                svn_action_get_ver_proplist,
                                ver, NULL, NULL, NULL,
                                NULL, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_get_ver_proplist (proplist,
                                 my_request.repos,                    
                                 user->svn_username,
                                 ver); 
}



/* Retrieve the entire property list of a node. */

svn_error_t *
svn_svr_get_node_proplist (apr_hash_t **proplist,
                           svn_svr_policies_t *policy,
                           svn_string_t *repos,
                           svn_user_t *user,
                           unsigned long ver,
                           svn_string_t *path)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user, 
                                svn_action_get_node_proplist,
                                ver, path, NULL, NULL,
                                NULL, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_get_node_proplist (proplist,
                                  my_request.repos,                    
                                  user->svn_username,
                                  ver,
                                  path); 
}




/* Retrieve the entire property list of a directory entry. */

svn_error_t *
svn_svr_get_dirent_proplist (apr_hash_t **proplist,
                             svn_svr_policies_t *policy,
                             svn_string_t *repos,
                             svn_user_t *user,
                             unsigned long ver,
                             svn_string_t *path)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user, 
                                svn_action_get_dirent_proplist,
                                ver, path, NULL, NULL,
                                NULL, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_get_dirent_proplist (proplist,
                                    my_request.repos,                    
                                    user->svn_username,
                                    ver,
                                    path); 
}



/* PROPERTIES:   Getting list of all property names  ----------------- */



/* Retrieve all propnames of a version */

svn_error_t *
svn_svr_get_ver_propnames (apr_hash_t **propnames,
                           svn_svr_policies_t *policy,
                           svn_string_t *repos,
                           svn_user_t *user,
                           unsigned long ver)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user, 
                                svn_action_get_ver_propnames,
                                ver, NULL, NULL, NULL,
                                NULL, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_get_ver_propnames (propnames,
                                  my_request.repos,                    
                                  user->svn_username,
                                  ver);
}



/* Retrieve all propnames of a node */

svn_error_t *
svn_svr_get_node_propnames (apr_hash_t **propnames,
                            svn_svr_policies_t *policy,
                            svn_string_t *repos,
                            svn_user_t *user,
                            unsigned long ver,
                            svn_string_t *path)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user, 
                                svn_action_get_node_propnames,
                                ver, path, NULL, NULL,
                                NULL, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_get_node_propnames (propnames,
                                   my_request.repos,                    
                                   user->svn_username,
                                   ver,
                                   path);
}



/* Retrieve all propnames of a dirent */

svn_error_t *
svn_svr_get_dirent_propnames (apr_hash_t **propnames,
                              svn_svr_policies_t *policy,
                              svn_string_t *repos,
                              svn_user_t *user,
                              unsigned long ver,
                              svn_string_t *path)
{
  svn_error_t *error;
  svn_fsrequest_t my_request = {policy, repos, user, 
                                svn_action_get_dirent_propnames,
                                ver, path, NULL, NULL,
                                NULL, NULL, NULL, NULL};
                                
  error = svn__svr_wrap_logic (&my_request);

  if (error)
    return error;
  else
    return svn_get_dirent_propnames (propnames,
                                   my_request.repos,                    
                                   user->svn_username,
                                   ver,
                                   path);
}






/*========================================================================

  STATUS / UPDATE

  The status() and update() routines are the only ones which aren't
  simple wrappers for the filesystem API.  They make repeated small
  calls to svn_fs_cmp() and svn_fs_get_delta() respectively (see
  <svn_fs.h>)

*/



/* svn_svr_get_status():

   Input:  a skelta describing working copy's current tree

   Returns: an svn error or SVN_NO_ERROR, and 

            returndata = a skelta describing how the tree is out of date 
*/

svn_error_t * 
svn_svr_get_status (svn_skelta_t **returnskelta,
                    svn_svr_policies_t *policy,
                    svn_string_t *repos, 
                    svn_user_t *user, 
                    svn_skelta_t *skelta)
{
  /* Can't do anything here till we have a working delta/skelta library.  

     We would iterate over the skelta and call svn_fs_cmp() on each
     file to check for up-to-date-ness.  Then we'd built a new skelta
     to send back the results.  */

  return SVN_NO_ERROR;
}
 


/* svn_svn_get_update():

   Input: a skelta describing working copy's current tree.

   Returns:  svn_error_t * or SVN_NO_ERROR, and

            returndata = a delta which, when applied, will actually
            update working copy's tree to latest version.  
*/

svn_error_t * 
svn_svr_get_update (svn_delta_t **returndelta,
                    svn_svr_policies_t *policy,
                    svn_string_t *repos, 
                    svn_user_t *user, 
                    svn_skelta_t *skelta)
{
  /* Can't do anything here till we have a working delta/skelta library.  

     We would iterate over the skelta and call svn_fs_get_delta() on
     each file.  Then we'd built a new composite delta to send back. 
  */

  return SVN_NO_ERROR;
}





/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
