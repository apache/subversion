/*
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_ra.h"
#include "svn_repos.h"
#include "svn_path.h"
#include "svn_private_config.h"

#include "svnrdump.h"
#include "dump_editor.h"

static svn_error_t *
replay_revstart(svn_revnum_t revision,
                void *replay_baton,
                const svn_delta_editor_t **editor,
                void **edit_baton,
                apr_hash_t *rev_props,
                apr_pool_t *pool)
{
  struct replay_baton *rb = replay_baton;
  /* Editing this revision has just started; dump the revprops
     before invoking the editor callbacks */
  svn_stringbuf_t *propstring = svn_stringbuf_create("", pool);
  svn_stream_t *stdout_stream;

  /* Create an stdout stream */
  svn_stream_for_stdout(&stdout_stream, pool);

        /* Print revision number and prepare the propstring */
  SVN_ERR(svn_stream_printf(stdout_stream, pool,
          SVN_REPOS_DUMPFILE_REVISION_NUMBER
          ": %ld\n", revision));
  write_hash_to_stringbuf(rev_props, FALSE, &propstring, pool);
  svn_stringbuf_appendbytes(propstring, "PROPS-END\n", 10);

  /* prop-content-length header */
  SVN_ERR(svn_stream_printf(stdout_stream, pool,
          SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH
          ": %" APR_SIZE_T_FMT "\n", propstring->len));

  /* content-length header */
  SVN_ERR(svn_stream_printf(stdout_stream, pool,
          SVN_REPOS_DUMPFILE_CONTENT_LENGTH
          ": %" APR_SIZE_T_FMT "\n\n", propstring->len));

  /* Print the revprops now */
  SVN_ERR(svn_stream_write(stdout_stream, propstring->data,
         &(propstring->len)));

  svn_stream_close(stdout_stream);

  /* Extract editor and editor_baton from the replay_baton and
     set them so that the editor callbacks can use them */
  *editor = rb->editor;
  *edit_baton = rb->edit_baton;

  return SVN_NO_ERROR;
}

static svn_error_t *
replay_revend(svn_revnum_t revision,
              void *replay_baton,
              const svn_delta_editor_t *editor,
              void *edit_baton,
              apr_hash_t *rev_props,
              apr_pool_t *pool)
{
  /* Editor has finished for this revision and close_edit has
     been called; do nothing: just continue to the next
     revision */
  struct replay_baton *rb = replay_baton;
  if (rb->verbose)
    svn_cmdline_fprintf(stderr, pool, "* Dumped revision %lu\n", revision);
  return SVN_NO_ERROR;
}

static svn_error_t *
open_connection(svn_ra_session_t *session, const char *url, apr_pool_t *pool)
{
  svn_client_ctx_t *ctx = NULL;
  SVN_ERR(svn_config_ensure (NULL, pool));
  SVN_ERR(svn_client_create_context (&ctx, pool));
  SVN_ERR(svn_ra_initialize(pool));

  SVN_ERR(svn_config_get_config(&(ctx->config), NULL, pool));

  /* Default authentication providers for non-interactive use */
  SVN_ERR(svn_cmdline_create_auth_baton(&(ctx->auth_baton), TRUE,
                NULL, NULL, NULL, FALSE,
                FALSE, NULL, NULL, NULL,
                pool));
  SVN_ERR(svn_client_open_ra_session(&session, url, ctx, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
replay_range(svn_ra_session_t *session, svn_revnum_t start_revision,
	     svn_revnum_t end_revision, apr_pool_t *pool,
	     svn_boolean_t verbose)
{
  const svn_delta_editor_t *dump_editor;
  struct replay_baton *replay_baton;
  void *dump_baton;

  SVN_ERR(get_dump_editor(&dump_editor,
                          &dump_baton, pool));

  replay_baton = apr_pcalloc(pool, sizeof(*replay_baton));
  replay_baton->editor = dump_editor;
  replay_baton->edit_baton = dump_baton;
  replay_baton->verbose = verbose;
  SVN_ERR(svn_cmdline_printf(pool, SVN_REPOS_DUMPFILE_MAGIC_HEADER ": %d\n",
           SVN_REPOS_DUMPFILE_FORMAT_VERSION));
  SVN_ERR(svn_ra_replay_range(session, start_revision, end_revision,
                              0, TRUE, replay_revstart, replay_revend,
                              replay_baton, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
usage(FILE *out_stream)
{
  fprintf(out_stream,
    _("usage: svnrdump URL [-r LOWER[:UPPER]]\n\n"
    "Dump the contents of repository at remote URL to stdout in a 'dumpfile'\n"
    "portable format.  Dump revisions LOWER rev through UPPER rev.\n"
    "LOWER defaults to 1 and UPPER defaults to the highest possible revision\n"
    "if omitted.\n"));
  return SVN_NO_ERROR;
}

int
main(int argc, const char **argv)
{
  int i;
  const char *url = NULL;
  char *revision_cut = NULL;
  svn_revnum_t start_revision = svn_opt_revision_unspecified;
  svn_revnum_t end_revision = svn_opt_revision_unspecified;
  svn_boolean_t verbose = FALSE;
  apr_pool_t *pool = NULL;
  svn_ra_session_t *session = NULL;

  if (svn_cmdline_init ("svnrdump", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  pool = svn_pool_create(NULL);

  for (i = 1; i < argc; i++) {
    if (!strncmp("-r", argv[i], 2)) {
      revision_cut = strchr(argv[i] + 2, ':');
      if (revision_cut) {
        start_revision = (svn_revnum_t) strtoul(argv[i] + 2, &revision_cut, 10);
        end_revision = (svn_revnum_t) strtoul(revision_cut + 1, NULL, 10);
      }
      else
        start_revision = (svn_revnum_t) strtoul(argv[i] + 2, NULL, 10);
    } else if (!strcmp("-v", argv[i]) || !strcmp("--verbose", argv[i])) {
      verbose = TRUE;
    } else if (!strcmp("help", argv[i]) || !strcmp("--help", argv[i])) {
      SVN_INT_ERR(usage(stdout));
      return EXIT_SUCCESS;
    } else if (*argv[i] == '-' || url) {
      SVN_INT_ERR(usage(stderr));
      return EXIT_FAILURE;
    } else
      url = argv[i];
  }

  if (!url || !svn_path_is_url(url)) {
    usage(stderr);
    return EXIT_FAILURE;
  }
  SVN_INT_ERR(open_connection(session, url, pool));

  /* Have sane start_revision and end_revision defaults if unspecified */
  if (start_revision == svn_opt_revision_unspecified)
    start_revision = 1;
  if (end_revision == svn_opt_revision_unspecified)
    SVN_INT_ERR(svn_ra_get_latest_revnum(session, &end_revision, pool));

  SVN_INT_ERR(replay_range(session, start_revision, end_revision, pool, verbose));

  svn_pool_destroy(pool);

  return 0;
}
