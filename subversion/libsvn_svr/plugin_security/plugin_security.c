
/* 
   plugin_security.c:  a simple server-side plugin for Subversion
                       which implements basic filesystem authorization.
*/

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

  This plug-in consults the `svn_security' file.

  (An alternate tigris plug-in would actually look up roles in a mySQL
  database and return the same information.)

 */


/* Note: remember to build plugins with -k PIC and in a way that
   libltdl can use them! */


#include <svn_types.h>   /* defines common Subversion data types */
#include <svn_svr.h>     /* defines the server-side plug-in structure */



/*
  Here is a basic example of an "authorization hook" routine.

  Input:   a `user' structure from the network layer
  
  Returns: either NULL if the action is denied, or non-NULL on success.
           
  If successful, fill in the "canonical" username in the user
  structure to use with the filesystem.
  
*/
  
char
svn_internal_authorization (svn_string_t *repos,
                            svn_user_t *user,
                            svr_action_t requested_action,
                            svn_string_t *path)
{

  /* this routine should consult the repository's `svn_security' file
     to make the authorization decision.  */

}


/* Now create a plugin structure; the server will automatically look
   for a plugin structure named after the plugin library.  */


svn_svr_plugin_t plugin_security = 
{ 
  svn_internal_authorization,         /* authorization hook */
  NULL                                /* conflict resolution hook */
};




/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
