/* svn-push.c --- propagate changesets from one (networked) repository to 
 * a different (networked) repository.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include <stdio.h>

#include "svn_pools.h"
#include "svn_path.h"
#include "svn_ra.h"
#include "svn_delta.h"
#include "svn_config.h"
#include "svn_cmdline.h"

/* Implements svn_commit_callback2_t. */
static svn_error_t *
my_commit_callback (const svn_commit_info_t *ci, void *baton, apr_pool_t *pool)
{
  printf ("Commiting Rev. %" SVN_REVNUM_T_FMT " at date \"%s\", by "
          "author \"%s\"\n", ci->revision, ci->date, ci->author);
  if (ci->post_commit_err)
    printf ("Post-commit Error: %s\n", ci->post_commit_err);
  return SVN_NO_ERROR;
}


/* Implements svn_ra_callbacks2_t.open_tmp_file */
static svn_error_t *
open_tmp_file (apr_file_t **fp, void *callback_baton, apr_pool_t *pool)
{
  const char *path;

  SVN_ERR (svn_io_temp_dir (&path, pool));
  path = svn_path_join (path, "tempfile", pool);
  SVN_ERR (svn_io_open_unique_file (fp, NULL, path, ".tmp", TRUE, pool));

  return SVN_NO_ERROR;
}
  

svn_error_t *(*old_change_file_prop) (void *file_baton,
                                      const char *name,
                                      const svn_string_t *value,
                                      apr_pool_t *pool);

/* Implements svn_ra_callbacks2_t.change_file_prop */
static svn_error_t *
new_change_file_prop (void *file_baton, const char *name,
                      const svn_string_t *value, apr_pool_t *pool)
{
  if (svn_property_kind (NULL, name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;
  else
    return old_change_file_prop (file_baton, name, value, pool);
}


svn_error_t *(*old_change_dir_prop) (void *dir_baton,
                                     const char *name,
                                     const svn_string_t *value,
                                     apr_pool_t *pool);

/* Implements svn_ra_callbacks2_t.change_dir_prop */
static svn_error_t *
new_change_dir_prop (void *dir_baton, const char *name,
                     const svn_string_t *value, apr_pool_t *pool)
{
  if (svn_property_kind (NULL, name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;
  else
    return old_change_dir_prop (dir_baton, name, value, pool);
}


static svn_error_t *
do_job (const char *src_url, const char *dest_url, int start_rev, int end_rev,
        apr_pool_t *pool)
{
  apr_hash_t *config;
  svn_config_t *cfg;
  svn_auth_baton_t *ab;
  svn_ra_callbacks2_t *callbacks;
  svn_ra_session_t *ra_src, *ra_dest;
  const svn_delta_editor_t *editor;
  svn_delta_editor_t *my_editor;
  void *edit_baton;
  const svn_ra_reporter2_t *reporter;
  void *report_baton;

  SVN_ERR (svn_config_get_config (&config, NULL, pool));
  cfg = apr_hash_get (config, SVN_CONFIG_CATEGORY_CONFIG, APR_HASH_KEY_STRING);

  SVN_ERR (svn_cmdline_setup_auth_baton (&ab, FALSE, NULL, NULL, NULL, FALSE,
                                         cfg, NULL, NULL, pool));
  SVN_ERR (svn_ra_create_callbacks (&callbacks, pool));
  callbacks->open_tmp_file = open_tmp_file;
  callbacks->auth_baton = ab;

  SVN_ERR (svn_ra_open2 (&ra_dest, dest_url, callbacks, NULL, config, pool));
  SVN_ERR (svn_ra_open2 (&ra_src, src_url, callbacks, NULL, config, pool));

  SVN_ERR (svn_ra_get_commit_editor2 (ra_dest, &editor, &edit_baton,
                                      "Hello World!", my_commit_callback,
                                      NULL, NULL, TRUE, pool));

  /* Create a copy of the editor so we can hook some calls. */
  my_editor = apr_palloc (pool, sizeof (*my_editor));
  *my_editor = *editor;

  /* Install the editor hooks. */
  old_change_file_prop = editor->change_file_prop;
  my_editor->change_file_prop = new_change_file_prop;
  old_change_dir_prop = editor->change_dir_prop;
  my_editor->change_dir_prop = new_change_dir_prop;

  SVN_ERR (svn_ra_do_diff (ra_src, &reporter, &report_baton,
                           end_rev, "", TRUE, TRUE, src_url,
                           my_editor, edit_baton, pool));
  SVN_ERR (reporter->set_path (report_baton, "", start_rev, 0, NULL, pool));
  SVN_ERR (reporter->finish_report (report_baton, pool));

  return SVN_NO_ERROR;
}


/* Version compatibility check */
static svn_error_t *
check_lib_versions (void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",   svn_subr_version },
      { "svn_delta",  svn_delta_version },
      { "svn_ra",     svn_ra_version },
      { NULL, NULL }
    };

  SVN_VERSION_DEFINE (my_version);
  return svn_ver_check_list (&my_version, checklist);
}


int
main (int argc, char *argv[])
{
  apr_pool_t *pool;
  svn_error_t *error = NULL;
  int start_rev, end_rev;
  char *src_url, *dest_url;

  if (svn_cmdline_init ("svn-push", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  if (argc != 5 ||
      strcmp (argv[1], "-r") != 0 ||
      sscanf (argv[2], "%i:%i", &start_rev, &end_rev) != 2)
    {
      fprintf(stderr, "%s", "Usage : svn-push -r N:M SRC_URL DEST_URL\n");
      return EXIT_FAILURE;
    }

  src_url = argv[3];
  dest_url = argv[4];

  pool = svn_pool_create (NULL);

  /* Check library versions */
  error = check_lib_versions ();
  if (error) goto error;

  error = do_job (src_url, dest_url, start_rev, end_rev, pool);
  if (error) goto error;

  svn_pool_destroy (pool);
  return EXIT_SUCCESS;

error:
  svn_handle_error2 (error, stderr, FALSE, "svn-push: ");
  return EXIT_FAILURE;
}
