/*
 * svn_svr.h :  public interface for the Subversion Server Library
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


/* ==================================================================== */

/* 
   The Subversion Server Library (libsvn_svr) acts a basic multiplexer
   for the filesystem API calls coming from the client.  Thus it
   provides almost the same public API as libsvn_ra.
   
   Requires:  the Subversion filesystem library (libsvn_fs)
   
   Provides:  
               - wrappers around filesystem calls
               - enforcement of server-side "policies"
               - loadable server-side "plug-ins"
                 (basic authorization plugin included)

   Used By:   any network layer (such as a Subversion-aware httpd)

*/

#ifndef __SVN_SVR_H__
#define __SVN_SVR_H__


#include <svn_types.h>   /* publically declared types */
#include <svn_error.h>   /* private utility in svn_subr/  */
#include <apr_dso.h>     /* defines ap_dso_handle_t */


/* 
   A "plug-in" object is a list which describes exactly where custom
   routines should be called from within the server.  We define broad
   categories of hooks as necessary here, expanding as we go.

   Each plugin object fills in the hook fields with either a well-defined
   routine of its own, or a NULL value.
*/

typedef struct svn_svr_plugin_t
{

  svn_string_t *name;         /* What the plugin calls itself */
  svn_string_t *description;  /* Plugin's documentation string 
                                 (short self-description) */

  ap_dso_handle_t *my_dso;    /* handle on the actual library loaded */

  /* AUTHORIZATION HOOK: 

     An authorization hook returns a ptr to an error structure (if
     authorization fails) which details the reason for failure.  If
     authorization succeeds, return 0.

     If successful, it should fill in the
     "canonical" filesystem name in the user structure.  */

  (svn_error_t *) (* authorization_hook) (svn_string_t *repos,
                                          svn_user_t *user,
                                          svn_svr_action_t action,
                                          unsigned long ver,
                                          svn_string_t *path);

  /* CONFLICT RESOLUTION HOOK:
     
     This hook isn't fully fleshed out yet */

  svn_delta_t * (* conflict_resolve_hook) (svn_delta_t *rejected_delta,
                                             svn_error_t *rationale);

} svn_svr_plugin_t;




/* 
   This object holds three lists that describe the information read in
   from a `svn.conf' file.  Every svn_svr_* routine requires a pointer
   to one of these.  (It's similar to the "global context" objects
   used by APR.)  
*/


typedef struct svn_svr_policies_t
{
  /* A hash which maps repositories -> aliases.
     KEY = bytestring data,  VAL = (svn_string_t *)  */
  ap_hash_t *repos_aliases;

  /* A hash which maps security commands -> command args.
     (These commands describe global security policies.)
     KEY = bytestring data,  VAL = (svn_string_t *)  */
  ap_hash_t *global_restrictions;
  
  /* A hash which maps plugin names -> loaded plugin objects.
     KEY = bytestring data,  VAL = (svn_svr_plugin_t *)   */
  ap_hash_t *plugins;

  /* A convience memory pool, in case a server routine ever needs one */
  ap_pool_t *pool;                   
  
} svn_svr_policies_t;



/* 
   Makes the server library load a specified config file.  Network
   layers *must* call this routine before using the rest of libsvn_svr.

   Returns a svn_svr_policies_t to be used with all server routines. 
*/

svn_svr_policies_t * svn_svr_init (ap_hash_t *configdata, ap_pool_t *pool);


/* Routine which each plugin's init() routine uses to register itself
   in the server's policy structure.  */

svn_error_t * svn_svr_register_plugin (svn_svr_policies_t *policy,
                                       svn_string_t *dso_filename,
                                       svn_svr_plugin_t *new_plugin);


/* Three routines for checking authorization.

   The first one checks global server policy.

   The second one loops through each plugin's authorization hook.

   The third one is a convenience routine, which calls the other two.
*/

svn_error_t * svn_server_policy_authorize (svn_svr_policies_t *policy,
                                           svn_string_t *repos,
                                           svn_user_t *user,
                                           svn_svr_action_t *action,
                                           unsigned long ver,
                                           svn_string_t *path);

svn_error_t * svn_svr_plugin_authorize (svn_svr_policies_t *policy, 
                                        svn_string_t *repos, 
                                        svn_user_t *user, 
                                        svn_svr_action_t *action,
                                        unsigned long ver,
                                        svn_string_t *path);

svn_error_t * svn_svr_authorize (svn_svr_policies_t *policy, 
                                 svn_string_t *repos, 
                                 svn_user_t *user, 
                                 svn_svr_action_t *action,
                                 unsigned long ver,
                                 svn_string_t *path);


