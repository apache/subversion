/*
 * util.c :  utility functions for the RA/DAV library
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <uri.h>

#include "svn_string.h"

#include "ra_dav.h"



void svn_ra_dav__copy_href(svn_string_t *dst, const char *src)
{
  struct uri parsed_url;

  /* parse the PATH element out of the URL and store it.

     ### do we want to verify the rest matches the current session?

     Note: mod_dav does not (currently) use an absolute URL, but simply a
     server-relative path (i.e. this uri_parse is effectively a no-op).
  */
  (void) uri_parse(src, &parsed_url, NULL);
  svn_string_set(dst, parsed_url.path);
  uri_free(&parsed_url);
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
