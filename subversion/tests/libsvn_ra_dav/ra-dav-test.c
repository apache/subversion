/*
 * ra-dav-test.c :  basic test program for the RA/DAV library
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

/* declare explicitly when we call directly (rather than via DSO load) */
svn_error_t *svn_ra_dav_init(int abi_version,
                             apr_pool_t *pconf,
                             const svn_ra_plugin_t **plugin);


int
main (int argc, char **argv)
{
  apr_pool_t *pool;
  svn_error_t *err;
  void *session_baton;
  svn_string_t *url;
  const char *dir;
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  svn_string_t *repos;
  svn_string_t *anc_path;
  svn_string_t *root_path;
  svn_revnum_t revision;
  const svn_ra_plugin_t *plugin;

  apr_initialize ();
  pool = svn_pool_create (NULL);

  if (argc != 3)
    {
      fprintf (stderr, "usage: %s REPOSITORY_URL TARGET_DIR\n", argv[0]);
      return 1;
    }

  url = svn_string_create(argv[1], pool);
  dir = argv[2];        /* ### default to the last component of the URL */

  err = svn_ra_dav_init(0, pool, &plugin);
  if (err)
    {
      svn_handle_error (err, stdout, 0);
      return 1;
    }

  err = (*plugin->svn_ra_open)(&session_baton, url, pool);
  if (err)
    {
      svn_handle_error (err, stdout, 0);
      return 1;
    }

  /* ### hmm... */
  repos = url;

  /* ### what the heck does "ancestor path" mean for a checkout? */
  anc_path = svn_string_create("", pool);

  /* ### how can we know this before we start fetching crap? */
  revision = 1;

  err = svn_wc_get_checkout_editor(svn_string_create(dir, pool),
                                   repos, anc_path, revision,
                                   &editor, &edit_baton, pool);
  if (err)
    goto error;

  /* ### what is this path? */
  root_path = svn_string_create("", pool);
  err = (*plugin->svn_ra_do_checkout)(session_baton, editor, edit_baton,
                                      root_path);
  if (err)
    goto error;

  err = (*editor->close_edit)(edit_baton);
  if (err)
    goto error;

  (*plugin->svn_ra_close)(session_baton);

  apr_destroy_pool(pool);
  apr_terminate();

  return 0;

 error:
  svn_handle_error (err, stdout, 0);
  return 1;
}





/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
