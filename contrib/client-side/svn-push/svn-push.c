/* svn-push.c --- propagate changesets from one (networked) repository to 
 * a different (networked) repository.
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
#include <stdio.h>

#include <svn_pools.h>
#include <svn_ra.h>
#include <svn_delta.h>
#include <svn_config.h>

static svn_error_t *
my_commit_callback (svn_revnum_t new_revision,
		    const char *date, const char *author, void *baton)
{
  printf ("Commiting Rev. %i at date \"%s\", by author \"%s\"",
	  new_revision, date, author);

  return SVN_NO_ERROR;
}

svn_error_t *(*old_change_file_prop) (void *file_baton,
				      const char *name,
				      const svn_string_t * value,
				      apr_pool_t * pool);

svn_error_t *(*old_change_dir_prop) (void *dir_baton,
				     const char *name,
				     const svn_string_t * value,
				     apr_pool_t * pool);

svn_error_t *
new_change_file_prop (void *file_baton,
		      const char *name,
		      const svn_string_t * value, apr_pool_t * pool)
{
  if (svn_property_kind (NULL, name) != svn_prop_regular_kind)
    {
      /* Do nothing */
      return SVN_NO_ERROR;
    }
  else
    return old_change_file_prop (file_baton, name, value, pool);
}

svn_error_t *
new_change_dir_prop (void *dir_baton,
		     const char *name,
		     const svn_string_t * value, apr_pool_t * pool)
{
  if (svn_property_kind (NULL, name) != svn_prop_regular_kind)
    {
      return SVN_NO_ERROR;	/* Do nothing */
    }
  else
    return old_change_dir_prop (dir_baton, name, value, pool);
}



static svn_error_t *
do_job (apr_pool_t * pool, const char *src_url, const char *dest_url,
	int start_rev, int end_rev)
{
  svn_ra_plugin_t *ra_src, *ra_dest;
  void *ra_src_sess_baton, *ra_dest_sess_baton;
  svn_delta_editor_t *delta_editor;
  void *edit_baton;
  void *ra_baton;
  const svn_ra_reporter_t *reporter;
  void *report_baton;
  apr_hash_t *config;
  svn_ra_callbacks_t dest_callbacks;
  svn_ra_callbacks_t src_callbacks;
  svn_auth_baton_t *ab;
  apr_array_header_t *providers;

  SVN_ERR (svn_config_get_config (&config, NULL, pool));

  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));

  SVN_ERR (svn_ra_get_ra_library (&ra_src, ra_baton, src_url, pool));

  SVN_ERR (svn_ra_get_ra_library (&ra_dest, ra_baton, dest_url, pool));

  memset ((void *) &dest_callbacks, '\0', sizeof (dest_callbacks));

#if 0
  providers
    = apr_array_make (pool, 10, sizeof (svn_auth_provider_object_t *));


  svn_auth_open (&ab, providers, pool);
#endif

  SVN_ERR (ra_dest->open (&ra_dest_sess_baton,
			  dest_url, &dest_callbacks, NULL, config, pool));

  SVN_ERR (ra_src->open (&ra_src_sess_baton,
			 src_url, &dest_callbacks, NULL, config, pool));


  SVN_ERR (ra_dest->get_commit_editor (ra_dest_sess_baton,
				       &delta_editor,
				       &edit_baton,
				       "Hello World!",
				       my_commit_callback, NULL, pool));

  old_change_dir_prop = delta_editor->change_dir_prop;
  delta_editor->change_dir_prop = new_change_dir_prop;

  old_change_file_prop = delta_editor->change_file_prop;
  delta_editor->change_file_prop = new_change_file_prop;


  SVN_ERR (ra_src->do_diff (ra_src_sess_baton,
			    &reporter,
			    &report_baton,
			    end_rev,
			    NULL,
			    1, 1, src_url, delta_editor, edit_baton, pool));

  SVN_ERR (reporter->set_path (report_baton, "", start_rev, 0, pool));

  SVN_ERR (reporter->finish_report (report_baton));

  return SVN_NO_ERROR;
}

int
main (int argc, char *argv[])
{
  apr_pool_t *top_pool;
  svn_error_t *error = NULL;
  int start_rev, end_rev;
  char *src_url, *dest_url, *s;


  /* Initialize the app.  Send all error messages to 'stderr'.  */
  if (svn_cmdline_init ("minimal_client", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  top_pool = svn_pool_create (NULL);

#define CMD_LINE_ERROR \
        { \
        fprintf(stderr, "%s",  \
                "Usage : svn-push -r N:M [SRC_URL] [DEST_URL]\n"); \
        goto error; \
        } \

  if (argc != 5)
    {
    CMD_LINE_ERROR
    }

  if (strcmp (argv[1], "-r"))
    {
    CMD_LINE_ERROR
    }

  if (sscanf (argv[2], "%i:%i", &start_rev, &end_rev) != 2)
    {
    CMD_LINE_ERROR
    }

  src_url = argv[3];
  dest_url = argv[4];

  error = do_job (top_pool, src_url, dest_url, start_rev, end_rev);

  if (error)
    {
      svn_handle_error (error, stderr, 0);
      return EXIT_FAILURE;
    }


error:
  svn_pool_destroy (top_pool);

  return 0;
}


