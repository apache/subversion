/*
 * prompt.c -- ask the user for authentication information.
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

/* ==================================================================== */



/*** Includes. ***/

#include <apr_lib.h>

#include "svn_cmdline.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_auth.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "cl.h"




/* Set @a *result to the result of prompting the user with @a
 * prompt_msg.  Allocate @a *result in @a pool.
 *
 * If @a hide is true, then try to avoid displaying the user's input.
 */
static svn_error_t *
prompt (const char **result,
        const char *prompt_msg,
        svn_boolean_t hide,
        apr_pool_t *pool)
{
  apr_status_t status;
  apr_file_t *fp;
  char c;
  const char *prompt_stdout;

  svn_stringbuf_t *strbuf = svn_stringbuf_create ("", pool);

  status = apr_file_open_stdin (&fp, pool);
  if (status)
    return svn_error_create (status, NULL, "couldn't open stdin");

  SVN_ERR (svn_cmdline_cstring_from_utf8 (&prompt_stdout, prompt_msg, pool));

  if (! hide)
    {
      svn_boolean_t saw_first_half_of_eol = FALSE;
      fprintf (stderr, "%s", prompt_stdout);
      fflush (stderr);

      while (1)
        {
          status = apr_file_getc (&c, fp);
          if (status && ! APR_STATUS_IS_EOF(status))
            return svn_error_create (status, NULL, "error reading stdin");

          if (saw_first_half_of_eol)
            {
              if (c == APR_EOL_STR[1])
                break;
              else
                saw_first_half_of_eol = FALSE;
            }
          else if (c == APR_EOL_STR[0])
            {
              if (sizeof(APR_EOL_STR) == 3)
                {
                  saw_first_half_of_eol = TRUE;
                  continue;
                }
              else if (sizeof(APR_EOL_STR) == 2)
                break;
              else
                /* ### APR_EOL_STR holds more than two chars?  Who
                   ever heard of such a thing? */
                abort ();
            }
          
          svn_stringbuf_appendbytes (strbuf, &c, 1);
        }
    }
  else
    {
      size_t bufsize = 300;
      svn_stringbuf_ensure (strbuf, bufsize);

      status = apr_password_get (prompt_stdout, strbuf->data, &bufsize);
      if (status)
        return svn_error_create (status, NULL, "error from apr_password_get");
    }

  SVN_ERR (svn_cmdline_cstring_to_utf8 (result, strbuf->data, pool));

  return SVN_NO_ERROR;
}



/** Prompt functions for auth providers. **/

/* Helper function for auth provider prompters: mention the
 * authentication @a realm on stderr, in a manner appropriate for
 * preceding a prompt; or if @a realm is null, then do nothing.
 */
