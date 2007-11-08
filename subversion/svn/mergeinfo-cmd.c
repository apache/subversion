/*
 * mergeinfo-cmd.c -- Query merge-relative info.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#include "svn_pools.h"
#include "svn_client.h"
#include "svn_cmdline.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_types.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

static void
print_merge_ranges(apr_array_header_t *ranges, apr_pool_t *pool)
{
  int i;
  for (i = 0; i < ranges->nelts; i++)
    {
      svn_merge_range_t *range = APR_ARRAY_IDX(ranges, i, svn_merge_range_t *);
      svn_cmdline_printf(pool, "r%ld:%ld%s", range->start, range->end,
                         (i == (ranges->nelts - 1)) ? "" : ", ");
    }
  svn_cmdline_printf(pool, "\n");
}


static const char *
relative_path(const char *root_url,
              const char *url,
              apr_pool_t *pool)
{
  const char *relurl = svn_path_is_child(root_url, url, pool);
  return relurl ? apr_pstrcat(pool, "/",
                              svn_path_uri_decode(relurl, pool), NULL)
                : "/";
}

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__mergeinfo(apr_getopt_t *os,
                  void *baton,
                  apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_pool_t *subpool = svn_pool_create(pool);
  int i;

  SVN_ERR(svn_opt_args_to_target_array2(&targets, os,
                                        opt_state->targets, pool));

  /* Add "." if user passed 0 arguments. */
  svn_opt_push_implicit_dot_target(targets, pool);

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      const char *truepath;
      svn_opt_revision_t peg_revision;
      apr_hash_t *mergeinfo;
      const char *root_url;
      apr_hash_index_t *hi;
      apr_pool_t *iterpool;

      svn_pool_clear(subpool);
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Parse the path into a path and peg revision. */
      SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target, subpool));

      /* If no peg-rev was attached to a URL target, then assume HEAD. */
      if ((peg_revision.kind == svn_opt_revision_unspecified)
          && svn_path_is_url(target))
        peg_revision.kind = svn_opt_revision_head;

      /* If no peg-rev was attached to a non-URL target, then assume BASE. */
      if ((peg_revision.kind == svn_opt_revision_unspecified)
          && (! svn_path_is_url(target)))
        peg_revision.kind = svn_opt_revision_base;

      /* Get the already-merged information. */
      SVN_ERR(svn_client_mergeinfo_get_merged(&mergeinfo, truepath,
                                              &peg_revision, ctx, subpool));

      svn_cmdline_printf(pool, _("Path: %s\n"),
                         svn_path_local_style(truepath, pool));
      if (! mergeinfo)
        {
          svn_cmdline_printf(pool, "\n");
          continue;
        }

      SVN_ERR(svn_client_root_url_from_path(&root_url, truepath, ctx, pool));

      iterpool = svn_pool_create(subpool);
      for (hi = apr_hash_first(NULL, mergeinfo); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          const char *merge_source;
          void *val;
          apr_array_header_t *merge_ranges;
          svn_error_t *err;

          svn_pool_clear(iterpool);
          apr_hash_this(hi, &key, NULL, &val);
          merge_source = key;

          svn_cmdline_printf(iterpool, _("  Source path: %s\n"),
                             relative_path(root_url, merge_source, pool));
          svn_cmdline_printf(iterpool, _("    Merged ranges: "));
          merge_ranges = val;
          print_merge_ranges(merge_ranges, iterpool);

          /* Now fetch the available merges for this source. */

          /* ### FIXME: There's no reason why this API should fail to
             ### answer the question, short of something being quite
             ### wrong with the question.  Certainly, that the merge
             ### source URL can't be found in HEAD shouldn't mean we
             ### can't get any decent information about it out of the
             ### system.  It may just mean the system has to work
             ### harder to provide that information.
          */
          svn_cmdline_printf(iterpool, _("    Eligible ranges: "));
          err = svn_client_mergeinfo_get_available(&merge_ranges,
                                                   truepath,
                                                   &peg_revision,
                                                   merge_source,
                                                   ctx,
                                                   subpool);
          if (err)
            {
              if ((err->apr_err == SVN_ERR_FS_NOT_FOUND)
                  || (err->apr_err == SVN_ERR_RA_DAV_PATH_NOT_FOUND))
                {
                  svn_error_clear(err);
                  svn_cmdline_printf(subpool, _("(source no longer available "
                                                "in HEAD)\n"));
                }
              else
                {
                  return err;
                }
            }
          else
            {
              print_merge_ranges(merge_ranges, iterpool);
            }
        }
      svn_cmdline_printf(subpool, "\n");

      svn_pool_destroy(iterpool);
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}
