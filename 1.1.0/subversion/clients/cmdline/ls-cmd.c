/*
 * ls-cmd.c -- list a URL
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

#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_sorts.h"
#include "svn_pools.h"
#include "svn_time.h"
#include "cl.h"


/*** Code. ***/

static svn_error_t *
print_dirents (apr_hash_t *dirents,
               svn_boolean_t verbose,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  apr_array_header_t *array;
  int i;

  array = svn_sort__hash (dirents, svn_sort_compare_items_as_paths, pool);
  
  for (i = 0; i < array->nelts; ++i)
    {
      const char *utf8_entryname;
      svn_dirent_t *dirent;
      svn_sort__item_t *item;

      if (ctx->cancel_func)
        SVN_ERR (ctx->cancel_func (ctx->cancel_baton));
     
      item = &APR_ARRAY_IDX (array, i, svn_sort__item_t);

      utf8_entryname = item->key;

      dirent = apr_hash_get (dirents, utf8_entryname, item->klen);

      if (verbose)
        {
          apr_time_t now = apr_time_now();
          apr_time_exp_t exp_time;
          apr_status_t apr_err;
          apr_size_t size;
          char timestr[20];
          const char *sizestr, *utf8_timestr;
          
          /* svn_time_to_human_cstring gives us something *way* too long
             to use for this, so we have to roll our own.  We include
             the year if the entry's time is not within half a year. */
          apr_time_exp_lt (&exp_time, dirent->time);
          if (apr_time_sec(now - dirent->time) < (365 * 86400 / 2)
              && apr_time_sec(dirent->time - now) < (365 * 86400 / 2))
            {
              apr_err = apr_strftime (timestr, &size, sizeof (timestr),
                                      "%b %d %H:%M", &exp_time);
            }
          else
            {
              apr_err = apr_strftime (timestr, &size, sizeof (timestr),
                                      "%b %d  %Y", &exp_time);
            }

          /* if that failed, just zero out the string and print nothing */
          if (apr_err)
            timestr[0] = '\0';

          /* we need it in UTF-8. */
          SVN_ERR (svn_utf_cstring_to_utf8 (&utf8_timestr, timestr, pool));

          sizestr = apr_psprintf (pool, "%" SVN_FILESIZE_T_FMT, dirent->size);

          SVN_ERR (svn_cmdline_printf
                   (pool, "%7ld %-8.8s %10s %12s %s%s\n",
                    dirent->created_rev,
                    dirent->last_author ? dirent->last_author : " ? ",
                    (dirent->kind == svn_node_file) ? sizestr : "",
                    timestr,
                    utf8_entryname,
                    (dirent->kind == svn_node_dir) ? "/" : ""));
        }
      else
        {
          SVN_ERR (svn_cmdline_printf (pool, "%s%s\n", utf8_entryname, 
                                       (dirent->kind == svn_node_dir)
                                       ? "/" : ""));
        }
    }

  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__ls (apr_getopt_t *os,
            void *baton,
            apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  int i;
  apr_pool_t *subpool = svn_pool_create (pool); 

  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target (targets, pool);

  /* For each target, try to list it. */
  for (i = 0; i < targets->nelts; i++)
    {
      apr_hash_t *dirents;
      const char *target = ((const char **) (targets->elts))[i];
     
      SVN_ERR (svn_cl__check_cancel (ctx->cancel_baton));
      SVN_ERR (svn_client_ls (&dirents, target, &(opt_state->start_revision),
                              opt_state->recursive, ctx, subpool));

      SVN_ERR (print_dirents (dirents, opt_state->verbose, ctx, subpool));
      svn_pool_clear (subpool);
    }

  return SVN_NO_ERROR;
}
