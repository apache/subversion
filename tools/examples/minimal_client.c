/*
 * minimal_client.c  - a minimal Subversion client application ("hello world")
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
 *
 *  This app demonstrates how to use the svn_client.h API.
 *
 *  It reads a directory URL from the commandline, runs
 *  svn_client_list() and prints the list of directory-entries.  It
 *  also knows how to deal with basic username/password authentication
 *  challenges.
 *
 *  For a much more complex example, the svn cmdline client might be
 *  considered the 'reference implementation'.
 */


#include "svn_client.h"
#include "svn_pools.h"
#include "svn_config.h"


/* A tiny callback function of type 'svn_client_prompt_t'.  For a much
   better example, see svn_cl__prompt_user() in the official svn
   cmdline client. */
svn_error_t *
my_prompt_callback (const char **info,
                    const char *prompt,
                    svn_boolean_t hide,
                    void *baton,
                    apr_pool_t *pool)
{
  char *answer;
  char answerbuf[100];
  int len;

  printf ("%s: ", prompt);
  answer = fgets (answerbuf, 100, stdin);

  len = strlen(answer);
  if (answer[len-1] == '\n')
    answer[len-1] = '\0';

  *info = apr_pstrdup (pool, answer);
  return SVN_NO_ERROR;
}



int
main (int argc, const char **argv)
{
  apr_pool_t *pool;
  svn_error_t *err;
  svn_opt_revision_t revision;
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  svn_client_ctx_t ctx = { 0 };
  const char *URL;

  if (argc <= 1)
    {
      printf ("Usage:  %s URL\n", argv[0]);  
      return EXIT_FAILURE;
    }
  else
    URL = argv[1];

  /* Initialize the app.  Send all error messages to 'stderr'.  */
  if (svn_cmdline_init ("minimal_client", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create top-level memory pool. Be sure to read the HACKING file to
     understand how to properly use/free subpools. */
  pool = svn_pool_create (NULL);

  /* Make sure the ~/.subversion run-time config files exist */  
  err = svn_config_ensure (pool);
  if (err)
    {
      /* For functions deeper in the stack, we usually use the
         SVN_ERR() exception-throwing macro (see svn_error.h).  At the
         top level, we catch & print the error with svn_handle_error(). */
      svn_handle_error (err, stderr, 0);
      return EXIT_FAILURE;
    }

  /* All clients need to fill out a client_ctx object. */
  {
    /* A function (& context) which can prompt the user for information. */
    ctx.prompt_func = my_prompt_callback; 
    ctx.prompt_baton = NULL;
    
    /* Load the run-time config file into a hash */
    if ((err = svn_config_get_config (&(ctx.config), pool)))
      {
        svn_handle_error (err, stderr, 0);
        return EXIT_FAILURE;
      }

    /* Depending on what your client does, you'll want to read about
       (and implement) the various callback function types below.  */

    /* A func (& context) which receives event signals during
       checkouts, updates, commits, etc.  */
    /* ctx.notify_func = my_notification_func;
       ctx.notify_baton = NULL; */
    
    /* A func (& context) which can receive log messages */
    /* ctx.log_msg_func = my_log_msg_receiver_func;
       ctx.log_msg_baton = NULL; */
    
    /* A func (& context) which checks whether the user cancelled */
    /* ctx.cancel_func = my_cancel_checking_func;
       ctx.cancel_baton = NULL; */

    /* Make the client_ctx capable of authenticating users */
    {
      /* There are many different kinds of authentication back-end
         "providers".  See svn_auth.h for a full overview. */
      svn_auth_provider_object_t *provider;
      apr_array_header_t *providers
        = apr_array_make (pool, 4, sizeof (svn_auth_provider_object_t *));

      svn_client_get_simple_prompt_provider (&provider,
                                             my_prompt_callback, NULL, 
                                             2, /* retry limit */ pool);
      APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;

      svn_client_get_username_prompt_provider (&provider,
                                               my_prompt_callback, NULL, 
                                               2, /* retry limit */ pool);
      APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;

      /* Register the auth-providers into the context's auth_baton. */
      svn_auth_open (&ctx.auth_baton, providers, pool);      
    }
  } /* end of client_ctx setup */


  /* Now do the real work. */
  
  /* Set revision to always be the HEAD revision.  It could, however,
     be set to a specific revision number, date, or other values. */
  revision.kind = svn_opt_revision_head;

  /* Main call into libsvn_client does all the work. */
  err = svn_client_ls (&dirents,
                       URL, &revision,
                       FALSE, /* no recursion */
                       &ctx, pool);
  if (err)
    {
      svn_handle_error (err, stderr, 0);
      return EXIT_FAILURE;
    }

  /* Print the dir entries in the hash. */
  for (hi = apr_hash_first (pool, dirents); hi; hi = apr_hash_next (hi))
    {
      const char *entryname;
      svn_dirent_t *val;

      apr_hash_this (hi, (void *) &entryname, NULL, (void *) &val);      
      printf ("   %s\n", entryname);

      /* 'val' is actually an svn_dirent_t structure; a more complex
          program would mine it for extra printable information. */
    }

  return EXIT_SUCCESS;
}