/******************************************

   The wrappered Filesystem API

******************************************/

/* For reading history */

svn_error_t * svn_svr_latest (svn_ver_t **latest_ver,
                              svn_svr_policies_t *policy,
                              svn_string_t *repos, 
                              svn_user_t *user);

svn_string_t * svn_svr_get_ver_prop (svn_svr_policies_t *policy,
                                     svn_string_t *repos, 
                                     svn_user_t *user, 
                                     unsigned long ver, 
                                     svn_string_t *propname);

ap_hash_t * svn_svr_get_ver_proplist (svn_svr_policies_t *policy,
                                      svn_string_t *repos, 
                                      svn_user_t *user, 
                                      unsigned long ver);

ap_hash_t * svn_svr_get_ver_propnames (svn_svr_policies_t *policy,
                                       svn_string_t *repos, 
                                       svn_user_t *user, 
                                       unsigned long ver);
 


/* For reading nodes */

svn_node_t * svn_svr_read (svn_svr_policies_t *policy,
                           svn_string_t *repos, 
                           svn_user_t *user, 
                           unsigned long ver, 
                           svn_string_t *path);

svn_string_t * svn_svn_svr_get_node_prop (svn_svr_policies_t *policy,
                                          svn_string_t *repos, 
                                          svn_user_t *user, 
                                          unsigned long ver, 
                                          svn_string_t *path, 
                                          svn_string_t *propname);

svn_string_t * svn_svr_get_dirent_prop (svn_svr_policies_t *policy,
                                        svn_string_t *repos, 
                                        svn_user_t *user, 
                                        unsigned long ver, 
                                        svn_string_t *path, 
                                        svn_string_t *propname);
 
ap_hash_t * svn_svr_get_node_proplist (svn_svr_policies_t *policy,
                                       svn_string_t *repos, 
                                       unsigned long ver, 
                                       svn_string_t *path);
 
ap_hash_t * svn_svr_get_dirent_proplist (svn_svr_policies_t *policy,
                                         svn_string_t *repos, 
                                         svn_user_t *user, 
                                         unsigned long ver, 
                                         svn_string_t *path);
 
ap_hash_t * svn_svr_get_node_propnames (svn_svr_policies_t *policy,
                                        svn_string_t *repos, 
                                        svn_user_t *user, 
                                        unsigned long ver, 
                                        svn_string_t *path);
 
ap_hash_t * svn_svr_get_dirent_propnames (svn_svr_policies_t *policy,
                                          svn_string_t *repos, 
                                          svn_user_t *user, 
                                          unsigned long ver, 
                                          svn_string_t *path); 



/* For writing */

svn_token_t svn_svr_submit (svn_svr_policies_t *policy,
                            svn_string_t *repos, 
                            svn_user_t *user, 
                            svn_skelta_t *skelta);
 
unsigned long * svn_svr_write (svn_svr_policies_t *policy,
                               svn_string_t *repos, 
                               svn_user_t *user, 
                               svn_delta_t *delta, 
                               svn_token_t token);
 
svn_boolean_t svn_svr_abandon (svn_svr_policies_t *policy,
                               svn_string_t *repos, 
                               svn_user_t *user, 
                               svn_token_t token);


/* For difference queries */

svn_delta_t * svn_svr_get_delta (svn_svr_policies_t *policy,
                                 svn_string_t *repos, 
                                 svn_user_t *user, 
                                 unsigned long ver1, 
                                 svn_string_t *path1, 
                                 unsigned long ver2, 
                                 svn_string_t *path2);
 
svn_diff_t * svn_svr_get_diff (svn_svr_policies_t *policy,
                               svn_string_t *repos, 
                               svn_user_t *user, 
                               unsigned long ver1, 
                               svn_string_t *path1, 
                               unsigned long ver2, 
                               svn_string_t *path2); 


/* The status() and update() routines are the only ones which aren't
simple wrappers for the filesystem API.  They make repeated small
calls to svn_fs_cmp() and svn_fs_get_delta() respectively (see
<svn_fs.h>) */

svn_skelta_t * svn_svr_get_status (svn_svr_policies_t *policy,
                                   svn_string_t *repos, 
                                   svn_user_t *user, 
                                   svn_skelta_t *skelta);
 
svn_delta_t * svn_svr_get_update (svn_svr_policies_t *policy,
                                  svn_string_t *repos, 
                                  svn_user_t *user, 
                                  svn_skelta_t *skelta); 





#endif  /* __SVN_SVR_H__ */

/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */

