/*
 * auth_example2.c: another simple demo of svn_auth.c / libsvn_auth API
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */


/* A quick test of the two built-in libsvn_auth simple-credential providers.

   On my FreeBSD system, I compile at the commandline with:

   cc -g -o auth_example2 auth_example2.c \
   -I/usr/local/include/subversion-1  -I/usr/local/apache2/include \
   -L/usr/local/lib -L/usr/local/apache2/lib \
   -lapr-0 -lsvn_auth-1 -lsvn_subr-1 -lsvn_wc-1 \
   -rpath /usr/local/apache2/lib

*/

#include <apr_file_io.h>
#include <apr_general.h>
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_auth.h"
#include "svn_wc.h"
#include "svn_utf.h"


/* A callback of type svn_client_prompt_t, completely stolen from the
   svn commandline client application. */
svn_error_t *
prompt_user (const char **result,
             const char *prompt,
             svn_boolean_t hide,
             void *baton,
             apr_pool_t *pool)
{
  apr_status_t status;
  apr_file_t *fp;
  char c;
  const char *prompt_native;

  /* ### baton is NULL... the commandline client app doesn't need one,
   but a GUI app probably would. */

  svn_stringbuf_t *strbuf = svn_stringbuf_create ("", pool);

  status = apr_file_open_stdin (&fp, pool);
  if (status)
    return
      svn_error_create (status, NULL,
                        "svn_cl__prompt_user:  couldn't open stdin.");

  SVN_ERR (svn_utf_cstring_from_utf8 (&prompt_native, prompt, pool));

  /* ### implement the HIDE flag later using apr_getpassword or
     something. */
  if (! hide)
    {
      printf (prompt_native);
      fflush (stdout);

      while (1)
        {
          status = apr_file_getc (&c, fp);
          if (status && ! APR_STATUS_IS_EOF(status))
            return svn_error_create (status, NULL, "error reading stdin.");
          if ((c == '\n') || (c == '\r'))
            break;
          
          svn_stringbuf_appendbytes (strbuf, &c, 1);
        }
    }
  else
    {
      size_t bufsize = 300;
      svn_stringbuf_ensure (strbuf, bufsize);

      status = apr_password_get (prompt_native, strbuf->data, &bufsize);
      if (status)
        return svn_error_create (status, NULL,
                                 "error from apr_password_get().");      
    }

  SVN_ERR (svn_utf_cstring_to_utf8 ((const char **)result, strbuf->data,
                                    NULL, pool));

  return SVN_NO_ERROR;
}





int
main (int argc, const char * const *argv)
{
  svn_error_t *err = NULL;
  apr_pool_t *pool;
  svn_auth_baton_t *auth_baton;
  svn_auth_iterstate_t *state;
  svn_auth_cred_simple_t *creds;
  const char *wc_dir;
  const svn_auth_provider_t *wc_provider, *prompt_provider;
  void *wc_prov_baton, *prompt_prov_baton;
  svn_auth_prompt_t prompt_func;

  apr_initialize ();
  pool = svn_pool_create (NULL);

  /* Create the auth_baton */
  err = svn_auth_open (&auth_baton, pool);
  if (err)
    svn_handle_error (err, stderr, TRUE);

  /* Get the two providers from libsvn_auth */
  wc_dir = "/home/sussman/projects/svn"; /* ### CHANGE ME TO EXPERIMENT */  
  svn_wc_get_simple_wc_provider (&wc_provider, &wc_prov_baton,
                                 wc_dir, NULL, pool);

  svn_auth_get_simple_prompt_provider (&prompt_provider, &prompt_prov_baton,
                                       &prompt_user, NULL, 2,
                                       "schmooey", "zoink", /* defaults */
                                       pool);
  
  /* Register the providers */
  err = svn_auth_register_provider (auth_baton, 0 /* ignored */,
                                    wc_provider, wc_prov_baton, pool);
  if (err)
    svn_handle_error (err, stderr, TRUE);

  err = svn_auth_register_provider (auth_baton, 0 /* ignored */,
                                    prompt_provider, prompt_prov_baton, pool);
  if (err)
    svn_handle_error (err, stderr, TRUE);

  
  /* Query the auth baton for "simple" creds. */
  err = svn_auth_first_credentials ((void **) &creds,
                                    &state, SVN_AUTH_CRED_SIMPLE,
                                    auth_baton, pool);
  if (err)
    svn_handle_error (err, stderr, TRUE);
  
  printf ("### First creds back are %s, %s.\n",
          creds->username, creds->password); 

  /* Keep querying until there are no more creds left. */
  while (creds != NULL)
    {
      err = svn_auth_next_credentials ((void **) &creds, state, pool);
      if (err)
        svn_handle_error (err, stderr, TRUE);
      
      if (creds)
        printf ("### Next creds back are %s, %s.\n",
                creds->username, creds->password); 
    }

  return 0;
}