static svn_error_t *
maybe_print_realm (const char *realm, apr_pool_t *pool)
{
  const char *realm_native;

  if (realm)
    {
      SVN_ERR (svn_utf_cstring_from_utf8 (&realm_native, realm, pool));
      fprintf (stderr, "Authentication realm: %s\n", realm_native);
      fflush (stderr);
    }

  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_simple_prompt_func_t'. */
svn_error_t *
svn_cl__auth_simple_prompt (svn_auth_cred_simple_t **cred_p,
                            void *baton,
                            const char *realm,
                            const char *username,
                            apr_pool_t *pool)
{
  svn_auth_cred_simple_t *ret = apr_pcalloc (pool, sizeof (*ret));
  const char *pass_prompt;

  SVN_ERR (maybe_print_realm (realm, pool));

  if (username)
    ret->username = apr_pstrdup (pool, username);
  else
    SVN_ERR (prompt (&(ret->username), "Username: ", FALSE, pool));

  pass_prompt = apr_psprintf (pool, "Password for '%s': ", ret->username);
  SVN_ERR (prompt (&(ret->password), pass_prompt, TRUE, pool));

  *cred_p = ret;
  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_username_prompt_func_t'. */
svn_error_t *
svn_cl__auth_username_prompt (svn_auth_cred_username_t **cred_p,
                              void *baton,
                              const char *realm,
                              apr_pool_t *pool)
{
  svn_auth_cred_username_t *ret = apr_pcalloc (pool, sizeof (*ret));

  SVN_ERR (maybe_print_realm (realm, pool));

  SVN_ERR (prompt (&(ret->username), "Username: ", FALSE, pool));
  *cred_p = ret;
  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_ssl_server_prompt_func_t'. */
svn_error_t *
svn_cl__auth_ssl_server_prompt (
  svn_auth_cred_server_ssl_t **cred_p,
  void *baton,
  int failures,
  const svn_auth_ssl_server_cert_info_t *cert_info,
  apr_pool_t *pool)
{
  int allow_perm_accept = failures & SVN_AUTH_SSL_UNKNOWNCA;
  const char *choice;
  svn_stringbuf_t *buf = svn_stringbuf_create
    ("Error validating server certificate:\n", pool);

  if (failures & SVN_AUTH_SSL_UNKNOWNCA)
    {
      svn_stringbuf_appendcstr (buf, " - Unknown certificate issuer\n");
      svn_stringbuf_appendcstr (buf, "   Fingerprint: ");
      svn_stringbuf_appendcstr (buf, cert_info->fingerprint);
      svn_stringbuf_appendcstr (buf, "\n");
      svn_stringbuf_appendcstr (buf, "   Distinguished name: ");
      svn_stringbuf_appendcstr (buf, cert_info->issuer_dname);
      svn_stringbuf_appendcstr (buf, "\n");
    }

  if (failures & SVN_AUTH_SSL_CNMISMATCH)
    {
      svn_stringbuf_appendcstr (buf, " - Hostname mismatch (");
      svn_stringbuf_appendcstr (buf, cert_info->hostname);
      svn_stringbuf_appendcstr (buf, ")\n");
    } 

  if (failures & SVN_AUTH_SSL_NOTYETVALID)
    {
      svn_stringbuf_appendcstr (buf, " - Certificate is not yet valid\n");
      svn_stringbuf_appendcstr (buf, "   Valid from ");
      svn_stringbuf_appendcstr (buf, cert_info->valid_from);
      svn_stringbuf_appendcstr (buf, "\n");
    }

  if (failures & SVN_AUTH_SSL_EXPIRED)
    {
      svn_stringbuf_appendcstr (buf, " - Certificate has expired\n");
      svn_stringbuf_appendcstr (buf, "   Valid until ");
      svn_stringbuf_appendcstr (buf, cert_info->valid_until);
      svn_stringbuf_appendcstr (buf, "\n");
    }

  if (allow_perm_accept)
    {
      svn_stringbuf_appendcstr (buf,
                                "(R)eject, accept (t)emporarily or accept "
                                "(p)ermanently? ");
    }
  else
    {
      svn_stringbuf_appendcstr (buf, "(R)eject or accept (t)emporarily? ");
    }
  SVN_ERR (prompt (&choice, buf->data, FALSE, pool));

  if (choice && (choice[0] == 't' || choice[0] == 'T'))
    {
      *cred_p = apr_pcalloc (pool, sizeof (**cred_p));
      (*cred_p)->trust_permanently = FALSE;
    }
  else if (allow_perm_accept &&
           choice && (choice[0] == 'p' || choice[0] == 'P'))
    {
      *cred_p = apr_pcalloc (pool, sizeof (**cred_p));
      (*cred_p)->trust_permanently = TRUE;
    }
  else
    {
      *cred_p = NULL;
    }

  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_ssl_client_prompt_func_t'. */
svn_error_t *
svn_cl__auth_ssl_client_prompt (svn_auth_cred_client_ssl_t **cred_p,
                                void *baton,
                                apr_pool_t *pool)
{
  const char *cert_file = NULL;
  svn_auth_cred_client_ssl_t *cred;

  SVN_ERR (prompt (&cert_file, "client certificate filename: ", FALSE, pool));

  cred = apr_palloc (pool, sizeof(*cred));
  cred->cert_file = cert_file;
  *cred_p = cred;

  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_ssl_pw_prompt_func_t'. */
svn_error_t *
svn_cl__auth_ssl_pw_prompt (svn_auth_cred_client_ssl_pass_t **cred_p,
                            void *baton,
                            apr_pool_t *pool)
{
  
  const char *result;

  SVN_ERR (prompt (&result, "client certificate passphrase: ", TRUE, pool));

  if (result && result[0])
    {
      svn_auth_cred_client_ssl_pass_t *ret = apr_pcalloc (pool, sizeof (*ret));
      ret->password = result;
      *cred_p = ret;
    }
  else
    {
      *cred_p = NULL;
    }

  return SVN_NO_ERROR;
}



/** Generic prompting. **/

svn_error_t *
svn_cl__prompt_user (const char **result,
                     const char *prompt_str,
                     apr_pool_t *pool)
{
  return prompt (result, prompt_str, FALSE /* don't hide input */, pool);
}
