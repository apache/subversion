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

   Used By:   any network layer (such as a Subversion-aware httpd)

*/

#include <svn_types.h>



/* For reading history */

svn_ver_t * svn_svr_latest (svn_string_t *repos, 
                            svn_string_t *user);

svn_string_t * svn_svr_get_ver_prop (svn_string_t *repos, 
                                     svn_string_t *user, 
                                     svn_ver_t *ver, 
                                     svn_string_t *propname);

svn_proplist_t * svn_svr_get_ver_proplist (svn_string_t *repos, 
                                           svn_string_t *user, 
                                           svn_ver_t *ver);

svn_proplist_t * svn_svr_get_ver_propnames (svn_string_t *repos, 
                                            svn_string_t *user, 
                                            svn_ver_t *ver);
 


/* For reading nodes */

svn_node_t * svn_svr_read (svn_string_t *repos, 
                           svn_string_t *user, 
                           svn_ver_t *ver, 
                           svn_string_t *path);

svn_string_t * svn_svn_svr_get_node_prop (svn_string_t *repos, 
                                          svn_string_t *user, 
                                          svn_ver_t *ver, 
                                          svn_string_t *path, 
                                          svn_string_t *propname);

svn_string_t * svn_svr_get_dirent_prop (svn_string_t *repos, 
                                        svn_string_t *user, 
                                        svn_ver_t *ver, 
                                        svn_string_t *path, 
                                        svn_string_t *propname);
 
svn_proplist_t * svn_svr_get_node_proplist (svn_string_t *repos, 
                                            svn_ver_t *ver, 
                                            svn_string_t *path);
 
svn_proplist_t * svn_svr_get_dirent_proplist (svn_string_t *repos, 
                                              svn_string_t *user, 
                                              svn_ver_t *ver, 
                                              svn_string_t *path);
 
svn_proplist_t * svn_svr_get_node_propnames (svn_string_t *repos, 
                                             svn_string_t *user, 
                                             svn_ver_t *ver, 
                                             svn_string_t *path);
 
svn_proplist_t * svn_svr_get_dirent_propnames (svn_string_t *repos, 
                                               svn_string_t *user, 
                                               svn_ver_t *ver, 
                                               svn_string_t *path); 



/* For writing */

svn_token_t svn_svr_submit (svn_string_t *repos, 
                            svn_string_t *user, 
                            svn_skelta_t *skelta);
 
svn_ver_t * svn_svr_write (svn_string_t *repos, 
                           svn_string_t *user, 
                           svn_delta_t *delta, 
                           svn_token_t token);
 
int svn_svr_abandon (svn_string_t *repos, 
                     svn_string_t *user, 
                     svn_token_t token);   /* returns success or failure */ 


/* For difference queries */

svn_delta_t * svn_svr_get_delta (svn_string_t *repos, 
                                 svn_string_t *user, 
                                 svn_ver_t *ver1, 
                                 svn_string_t *path1, 
                                 svn_ver_t *ver2, 
                                 svn_string_t *path2);
 
svn_diff_t * svn_svr_get_diff (svn_string_t *repos, 
                               svn_string_t *user, 
                               svn_ver_t *ver1, 
                               svn_string_t *path1, 
                               svn_ver_t *ver2, 
                               svn_string_t *path2); 


/* The status() and update() routines are the only ones which aren't
simple wrappers for the filesystem API.  They make repeated small
calls to svn_fs_cmp() and svn_fs_get_delta() respectively (see
<svn_fs.h>) */

svn_skelta_t * svn_svr_get_status (svn_string_t *repos, 
                                   svn_string_t *user, 
                                   svn_skelta_t *skelta);
 
svn_delta_t * svn_svr_get_update (svn_string_t *repos, 
                                  svn_string_t *user, 
                                  svn_skelta_t *skelta); 




/* One simple routine for determining permissions and roles.

   We're assuming that the network layer has *already* authenticated
   the user in question, and now simply wants to know if the user is
   permitted to perform an action on some data.

   Input:    a previously authenticated username and auth_method

   Returns:  either NULL if the action is denied, or returns the
             internal Subversion username.  (The server then uses this
             Subversion username to perform the requested action
             against the filesystem.)

   This routine is implemented by a server-side "plug-in" on the back end.

   The default plug-in consults the `svn_security' file, maps the
   auth_user/auth_method pair to an internal Subversion user, and
   looks up user's various roles.  

   An alternate tigris plug-in would actually look up roles in a mySQL
   database and return the same information.

 */

typedef enum svr_action {add, rm, mv, checkout, 
                         commit, import, update} svr_action_t;

svn_string_t * svn_authorize (svn_string_t *repos,
                              svn_string_t *authenticated_username,
                              svn_string_t *authenticated_method,
                              svn_string_t *authenticated_domain,
                              svr_action_t requested_action,
                              svn_string_t *path);
                            


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

