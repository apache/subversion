/*
 * prompt.c -- ask the user for authentication information.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
#include <apr_poll.h>

#include "svn_cmdline.h"
#include "svn_string.h"
#include "svn_auth.h"
#include "svn_error.h"

#include "svn_private_config.h"



/* Wait for input on @a *f.  Doing all allocations
 * in @a pool.  This functions is based on apr_wait_for_io_or_timeout().
 * Note that this will return an EINTR on a signal.
 *
 * ### FIX: When APR gives us a better way of doing this use it. */
static apr_status_t wait_for_input(apr_file_t *f,
                                   apr_pool_t *pool)
{
  apr_pollfd_t pollset;
  int srv, n;

/* APR specs say things that are unimplemented are supposed to return
 * APR_ENOTIMPL.  But when trying to use APR_POLL_FILE with apr_poll
 * on Windows it returns APR_EBADF instead.  So just return APR_ENOTIMPL
 * ourselves here.
 */
#ifdef WIN32
  return APR_ENOTIMPL;
#endif /* WIN32 */
  
  pollset.desc_type = APR_POLL_FILE;
  pollset.desc.f = f;
  pollset.p = pool;
  pollset.reqevents = APR_POLLIN;

#ifndef AS400
  srv = apr_poll(&pollset, 1, &n, -1);
#else
  /* OS400 requires a pool argument for apr_poll(). */
  srv = apr_poll(&pollset, 1, &n, -1, pool);
#endif

  if (n == 1 && pollset.rtnevents & APR_POLLIN)
    return APR_SUCCESS;

  return srv;
}

/* Set @a *result to the result of prompting the user with @a
 * prompt_msg.  Use @ *pb to get the cancel_func and cancel_baton.
 * Do not call the cancel_func if @a *pb is NULL.
 * Allocate @a *result in @a pool.
 *
 * If @a hide is true, then try to avoid displaying the user's input.
 */
static svn_error_t *
prompt(const char **result,
       const char *prompt_msg,
       svn_boolean_t hide,
       svn_cmdline_prompt_baton_t *pb,
       apr_pool_t *pool)
{
  apr_status_t status;
  apr_file_t *fp;
  char c;

  svn_stringbuf_t *strbuf = svn_stringbuf_create("", pool);

  status = apr_file_open_stdin(&fp, pool);
  if (status)
    return svn_error_wrap_apr(status, _("Can't open stdin"));

  if (! hide)
    {
      svn_boolean_t saw_first_half_of_eol = FALSE;
      SVN_ERR(svn_cmdline_fputs(prompt_msg, stderr, pool));
      fflush(stderr);

      while (1)
        {
          /* Hack to allow us to not block for io on the prompt, so
           * we can cancel. */
          if (pb)
            SVN_ERR(pb->cancel_func(pb->cancel_baton));
          status = wait_for_input(fp, pool);
          if (APR_STATUS_IS_EINTR(status))
            continue;
          else if (status && status != APR_ENOTIMPL)
            return svn_error_wrap_apr(status, _("Can't read stdin"));
             
          status = apr_file_getc(&c, fp);
          if (status)
            return svn_error_wrap_apr(status, _("Can't read stdin"));

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
                abort();
            }
          
          svn_stringbuf_appendbytes(strbuf, &c, 1);
        }
    }
  else
    {
      const char *prompt_stdout;
      size_t bufsize = 300;
      SVN_ERR(svn_cmdline_cstring_from_utf8(&prompt_stdout, prompt_msg,
                                            pool));
      svn_stringbuf_ensure(strbuf, bufsize);

      status = apr_password_get(prompt_stdout, strbuf->data, &bufsize);
      if (status)
        return svn_error_wrap_apr(status, _("Can't get password"));
    }

  SVN_ERR(svn_cmdline_cstring_to_utf8(result, strbuf->data, pool));

  return SVN_NO_ERROR;
}



/** Prompt functions for auth providers. **/

/* Helper function for auth provider prompters: mention the
 * authentication @a realm on stderr, in a manner appropriate for
 * preceding a prompt; or if @a realm is null, then do nothing.
 */
