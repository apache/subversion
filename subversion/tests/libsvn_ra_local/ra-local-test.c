/*
 * ra-dav-local.c :  basic test program for the RA LOCAL library
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <apr_general.h>
#include <apr_pools.h>

#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_wc.h"



int
main (int argc, char **argv)
{
  apr_pool_t *pool;
  svn_error_t *err;
  void *session_baton;
  svn_string_t *url; 
  const char *url_type;
  const svn_ra_plugin_t *plugin;

  apr_initialize ();
  pool = svn_pool_create (NULL);

  if (argc != 2)
    {
      fprintf (stderr, "usage: %s FILE_URL\n", argv[0]);
      return 1;
    }

  url = svn_string_create (argv[1], pool);

  err = svn_ra_local_init(0, pool, &url_type, &plugin);
  if (err)
    goto error;

  err = (*plugin->open)(&session_baton, url, pool);
  if (err)
    goto error;

  (*plugin->close)(session_baton);

  apr_pool_destroy(pool);
  apr_terminate();

  return SVN_NO_ERROR;

 error:
  svn_handle_error (err, stdout, 0);
  return 1;
}





/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
