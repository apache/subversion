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

#include <subversion/types.h>



/* For reading history */
ver_t svn_svr_latest (repos, user);
prop_t svn_svr_get_ver_prop (repos, user, ver, propname);
proplist_t svn_svr_get_ver_proplist (repos, user, ver);
proplist_t svn_svr_get_ver_propnames (repos, user, ver);
 
/* For reading nodes */
node_t svn_svr_read (repos, user, ver, path);
str_t svn_svn_svr_get_node_prop (repos, user, ver, path, propname);
str_t svn_svr_get_dirent_prop (repos, user, ver, path, propname); 
proplist_t svn_svr_get_node_proplist (repos, ver, path); 
proplist_t svn_svr_get_dirent_proplist (repos, user, ver, path); 
proplist_t svn_svr_get_node_propnames (repos, user, ver, path); 
proplist_t svn_svr_get_dirent_propnames (repos, user, ver, path); 

/* For writing */
token_t svn_svr_submit (repos, user, skelta); 
ver_t svn_svr_write (repos, user, delta, token); 
bool_t svn_svr_abandon (repos, user, token); 

/* For difference queries */
delta_t svn_svr_get_delta (repos, user, ver1, path1, ver2, path2); 
diff_t svn_svr_get_diff (repos, user, ver1, path1, ver2, path2); 

/* The status() and update() routines are the only ones which aren't
simple wrappers for the filesystem API.  They make repeated small
calls to svn_fs_cmp() and svn_fs_get_delta() respectively (see
<svn_fs.h>) */

skelta_t svn_svr_get_status (repos, user, skelta); 
delta_t svn_svr_get_update (repos, user, skelta); 






/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

