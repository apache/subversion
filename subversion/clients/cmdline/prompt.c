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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_auth.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "cl.h"




/*** Our implementation of the 'prompt callback' routine, as defined
     in svn_auth.h.  (See svn_client_prompt_t.)  */

svn_error_t *
svn_cl__prompt_user (const char **result,
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

  if (! hide)
    {
      svn_boolean_t saw_first_half_of_eol = FALSE;
      fprintf (stderr, "%s", prompt_native);
      fflush (stderr);

      while (1)
        {
          status = apr_file_getc (&c, fp);
          if (status && ! APR_STATUS_IS_EOF(status))
            return svn_error_create (status, NULL, "error reading stdin.");

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

      status = apr_password_get (prompt_native, strbuf->data, &bufsize);
      if (status)
        return svn_error_create (status, NULL,
                                 "error from apr_password_get().");      
    }

  SVN_ERR (svn_utf_cstring_to_utf8 ((const char **)result, strbuf->data,
                                    NULL, pool));

  return SVN_NO_ERROR;
}
