/*
 * info-cmd.c -- Display information about a resource
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_cmdline.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_time.h"
#include "cl.h"


/*** Code. ***/

static void
svn_cl__info_print_time (apr_time_t atime,
                         const char *desc,
                         apr_pool_t *pool)
{
  const char *time_utf8;
  const char *time_stdout;

  time_utf8 = svn_time_to_human_cstring (atime, pool);
  SVN_ERR (svn_utf_cstring_from_utf8 (&time_stdout, time_utf8, pool));
  printf ("%s: %s\n", desc, time_stdout);
}


static svn_error_t *
print_entry (const char *target,
             const svn_wc_entry_t *entry,
             apr_pool_t *pool)
{
  svn_boolean_t text_conflict = FALSE, props_conflict = FALSE;
  const char *name_stdout;

  /* Get a non-UTF8 version of the target. */
  SVN_ERR (svn_cmdline_cstring_from_utf8 (&name_stdout, target, pool));
  printf ("Path: %s\n", name_stdout);

  /* Note: we have to be paranoid about checking that these are
     valid, since svn_wc_entry() doesn't fill them in if they
     aren't in the entries file. */

  if (entry->name && strcmp (entry->name, SVN_WC_ENTRY_THIS_DIR))
    {
      SVN_ERR (svn_cmdline_cstring_from_utf8 (&name_stdout,
                                              entry->name, pool));
      printf ("Name: %s\n", name_stdout);
    }
 
  if (entry->url) 
    {
      SVN_ERR (svn_cmdline_cstring_from_utf8 (&name_stdout, entry->url, pool));
      printf ("URL: %s\n", name_stdout);
    }
           
  if (entry->repos) 
    {
      SVN_ERR (svn_cmdline_cstring_from_utf8 (&name_stdout,
                                              entry->repos, pool));
      printf ("Repository: %s\n", name_stdout);
    }
 
  if (entry->uuid) 
    {
      printf ("Repository UUID: %s\n", entry->uuid);
    }
 
  if (SVN_IS_VALID_REVNUM (entry->revision))
    printf ("Revision: %" SVN_REVNUM_T_FMT "\n", entry->revision);

  switch (entry->kind) 
    {
    case svn_node_file:
      printf ("Node Kind: file\n");
      {
        const char *dir_name;
        svn_path_split (target, &dir_name, NULL, pool);
        SVN_ERR (svn_wc_conflicted_p (&text_conflict, &props_conflict,
                                      dir_name, entry, pool));
      }
      break;
          
    case svn_node_dir:
      printf ("Node Kind: directory\n");
      SVN_ERR (svn_wc_conflicted_p (&text_conflict, &props_conflict,
                                    target, entry, pool));
      break;
          
    case svn_node_none:
      printf ("Node Kind: none\n");
      break;
          
    case svn_node_unknown:
    default:
      printf ("Node Kind: unknown\n");
      break;
    }

  switch (entry->schedule) 
    {
    case svn_wc_schedule_normal:
      printf ("Schedule: normal\n");
      break;
          
    case svn_wc_schedule_add:
      printf ("Schedule: add\n");
      break;
          
    case svn_wc_schedule_delete:
      printf ("Schedule: delete\n");
      break;
          
    case svn_wc_schedule_replace:
      printf ("Schedule: replace\n");
      break;
          
    default:
      break;
    }

  if (entry->copied)
    {
      if (entry->copyfrom_url) 
        {
          SVN_ERR (svn_cmdline_cstring_from_utf8 (&name_stdout,
                                                  entry->copyfrom_url,
                                                  pool));
          printf ("Copied From URL: %s\n", name_stdout);
        }
 
      if (SVN_IS_VALID_REVNUM (entry->copyfrom_rev))
        printf ("Copied From Rev: %" SVN_REVNUM_T_FMT "\n", 
                entry->copyfrom_rev);
    }
 
  if (entry->cmt_author) 
    {
      SVN_ERR (svn_cmdline_cstring_from_utf8 (&name_stdout,
                                              entry->cmt_author, pool));
      printf ("Last Changed Author: %s\n", name_stdout);
    }
 
  if (SVN_IS_VALID_REVNUM (entry->cmt_rev))
    printf ("Last Changed Rev: %" SVN_REVNUM_T_FMT "\n", entry->cmt_rev);

  if (entry->cmt_date)
    svn_cl__info_print_time (entry->cmt_date, "Last Changed Date", pool);

  if (entry->text_time)
    svn_cl__info_print_time (entry->text_time, "Text Last Updated", pool);

  if (entry->prop_time)
    svn_cl__info_print_time (entry->prop_time, "Properties Last Updated",
                             pool);
 
  if (entry->checksum) 
    {
      SVN_ERR (svn_cmdline_cstring_from_utf8 (&name_stdout,
                                              entry->checksum, pool));
      printf ("Checksum: %s\n", name_stdout);
    }
 
  if (text_conflict && entry->conflict_old) 
    {
      SVN_ERR (svn_cmdline_cstring_from_utf8 (&name_stdout,
                                              entry->conflict_old, pool));
      printf ("Conflict Previous Base File: %s\n", name_stdout);
    }
 
  if (text_conflict && entry->conflict_wrk) 
    {
      SVN_ERR (svn_cmdline_cstring_from_utf8 (&name_stdout,
                                              entry->conflict_wrk, pool));
      printf ("Conflict Previous Working File: %s\n", name_stdout);
    }
 
  if (text_conflict && entry->conflict_new) 
    {
      SVN_ERR (svn_cmdline_cstring_from_utf8 (&name_stdout,
                                              entry->conflict_new, pool));
      printf ("Conflict Current Base File: %s\n", name_stdout);
    }
 
  if (props_conflict && entry->prejfile) 
    {
      SVN_ERR (svn_cmdline_cstring_from_utf8 (&name_stdout,
                                              entry->prejfile, pool));
      printf ("Conflict Properties File: %s\n", name_stdout);
    }
 
  /* Print extra newline separator. */
  printf ("\n");

  return SVN_NO_ERROR;
}


static svn_error_t *
info_found_entry_callback (const char *path,
                           const svn_wc_entry_t *entry,
                           void *walk_baton,
                           apr_pool_t *pool)
{
  /* We're going to receive dirents twice;  we want to ignore the
     first one (where it's a child of a parent dir), and only print
     the second one (where we're looking at THIS_DIR.)  */
  if ((entry->kind == svn_node_dir) 
      && (strcmp (entry->name, SVN_WC_ENTRY_THIS_DIR)))
    return SVN_NO_ERROR;

  return print_entry (path, entry, pool);
}


static const svn_wc_entry_callbacks_t 
entry_walk_callbacks =
  {
    info_found_entry_callback
  };



/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__info (apr_getopt_t *os,
              void *baton,
              apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets;
  apr_pool_t *subpool = svn_pool_create (pool);
  int i;

  SVN_ERR (svn_opt_args_to_target_array (&targets, os, 
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  /* Add "." if user passed 0 arguments. */
  svn_opt_push_implicit_dot_target (targets, pool);

  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = ((const char **) (targets->elts))[i];
      svn_wc_adm_access_t *adm_access;
      const svn_wc_entry_t *entry;

      svn_pool_clear (subpool);
      SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, target, FALSE,
                                      opt_state->recursive, subpool));
      SVN_ERR (svn_wc_entry (&entry, target, adm_access, FALSE, subpool));
      if (! entry)
        {
          /* Print non-versioned message and extra newline separator. */

          const char *target_stdout;
          /* Get a non-UTF8 version of the target. */
          SVN_ERR (svn_cmdline_cstring_from_utf8 (&target_stdout, target,
                                                  subpool));

          printf ("%s:  (Not a versioned resource)\n\n", target_stdout);
          continue;
        }

      if (entry->kind == svn_node_file)
        SVN_ERR (print_entry (target, entry, subpool));

      else if (entry->kind == svn_node_dir)
        {
          if (opt_state->recursive)
            /* the generic entry-walker: */
            SVN_ERR (svn_wc_walk_entries (target, adm_access,
                                          &entry_walk_callbacks, NULL,
                                          FALSE, pool));
          else
            SVN_ERR (print_entry (target, entry, subpool));
        }
      SVN_ERR (svn_cl__check_cancel (ctx->cancel_baton));
    }
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}
