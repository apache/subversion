
/* 
   plugin_security.c:  a simple server-side plugin for Subversion
                       which implements basic filesystem authorization.


   We're assuming that the network layer has *already* authenticated
   the user in question, and now simply wants to know if the user is
   permitted to perform an action on some data.

  This plug-in consults the `svn_security' file.

  (An alternate tigris plug-in would actually look up roles in a mySQL
  database and return the same information.)

 */


/* Note: remember to build plugins with -k PIC and in a way that
   libltdl can use them! */


#include <svn_types.h>
#include <svn_svr.h>



/*
  Input:    a previously authenticated username, auth_method, auth domain
  
  Returns: either NULL if the action is denied, or returns the
           internal Subversion username.  (The server then uses this
           Subversion username to perform the requested action against
           the filesystem.)
*/
  
svn_string_t * 
svn_internal_authorization (svn_string_t *repos,
                            svn_string_t *authenticated_username,
                            svn_string_t *authenticated_method,
                            svn_string_t *authenticated_domain,
                            svr_action_t requested_action,
                            svn_string_t *path)
{

  /* this routine should consult the repository's `svn_security' file
     to make the authorization decision.  */

}



/* Now declare a new plugin object */

svn_svr_plugin plugin_security = 
{ 
  svn_internal_authorization,         /* authorization hook */
  NULL                                /* conflict resolution hook */
};


