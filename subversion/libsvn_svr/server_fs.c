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





/* svn__svr_expand_repos_name : NOT EXPORTED.

   Input: a policy and a repository name.  Repository name *might* be
   an abbreviated nickname (listed in `svn.conf' and in the policy
   structure)

   Returns:  the full (proper) repository pathname.

 */

svn_string_t *
svn__svr_expand_repos_name (svn_svr_policy_t *policy,
                            svn_string_t *repos)
{
 /* Loop through policy->repos_aliases hash.
     If there's a match, return new bytestring containing hash value.
     If there's no match, return original string pointer.
  */

  /* NOTE:  if we need a pool, use the one inside policy.  */

  return repos;
}




/* 
   svr_plugin_authorize()

   Loops through all authorization plugins, checking for success.

   Input:  policy + {repos, user, action, path} group

   Returns:  ptr to error structure (if not authorized)
             or 0 if authorized!
            
*/

svn_error_t *
svn_svr_plugin_authorize (svn_fsrequest_t *request)
{
  int i;
  svn_error_t *err;
  svn_svr_plugin_t *current_plugin;
  ap_hash_index_t *hash_index;
  void *key, *val;
  size_t keylen;
  (svn_error_t *) (* current_auth_hook) (svn_string_t *r, svn_user_t *u,
                                         svn_svr_action_t *a, unsigned long v,
                                         svr_string_t *p);

  /* Next:  loop through our policy's array of plugins... */

  for (hash_index = 
         ap_hash_first (request->policy->plugins); /* get first hash entry */
       hash_index;                                 /* NULL if out of entries */
       hash_index = ap_hash_next (hash_index))     /* get next hash entry */
    {
      /* grab a plugin from the list of plugins */
      ap_hash_this (hash_index, &key, &keylen, &val);

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
  
  return SVN_SUCCESS;  /* successfully authorized to perform the action! */
}




/* svn_svr_policy_authorize()

   See if general server `policy' allows an action.

   Input:  policy + {repos, user, action, ver, path} group

   Returns:  error structure (if authorization fails)
             0 (if authorization succeeds)

 */

svn_error_t *
svn_svr_policy_authorize (svn_fs_request_t *request)
{
  /* BIG TODO: loop through policy->global_restrictions array,
     interpreting each restriction and checking authorization */

  return SVN_SUCCESS;
}



/* 
   Convenience routine -- calls the other two authorization routines.

   This routine is called by each "wrappered" filesystem call in this
   library.


   Input:  policy + {repos, user, action, ver, path} group

   Returns:  error structure (if authorization fails)
             0 (if authorization succeeds)

*/

svn_error_t *
svn_svr_authorize (svn_fsrequest_t *request);
{
  svn_error_t *err;
  
  err = svn_svr_policy_authorize (request);
  if (err)
    return (svn_quick_wrap_error
            (err, "Global server policy denied authorization."));

  err = svn_svr_plugin_authorize (request);
  if (err)
    return (svn_quick_wrap_error
            (err, "At least one server plugin denied authorization."));

  return SVN_SUCCESS;  /* successfully authorized! */
}




/* 
   svn_svr_do_fs_call()  :  The UBER-filesystem wrapper routine!

   Input:  void **, and an fsrequest structure

   Returns:  svn_error_t or SVN_SUCCESS, and **returndata filled in.

*/


svn_error_t *
svn_svr_do_fs_call (void **returndata, svn_fsrequest_t *request)
{
  svn_error_t *error;

  /* Look up the repos alias, find the true repository name */
  svn_string_t *repository = svn__svr_expand_repos_name (*request->policy, 
                                                         *request->repos);
  
  /* Check authorization in both server policy & plugins */
  error = svn_svr_authorize (request);
  if (error)
    return svn_quick_wrap_error (error, "svn_svr_authorize() failed.");

  /* Now parse the fsrequest_t, and pass the call through to the filesystem */
  
  switch (request->action)
    {

    case svn_action_latest:
      {
        /* sets returndata = (svn_ver_t *) */
        return 
          svn_fs_latest (returndata,         
                         repository,                   
                         request->user->svn_username); 
      }

    case svn_action_get_ver_prop:
      {
        /* sets returndata = (svn_string_t *) */
        return 
          svn_fs_get_ver_prop (returndata,         
                               repository,                   
                               request->user->svn_username,
                               request->ver1,
                               request->propname); 
      }

    case svn_action_get_ver_proplist:
      {
        /* sets returndata = (ap_hash_t *) */
        return 
          svn_fs_get_ver_proplist (returndata,         
                                   repository,                   
                                   request->user->svn_username,
                                   request->ver1);
      }

    case svn_action_get_ver_propnames:
      {
        /* sets returndata = (ap_hash_t *) */
        return 
          svn_fs_get_ver_propnames (returndata,         
                                    repository,                   
                                    request->user->svn_username,
                                    request->ver1);
      }

    case svn_action_read:
      {
        /* sets returndata = (ap_node_t *) */
        return 
          svn_fs_read (returndata,         
                       repository,                   
                       request->user->svn_username,
                       request->ver1,
                       request->path1);
      }
      
    case svn_action_get_node_prop:
      {
        /* sets returndata = (svn_string_t *) */
        return 
          svn_fs_get_node_prop (returndata,         
                                repository,                   
                                request->user->svn_username,
                                request->ver1,
                                request->path1,
                                request->propname);
      }

    case svn_action_get_dirent_prop:
      {
        /* sets returndata = (svn_string_t *) */
        return 
          svn_fs_get_dirent_prop (returndata,         
                                  repository,                   
                                  request->user->svn_username,
                                  request->ver1,
                                  request->path1,
                                  request->propname);
      }

    case svn_action_get_node_proplist:
      {
        /* sets returndata = (ap_hash_t *) */
        return 
          svn_fs_get_node_proplist (returndata,         
                                    repository,                   
                                    request->user->svn_username,
                                    request->ver1,
                                    request->path1);
      }

    case svn_action_get_dirent_proplist:
      {
        /* sets returndata = (ap_hash_t *) */
        return 
          svn_fs_get_dirent_proplist (returndata,         
                                      repository,                   
                                      request->user->svn_username,
                                      request->ver1,
                                      request->path1);
      }

    case svn_action_get_node_propnames:
      {
        /* sets returndata = (ap_hash_t *) */
        return 
          svn_fs_get_node_propnames (returndata,         
                                     repository,                   
                                     request->user->svn_username,
                                     request->ver1,
                                     request->path1);
      }

    case svn_action_get_dirent_propnames:
      {
        /* sets returndata = (ap_hash_t *) */
        return 
          svn_fs_get_dirent_propnames (returndata,         
                                       repository,                   
                                       request->user->svn_username,
                                       request->ver1,
                                       request->path1);
      }

    case svn_action_submit:
      {        
        /* sets returndata = (svn_token_t *) */
        return 
          svn_fs_submit (returndata,         
                         repository,                   
                         request->user->svn_username,
                         request->skelta);
      }

    case svn_action_write:
      {
        /* sets returndata = (ap_ver_t *) */
        return 
          svn_fs_write (returndata,         
                        repository,                   
                        request->user->svn_username,
                        request->delta,
                        request->token);
      }

    case svn_action_abandon:
      {
        /* sets returndata = NULL (?) */
        return 
          svn_fs_abandon (returndata,         
                          repository,                   
                          request->user->svn_username,
                          request->token);
      }

    case svn_action_get_delta:
      {
        /* sets returndata = (svn_delta_t *) */
        return 
          svn_fs_get_delta (returndata,         
                            repository,                   
                            request->user->svn_username,
                            request->ver1,
                            request->path1,
                            request->ver2,
                            request->path2);
      }

    case svn_action_get_diff:
      {
        /* sets returndata = (svn_diff_t *) */
        return 
          svn_fs_get_diff (returndata,         
                           repository,                   
                           request->user->svn_username,
                           request->ver1,
                           request->path1,
                           request->ver2,
                           request->path2);
      }

    case svn_action_get_status:
      {
        /* SPECIAL CASE : see dedicated server routine below */
        /* sets returndata = (svn_skelta_t *) */
        return 
          svn_svr_get_status (returndata,         
                              repository,                   
                              request->user->svn_username,
                              request->skelta)
      }

    case svn_action_get_update:
      {
        /* SPECIAL CASE : see dedicated server routine below */
        /* sets returndata = (svn_delta_t *) */
        return 
          svn_svr_get_update (returndata,         
                              repository,                   
                              request->user->svn_username,
                              request->skelta)

      }

    default:
      {
        char *msg = 
          ap_psprintf (request->policy->pool,
                       "svn_svr_do_fs_call(): unknown fs action: %d",
                       request->action);
        return svn_create_error (SVN_ERR_UNKNOWN_FS_ACTION,
                                 NULL, msg, NULL, request->policy->pool);
      }

    }

}


/*========================================================================

  STATUS / UPDATE

  The status() and update() routines are the only ones which aren't
  simple wrappers for the filesystem API.  They make repeated small
  calls to svn_fs_cmp() and svn_fs_get_delta() respectively (see
  <svn_fs.h>)

*/



/* Input:  a skelta describing working copy's current tree

   Returns: an svn error or SVN_SUCCESS, and 

            returndata = a skelta describing how the tree is out of date 

*/

svn_error_t * 
svn_svr_get_status (void **returndata,
                    svn_string_t *repos, 
                    svn_user_t *user, 
                    svn_skelta_t *skelta)
{
  /* Can't do anything here till we have a working delta/skelta library.  

     We would iterate over the skelta and call svn_fs_cmp() on each
     file to check for up-to-date-ness.  Then we'd built a new skelta
     to send back the results.  */

  return SVN_SUCCESS;
}
 


/* Input: a skelta describing working copy's current tree.

   Returns:  svn_error_t * or SVN_SUCCESS, and

            returndata = a delta which, when applied, will actually
            update working copy's tree to latest version.  
*/

svn_error_t * 
svn_svr_get_update (void **returndata,
                    svn_string_t *repos, 
                    svn_user_t *user, 
                    svn_skelta_t *skelta)
{
  /* Can't do anything here till we have a working delta/skelta library.  

     We would iterate over the skelta and call svn_fs_get_delta() on
     each file.  Then we'd built a new composite delta to send back. 
  */

  return SVN_SUCCESS;
}





/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
