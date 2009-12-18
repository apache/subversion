/*
 * validate.c:  validation routines
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
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_error.h"
#include "svn_private_config.h"



/*** Code. ***/

svn_error_t *
svn_mime_type_validate(const char *mime_type, apr_pool_t *pool)
{
  /* Since svn:mime-type can actually contain a full content type
     specification, e.g., "text/html; charset=UTF-8", make sure we're
     only looking at the media type here. */
  const apr_size_t len = strcspn(mime_type, "; ");
  const char *const slash_pos = strchr(mime_type, '/');
  apr_size_t i;
  const char *tspecials = "()<>@,;:\\\"/[]?=";

  if (len == 0)
    return svn_error_createf
      (SVN_ERR_BAD_MIME_TYPE, NULL,
       _("MIME type '%s' has empty media type"), mime_type);

  if (slash_pos == NULL || slash_pos >= &mime_type[len])
    return svn_error_createf
      (SVN_ERR_BAD_MIME_TYPE, NULL,
       _("MIME type '%s' does not contain '/'"), mime_type);

  /* Check the mime type for illegal characters. See RFC 1521. */
  for (i = 0; i < len; i++)
    {
      if (&mime_type[i] != slash_pos
        && (! apr_isascii(mime_type[i])
            || apr_iscntrl(mime_type[i])
            || apr_isspace(mime_type[i])
            || (strchr(tspecials, mime_type[i]) != NULL)))
        return svn_error_createf
          (SVN_ERR_BAD_MIME_TYPE, NULL,
           _("MIME type '%s' contains invalid character '%c'"),
           mime_type, mime_type[i]);
    }

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_mime_type_is_binary(const char *mime_type)
{
  /* See comment in svn_mime_type_validate() above. */
  const apr_size_t len = strcspn(mime_type, "; ");
  return ((strncmp(mime_type, "text/", 5) != 0)
          && (len != 15 || strncmp(mime_type, "image/x-xbitmap", len) != 0)
          && (len != 15 || strncmp(mime_type, "image/x-xpixmap", len) != 0)
          );
}
