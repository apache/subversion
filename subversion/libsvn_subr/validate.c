/*
 * validate.c:  validation routines
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_props.h"



/*** Code. ***/

svn_error_t *
svn_mime_type_validate (const char *mime_type, apr_pool_t *pool)
{
  if (strchr (mime_type, '/') == NULL)
    return svn_error_createf
      (SVN_ERR_BAD_MIME_TYPE, 0, NULL,
       "Mime type \"%s\" missing '/'\n", mime_type);

  {
    /* Could just take an optional `len' arg and avoid this strlen.
       Many callers have the length anyway.  But is such a
       micro-optimization worth the extra interface noise?  Is it
       even worth this comment?? */
    char c = mime_type[strlen(mime_type) - 1];
    
    /* Man page seems to claim that isalnum is always a function.
       But I don't trust it. */
    if (! apr_isalnum (c))
      return svn_error_createf
        (SVN_ERR_BAD_MIME_TYPE, 0, NULL,
         "Mime type \"%s\" ends with non-alphanumeric.\n", mime_type);
  }

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_mime_type_is_binary (const char *mime_type)
{
  return ((strncmp (mime_type, "text/", 5) != 0)
          && (strcmp (mime_type, "image/x-xbitmap") != 0)
          && (strcmp (mime_type, "image/x-xpixmap") != 0)
          );
}