static svn_error_t *
maybe_print_realm(const char *realm, apr_pool_t *pool)
{
  if (realm)
    {
      SVN_ERR(svn_cmdline_fprintf(stderr, pool,
                                  _("Authentication realm: %s\n"), realm));
      fflush(stderr);
    }

  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_simple_prompt_func_t'. */
svn_error_t *
svn_cmdline_auth_simple_prompt(svn_auth_cred_simple_t **cred_p,
                               void *baton,
                               const char *realm,
                               const char *username,
                               svn_boolean_t may_save,
                               apr_pool_t *pool)
{
  svn_auth_cred_simple_t *ret = apr_pcalloc(pool, sizeof(*ret));
  const char *pass_prompt;
  svn_cmdline_prompt_baton_t *pb = baton;

  SVN_ERR(maybe_print_realm(realm, pool));

  if (username)
    ret->username = apr_pstrdup(pool, username);
  else
    SVN_ERR(prompt(&(ret->username), _("Username: "), FALSE, pb, pool));

  pass_prompt = apr_psprintf(pool, _("Password for '%s': "), ret->username);
  SVN_ERR(prompt(&(ret->password), pass_prompt, TRUE, pb, pool));
  ret->may_save = may_save;
  *cred_p = ret;
  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_username_prompt_func_t'. */
svn_error_t *
svn_cmdline_auth_username_prompt(svn_auth_cred_username_t **cred_p,
                                 void *baton,
                                 const char *realm,
                                 svn_boolean_t may_save,
                                 apr_pool_t *pool)
{
  svn_auth_cred_username_t *ret = apr_pcalloc(pool, sizeof(*ret));
  svn_cmdline_prompt_baton_t *pb = baton;

  SVN_ERR(maybe_print_realm(realm, pool));

  SVN_ERR(prompt(&(ret->username), _("Username: "), FALSE, pb, pool));
  ret->may_save = may_save;
  *cred_p = ret;
  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_ssl_server_trust_prompt_func_t'. */
svn_error_t *
svn_cmdline_auth_ssl_server_trust_prompt
  (svn_auth_cred_ssl_server_trust_t **cred_p,
   void *baton,
   const char *realm,
   apr_uint32_t failures,
   const svn_auth_ssl_server_cert_info_t *cert_info,
   svn_boolean_t may_save,
   apr_pool_t *pool)
{
  const char *choice;
  svn_stringbuf_t *msg;
  svn_cmdline_prompt_baton_t *pb = baton;
  svn_stringbuf_t *buf = svn_stringbuf_createf
    (pool, _("Error validating server certificate for '%s':\n"), realm);

  if (failures & SVN_AUTH_SSL_UNKNOWNCA)
    {
      svn_stringbuf_appendcstr
        (buf,
         _(" - The certificate is not issued by a trusted authority. Use the\n"
           "   fingerprint to validate the certificate manually!\n"));
    }

  if (failures & SVN_AUTH_SSL_CNMISMATCH)
    {
      svn_stringbuf_appendcstr
        (buf, _(" - The certificate hostname does not match.\n"));
    } 

  if (failures & SVN_AUTH_SSL_NOTYETVALID)
    {
      svn_stringbuf_appendcstr
        (buf, _(" - The certificate is not yet valid.\n"));
    }

  if (failures & SVN_AUTH_SSL_EXPIRED)
    {
      svn_stringbuf_appendcstr
        (buf, _(" - The certificate has expired.\n"));
    }

  if (failures & SVN_AUTH_SSL_OTHER)
    {
      svn_stringbuf_appendcstr
        (buf, _(" - The certificate has an unknown error.\n"));
    }

  msg = svn_stringbuf_createf
    (pool,
     _("Certificate information:\n"
       " - Hostname: %s\n"
       " - Valid: from %s until %s\n"
       " - Issuer: %s\n"
       " - Fingerprint: %s\n"),
     cert_info->hostname,
     cert_info->valid_from,
     cert_info->valid_until,
     cert_info->issuer_dname,
     cert_info->fingerprint);
  svn_stringbuf_appendstr(buf, msg);

  if (may_save)
    {
      svn_stringbuf_appendcstr
        (buf, _("(R)eject, accept (t)emporarily or accept (p)ermanently? "));
    }
  else
    {
      svn_stringbuf_appendcstr(buf, _("(R)eject or accept (t)emporarily? "));
    }
  SVN_ERR(prompt(&choice, buf->data, FALSE, pb, pool));

  if (choice && (choice[0] == 't' || choice[0] == 'T'))
    {
      *cred_p = apr_pcalloc(pool, sizeof(**cred_p));
      (*cred_p)->may_save = FALSE;
      (*cred_p)->accepted_failures = failures;
    }
  else if (may_save && choice && (choice[0] == 'p' || choice[0] == 'P'))
    {
      *cred_p = apr_pcalloc(pool, sizeof(**cred_p));
      (*cred_p)->may_save = TRUE;
      (*cred_p)->accepted_failures = failures;
    }
  else
    {
      *cred_p = NULL;
    }

  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_ssl_client_cert_prompt_func_t'. */
svn_error_t *
svn_cmdline_auth_ssl_client_cert_prompt
  (svn_auth_cred_ssl_client_cert_t **cred_p,
   void *baton,
   const char *realm,
   svn_boolean_t may_save,
   apr_pool_t *pool)
{
  svn_auth_cred_ssl_client_cert_t *cred = NULL;
  const char *cert_file = NULL;
  svn_cmdline_prompt_baton_t *pb = baton;

  SVN_ERR(maybe_print_realm(realm, pool));
  SVN_ERR(prompt(&cert_file, _("Client certificate filename: "), 
                 FALSE, pb, pool));

  cred = apr_palloc(pool, sizeof(*cred));
  cred->cert_file = cert_file;
  cred->may_save = may_save;
  *cred_p = cred;

  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_ssl_client_cert_pw_prompt_func_t'. */
svn_error_t *
svn_cmdline_auth_ssl_client_cert_pw_prompt
  (svn_auth_cred_ssl_client_cert_pw_t **cred_p,
   void *baton,
   const char *realm,
   svn_boolean_t may_save,
   apr_pool_t *pool)
{
  svn_auth_cred_ssl_client_cert_pw_t *cred = NULL;
  const char *result;
  const char *text = apr_psprintf(pool, _("Passphrase for '%s': "), realm);
  svn_cmdline_prompt_baton_t *pb = baton;

  SVN_ERR(prompt(&result, text, TRUE, pb, pool));

  cred = apr_pcalloc(pool, sizeof(*cred));
  cred->password = result;
  cred->may_save = may_save;
  *cred_p = cred;

  return SVN_NO_ERROR;
}



/** Generic prompting. **/

svn_error_t *
svn_cmdline_prompt_user(const char **result,
                        const char *prompt_str,
                        apr_pool_t *pool)
{
  return prompt(result, prompt_str, FALSE /* don't hide input */, NULL, pool);
}
