/*
 * ra-commit.c :  basic commit program for the RA/DAV library
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
#include <apr_hash.h>

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
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  const char *url_type;
  const svn_ra_plugin_t *plugin;
  svn_string_t *root_dir;

  apr_initialize ();
  pool = svn_pool_create (NULL);

  if (argc != 2)
    {
      fprintf (stderr, "usage: %s REPOSITORY_URL\n", argv[0]);
      return 1;
    }

  /* ### this is temporary. the URL should come from the WC library. */
  url = svn_string_create(argv[1], pool);

  err = svn_ra_dav_init(0, pool, &url_type, &plugin);
  if (err)
    goto error;

  err = (*plugin->open)(&session_baton, url, pool);
  if (err)
    goto error;

  /* ### this whole thing needs to be updated for the close_commit stuff
     ### and tossing svn_wc_close_commit */

  err = (*plugin->get_commit_editor)(session_baton, &editor,
                                     &edit_baton,
                                     svn_string_create ("dummy log msg", pool),
                                     NULL, NULL, NULL);
  if (err)
    goto error;

  /* ### god damn svn_string_t */
  root_dir = svn_string_create(".", pool);

  printf("Beginning crawl...\n");
  err = svn_wc_crawl_local_mods(root_dir, editor, edit_baton, pool);
  if (err)
    goto error;

  printf("Committing new version to working copy...\n");

  printf("Completed. Wrapping up...\n");
  (*plugin->close)(session_baton);

  apr_pool_destroy(pool);
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
