
/* 
   plugin_security.c:  a server-side plugin for Subversion which
                       implements basic filesystem authorization.  */

/*
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


/*
   We're assuming that the network layer has *already* authenticated
   the user in question, and now simply wants to know if the user is
   permitted to perform an action on some data.

  This plug-in consults the `svn_security' file for authorization;
  each repository has its own svn_security file describing ACLs.

*/


/* Note: remember to build plugins with -k PIC and in a way that
   libapr can use them as DSO's! */


#include "svn_svr.h"     /* describes server architecture */


/*
  Here is a basic example of an "authorization hook" routine.

  Input:   a `user' structure from the network layer
  
  Returns: either NULL if the action is denied, or non-NULL on success.
           
  If successful, fill in the "canonical" username in the user
  structure to use with the filesystem.
  
*/
  
svn_error_t *
svn_internal_authorization (svn_string_t *repos,
                            svn_user_t *user,
                            svr_action_t requested_action,
                            unsigned long ver,
                            svn_string_t *path)
{

  /* this routine should consult the repository's `svn_security' file
     to make the authorization decision.  */

  /* this routine should read the file by calling *directly* into
     libsvn_fs, and not call svn_svr_read(); svn_svr_read() checks for
     authorization, which would put us in an infinte loop! */

  return SVN_SUCCESS;
}


/* The routine called by the server, which causes the plugin to
   register itself */

svn_error_t *
plugin_security_init (svn_svr_policies_t *policy,
                      ap_dso_handle_t *dso,
                      ap_pool_t *pool)
{
  svn_error_t *err;

  /* First:  create an instance of this plugin */
  svn_svr_plugin_t *newplugin = 
    (svn_svr_plugin_t *) ap_palloc (pool, sizeof(svn_svr_plugin_t));

  /* Fill in the fields of the plugin */
  newplugin->name = svn_string_create ("plugin_security", pool);
  newplugin->description = 
    svn_string_create ("Authorizes via ACLs in each repository's `svn_security' file.", pool);
  newplugin->my_dso = dso;

  newplugin->authorization_hook = svn_internal_authorization;
  newplugin->conflict_resolve_hook = NULL;

  /* Finally, register the new plugin in the server's global policy struct */
  err = svn_svr_register_plugin (policy, newplugin);
  RETURN_IF_ERROR(err);

  return SVN_SUCCESS;
}



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
