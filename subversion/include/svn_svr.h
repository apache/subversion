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

#ifndef __SVN_SVR__
#define __SVN_SVR__


#include <svn_types.h>


/******************************************

   The API for general loading of policy config files

******************************************/

/* This object holds three lists that describe the information read in
   from a `svn.conf' file.  Every svn_svr_ routine requires a pointer
   to one of these! */


typedef struct svn_svr_policies_t
{
  svn_prop_t *repository_aliases;    /* an array of props (key/value)
                                        stores aliases for repositories */
  unsigned long repos_len;           /* length of this array */

  svn_string_t *global_restrictions; /* an array of strings describes
                                        global security restrictions
                                        (these are parsed individually) */
  unsigned long restrict_len;        /* length of this array */

  svn_svr_plugin_t *plugins;         /* an array of loaded plugin types */
  unsigned long plugin_len;          /* length of this array */

} svn_svr_policies_t;



/* Makes the server library load a specified config file.  Network
   layers *must* call this routine when first loaded.

   Returns a svn_svr_policies object to be used with *all* server routines. 

*/

svn_svr_policies_t svn_svr_init (svn_string_t *config_file);



/******************************************

   The wrappered Filesystem API

******************************************/

/* For reading history */

svn_ver_t * svn_svr_latest (svn_svr_policies_t *policy,
                            svn_string_t *repos, 
                            svn_user_t *user);

svn_string_t * svn_svr_get_ver_prop (svn_svr_policies_t *policy,
                                     svn_string_t *repos, 
                                     svn_user_t *user, 
                                     svn_ver_t *ver, 
                                     svn_string_t *propname);

svn_proplist_t * svn_svr_get_ver_proplist (svn_svr_policies_t *policy,
                                           svn_string_t *repos, 
                                           svn_user_t *user, 
                                           svn_ver_t *ver);

svn_proplist_t * svn_svr_get_ver_propnames (svn_svr_policies_t *policy,
                                            svn_string_t *repos, 
                                            svn_user_t *user, 
                                            svn_ver_t *ver);
 


/* For reading nodes */

svn_node_t * svn_svr_read (svn_svr_policies_t *policy,
                           svn_string_t *repos, 
                           svn_user_t *user, 
                           svn_ver_t *ver, 
                           svn_string_t *path);

svn_string_t * svn_svn_svr_get_node_prop (svn_svr_policies_t *policy,
                                          svn_string_t *repos, 
                                          svn_user_t *user, 
                                          svn_ver_t *ver, 
                                          svn_string_t *path, 
                                          svn_string_t *propname);

svn_string_t * svn_svr_get_dirent_prop (svn_svr_policies_t *policy,
                                        svn_string_t *repos, 
                                        svn_user_t *user, 
                                        svn_ver_t *ver, 
                                        svn_string_t *path, 
                                        svn_string_t *propname);
 
svn_proplist_t * svn_svr_get_node_proplist (svn_svr_policies_t *policy,
                                            svn_string_t *repos, 
                                            svn_ver_t *ver, 
                                            svn_string_t *path);
 
svn_proplist_t * svn_svr_get_dirent_proplist (svn_svr_policies_t *policy,
                                              svn_string_t *repos, 
                                              svn_user_t *user, 
                                              svn_ver_t *ver, 
                                              svn_string_t *path);
 
svn_proplist_t * svn_svr_get_node_propnames (svn_svr_policies_t *policy,
                                             svn_string_t *repos, 
                                             svn_user_t *user, 
                                             svn_ver_t *ver, 
                                             svn_string_t *path);
 
svn_proplist_t * svn_svr_get_dirent_propnames (svn_svr_policies_t *policy,
                                               svn_string_t *repos, 
                                               svn_user_t *user, 
                                               svn_ver_t *ver, 
                                               svn_string_t *path); 



/* For writing */

svn_token_t svn_svr_submit (svn_svr_policies_t *policy,
                            svn_string_t *repos, 
                            svn_user_t *user, 
                            svn_skelta_t *skelta);
 
svn_ver_t * svn_svr_write (svn_svr_policies_t *policy,
                           svn_string_t *repos, 
                           svn_user_t *user, 
                           svn_delta_t *delta, 
                           svn_token_t token);
 
int svn_svr_abandon (svn_svr_policies_t *policy,
                     svn_string_t *repos, 
                     svn_user_t *user, 
                     svn_token_t token);   /* returns success or failure */ 


/* For difference queries */

svn_delta_t * svn_svr_get_delta (svn_svr_policies_t *policy,
                                 svn_string_t *repos, 
                                 svn_user_t *user, 
                                 svn_ver_t *ver1, 
                                 svn_string_t *path1, 
                                 svn_ver_t *ver2, 
                                 svn_string_t *path2);
 
svn_diff_t * svn_svr_get_diff (svn_svr_policies_t *policy,
                               svn_string_t *repos, 
                               svn_user_t *user, 
                               svn_ver_t *ver1, 
                               svn_string_t *path1, 
                               svn_ver_t *ver2, 
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



/******************************************

   The API for server-side "plug-ins"  (modeled after Apache)

******************************************/


/* A "plug-in" object is a list which describes exactly where custom
   routines should be called from within the server.  We define broad
   categories of hooks as necessary here.

   Each plugin object fills in these fields with either a well-defined
   routine of its own, or a NULL value.

*/

typedef struct svn_svr_plugin_t
{
  /* An authorization function should return NULL (failure) or
     non-NULL (success).  If successful, it should fill in the
     "canonical" filesystem name in the user structure.  */

  char (* authorization_hook) (svn_string_t *repos,
                               svn_user_t *user,
                               svn_svr_action_t action,
                               svn_string_t *path);

  /* This hook isn't fully fleshed out yet */

  (svn_delta_t *) (* conflict_resolve_hook) (svn_delta_t *rejected_delta,
                                             int rejection_rationale);

} svn_svr_plugin_t;






#endif  /* __SVN_SVR__ */

/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */

