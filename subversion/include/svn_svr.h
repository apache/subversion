/*
 * svn_svr.h :  public interface for the Subversion Server Library
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
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
 * software developed by CollabNet (http://www.CollabNet/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
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
 * individuals on behalf of CollabNet.
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

#ifndef SVN_SVR_H
#define SVN_SVR_H


#include <svn_types.h>   /* publically declared types */
#include <svn_error.h>   /* error system  */
#include <svn_parse.h>   /* so folks can use the parser */
#include <apr_dso.h>     /* defines apr_dso_handle_t */


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

  apr_dso_handle_t *my_dso;    /* handle on the actual library loaded */

  /* AUTHORIZATION HOOK: 

     An authorization hook returns a ptr to an error structure (if
     authorization fails) which details the reason for failure.  If
     authorization succeeds, return 0.

     If successful, it should fill in the
     "canonical" filesystem name in the user structure.  */

  (svn_error_t *) (* authorization_hook) (struct svn_fsrequest *request);

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
  /* A hash which maps aliases -> repository paths
     KEY = bytestring data,  VAL = (svn_string_t *)  */
  apr_hash_t *repos_aliases;

  /* A hash which maps plugin names -> loaded plugin objects.
     KEY = bytestring data,  VAL = (svn_svr_plugin_t *)   */
  apr_hash_t *plugins;

  /* A client callback function for reporting warnings.  The first
     argument is simply passed along from the client.  FMT is a
     printf-style format string; it is followed by printf-style
     arguments.  */
  void (*warning) (void *data, char *fmt, ...);

  /* A pointer to pass through to the client's warning callback
     function.  */
  void *warning_data;

  /* A convience memory pool, in case a server routine ever needs one */
  apr_pool_t *pool;                   
  
} svn_svr_policies_t;




/* 
   A structure which represents all the information a client might
   ever need to give to the Subversion filesystem; unused fields are
   NULL.  This is the main argument to each wrappered filesystem call.  
*/


typedef struct svn_fsrequest
{
  svn_svr_policies_t *policy;     /* global server settings */
  svn_string_t *repos;            /* a repository alias-name */
  svn_user_t *user;               /* user making the request */
  svn_svr_action_t action;        /* filesystem call to be authorized */
  unsigned long ver1;
  svn_string_t *path1;            /* (ver, path) specify a repos object */
  unsigned long ver2;
  svn_string_t *path2;            /* needed if doing a diff */
  svn_string_t *propname;         /* a property name, if any is required */
  svn_skelta_t *skelta;           /* needed if doing a status/update */
  svn_delta_t *delta;             /* needed if doing a write */
  svn_token_t *token;             /* need if doing a write */

} svn_fsrequest_t;






/*
  Creates a new, empty policy structure and specifies grandaddy of all
  pools to be used in Subversion Server.  
*/


svn_error_t * svn_svr_init (svn_svr_policies_t **policy, 
                            apr_pool_t *pool);


/* 
   Makes the server library load a specified config file into a policy.
*/


svn_error_t * svn_svr_load_policy (svn_svr_policies_t *policy, 
                                   const char *filename);


/* Utility : load a single plugin and call its init routine, which
   causes the plugin to register itself.

   Ultimately, a new plugin structure ends up snugly nestled in the
   policy. */

svn_error_t * svn_svr_load_plugin (svn_svr_policies_t *policy,
                                   const svn_string_t *path,
                                   const svn_string_t *init_routine);


/* Routine which each plugin's init() routine uses to register itself
   in the server's policy structure.  */

svn_error_t * svn_svr_register_plugin (svn_svr_policies_t *policy,
                                       svn_string_t *dso_filename,
                                       svn_svr_plugin_t *new_plugin);


/* Set the warning callback function for use with policy.  */

extern void svn_svr_warning_callback (svn_server_policies_t *policy,
			              void (*warning) (void *, char *, ...),
				      void *data);


/* Loop through each plugin, calling each "authorization hook", if any
   exist.  */

svn_error_t * svn_svr_plugin_authorize (svn_fsrequest_t *request);


/* Each wrappered filesystem call executes this routine, checking for
   error.  It gives us a single point by which we can intercede
   filesystem calls.  */

svn_error_t * svn__svr_wrap_logic (svn_fsrequest_t *request)



/******************************
   WRAPPERED FILESYSTEM CALLS
*******************************/


/* Retrieve the latest `svn_ver_t' object in a repository */

svn_error_t * svn_svr_latest (svn_ver_t **latest_ver,
                              svn_svr_policies_t *policy,
                              svn_string_t *repos,
                              svn_user_t *user);


/* Retrieve an entire `node' object from the repository */

svn_error_t * svn_svr_read (svn_node_t **node,
                            svn_svr_policies_t *policy,
                            svn_string_t *repos,
                            svn_user_t *user,
                            unsigned long ver,
                            svn_string_t *path);


/* Submit a skelta for approval, get back a token if SVN_NO_ERROR */

svn_error_t * svn_svr_submit (svn_token_t **token,
                              svn_svr_policies_t *policy,
                              svn_string_t *repos,
                              svn_user_t *user,
                              svn_skelta_t *skelta);


/* Write an approved delta, using token from submit(). */

svn_error_t * svn_svr_submit (unsigned long *new_version,
                              svn_svr_policies_t *policy,
                              svn_string_t *repos,
                              svn_user_t *user,
                              svn_delta_t *delta,
                              svn_token_t *token);


/* Abandon an already approved skelta, using token. 
   NOTICE that it has no argument-return value, just plain old svn_error_t *.
 */

svn_error_t * svn_svr_abandon (svn_svr_policies_t *policy,
                               svn_string_t *repos,
                               svn_user_t *user,
                               svn_token_t *token);



/* DIFFERENCE QUERIES ---------------------------------------------- */


/* Retrieve a delta describing the difference between two trees in the
   repository */

svn_error_t * svn_svr_get_delta (svn_delta_t **delta,
                                 svn_svr_policies_t *policy,
                                 svn_string_t *repos,
                                 svn_user_t *user,
                                 unsigned long ver1,
                                 svn_string_t *path1,
                                 unsigned long ver2,
                                 svn_string_t *path2);


/* Retrieve a GNU-style diff describing the difference between two
   files in the repository */

svn_error_t * svn_svr_get_diff (svn_diff_t **diff,
                                svn_svr_policies_t *policy,
                                svn_string_t *repos,
                                svn_user_t *user,
                                unsigned long ver1,
                                svn_string_t *path1,
                                unsigned long ver2,
                                svn_string_t *path2);



/* PROPERTIES:   Getting individual values ------------------------- */


/* Retrieve the value of a property attached to a version 
   (such as a log message) 
*/

svn_error_t * svn_svr_get_ver_prop (svn_string_t **propvalue,
                                    svn_svr_policies_t *policy,
                                    svn_string_t *repos,
                                    svn_user_t *user,
                                    unsigned long ver,
                                    svn_string_t *propname);


/* Retrieve the value of a node's property */

svn_error_t * svn_svr_get_node_prop (svn_string_t **propvalue,
                                     svn_svr_policies_t *policy,
                                     svn_string_t *repos,
                                     svn_user_t *user,
                                     unsigned long ver,
                                     svn_string_t *path,
                                     svn_string_t *propname);


/* Retrieve the value of a dirent's property */

svn_error_t * svn_svr_get_dirent_prop (svn_string_t **propvalue,
                                       svn_svr_policies_t *policy,
                                       svn_string_t *repos,
                                       svn_user_t *user,
                                       unsigned long ver,
                                       svn_string_t *path,
                                       svn_string_t *propname);


/* PROPERTIES:   Getting whole property lists  ------------------------- */


/* Retrieve the entire property list of a version. */

svn_error_t * svn_svr_get_ver_proplist (apr_hash_t **proplist,
                                        svn_svr_policies_t *policy,
                                        svn_string_t *repos,
                                        svn_user_t *user,
                                        unsigned long ver);


/* Retrieve the entire property list of a node. */

svn_error_t * svn_svr_get_node_proplist (apr_hash_t **proplist,
                                         svn_svr_policies_t *policy,
                                         svn_string_t *repos,
                                         svn_user_t *user,
                                         unsigned long ver,
                                         svn_string_t *path);


/* Retrieve the entire property list of a directory entry. */

svn_error_t * svn_svr_get_dirent_proplist (apr_hash_t **proplist,
                                           svn_svr_policies_t *policy,
                                           svn_string_t *repos,
                                           svn_user_t *user,
                                           unsigned long ver,
                                           svn_string_t *path);


/* PROPERTIES:   Getting list of all property names  ----------------- */


/* Retrieve all propnames of a version */

svn_error_t * svn_svr_get_ver_propnames (apr_hash_t **propnames,
                                         svn_svr_policies_t *policy,
                                         svn_string_t *repos,
                                         svn_user_t *user,
                                         unsigned long ver);


/* Retrieve all propnames of a node */

svn_error_t * svn_svr_get_node_propnames (apr_hash_t **propnames,
                                          svn_svr_policies_t *policy,
                                          svn_string_t *repos,
                                          svn_user_t *user,
                                          unsigned long ver,
                                          svn_string_t *path);


/* Retrieve all propnames of a dirent */

svn_error_t * svn_svr_get_dirent_propnames (apr_hash_t **propnames,
                                            svn_svr_policies_t *policy,
                                            svn_string_t *repos,
                                            svn_user_t *user,
                                            unsigned long ver,
                                            svn_string_t *path);


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

svn_error_t * svn_svr_get_status (svn_skelta_t **returnskelta,
                                  svn_svr_policies_t *policy,
                                  svn_string_t *repos, 
                                  svn_user_t *user, 
                                  svn_skelta_t *skelta);



/* svn_svn_get_update():

   Input: a skelta describing working copy's current tree.

   Returns:  svn_error_t * or SVN_NO_ERROR, and

            returndata = a delta which, when applied, will actually
            update working copy's tree to latest version.  
*/

svn_error_t * svn_svr_get_update (svn_delta_t **returndelta,
                                  svn_svr_policies_t *policy,
                                  svn_string_t *repos, 
                                  svn_user_t *user, 
                                  svn_skelta_t *skelta);




#endif  /* SVN_SVR_H */

/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */

