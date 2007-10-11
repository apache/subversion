/*
 * conflict-callbacks.c: conflict resolution callbacks specific to the
 * commandline client.
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "cl.h"

#include "svn_private_config.h"




/* Utility to print a full description of the conflict. */
static svn_error_t *
print_conflict_description(const svn_wc_conflict_description_t *desc,
                           apr_pool_t *pool)
{
  SVN_ERR(svn_cmdline_printf(pool, _("Path: %s\n"), desc->path));
  switch (desc->node_kind)
  {
    case svn_node_file:
      SVN_ERR(svn_cmdline_printf(pool, _("Node kind: file\n")));
      SVN_ERR(svn_cmdline_printf(pool, _("Binary file?: %s\n"),
                                 desc->is_binary ? "yes" : "no"));
      if (desc->mime_type)
        SVN_ERR(svn_cmdline_printf(pool, _("Mime-type: %s"),
                                   desc->mime_type));
      break;
    case svn_node_dir:
      SVN_ERR(svn_cmdline_printf(pool, _("Node kind: directory\n")));
      break;
    default:
      SVN_ERR(svn_cmdline_printf(pool, _("Node kind: unknown\n")));
  }

  switch (desc->action)
  {
    case svn_wc_conflict_action_edit:
      SVN_ERR(svn_cmdline_printf(pool, _("Attempting to edit object.\n")));
      break;
    case svn_wc_conflict_action_add:
      SVN_ERR(svn_cmdline_printf(pool, _("Attempting to add object.\n")));
      break;
    case svn_wc_conflict_action_delete:
      SVN_ERR(svn_cmdline_printf(pool, _("Attempting to delete object.\n")));
      break;
    default:
      SVN_ERR(svn_cmdline_printf(pool, _("No action specified!\n")));
      break;
  }

  SVN_ERR(svn_cmdline_printf(pool, _("But:  ")));
  switch (desc->reason)
  {
    case svn_wc_conflict_reason_edited:
      SVN_ERR(svn_cmdline_printf(pool,
                                _("existing object has conflicting edits.\n")));
      break;
    case svn_wc_conflict_reason_obstructed:
      SVN_ERR(svn_cmdline_printf(pool, _("existing object is in the way.\n")));
      break;
    case svn_wc_conflict_reason_deleted:
      SVN_ERR(svn_cmdline_printf(pool, _("existing object is deleted.\n")));
      break;
    case svn_wc_conflict_reason_missing:
      SVN_ERR(svn_cmdline_printf(pool, _("existing object is missing.\n")));
      break;
    case svn_wc_conflict_reason_unversioned:
      SVN_ERR(svn_cmdline_printf(pool, _("existing object is unversioned.\n")));
      break;
   default:
      SVN_ERR(svn_cmdline_printf(pool, _("No reason specified!\n")));
      break;
  }

  if (desc->base_file)
    SVN_ERR(svn_cmdline_printf(pool, _("  Ancestor file: %s\n"),
                               desc->base_file));
  if (desc->repos_file)
    SVN_ERR(svn_cmdline_printf(pool, _("  Repository's file: %s\n"),
                               desc->repos_file));
  if (desc->user_file)
    SVN_ERR(svn_cmdline_printf(pool, _("  User's file: %s\n"),
                               desc->user_file));
  if (desc->merged_file)
    SVN_ERR(svn_cmdline_printf(pool, _("  File with conflict markers: %s\n"),
                               desc->merged_file));

  return SVN_NO_ERROR;
}


/* A conflict callback which does nothing; useful for debugging and/or
   printing a description of the conflict. */
svn_error_t *
svn_cl__ignore_conflicts(svn_wc_conflict_result_t *result,
                         const svn_wc_conflict_description_t *description,
                         void *baton,
                         apr_pool_t *pool)
{
  SVN_ERR(svn_cmdline_printf(pool, _("Discovered a conflict.\n\n")));
  SVN_ERR(print_conflict_description(description, pool));
  SVN_ERR(svn_cmdline_printf(pool, "\n\n"));

  *result = svn_wc_conflict_result_conflicted; /* conflict remains. */
  return SVN_NO_ERROR;
}


/* A conflict callback which does real user prompting. */
svn_error_t *
svn_cl__interactive_conflict_handler(svn_wc_conflict_result_t *result,
                                     const svn_wc_conflict_description_t *desc,
                                     void *baton,
                                     apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_cmdline_prompt_baton_t *pb = baton;

  /* Handle conflicting file contents, which is the most common case. */
  if ((desc->node_kind == svn_node_file)
      && (desc->action == svn_wc_conflict_action_edit)
      && (desc->reason == svn_wc_conflict_reason_edited))
    {
      const char *answer;
      char *prompt;
      svn_boolean_t performed_edit = FALSE;

      SVN_ERR(svn_cmdline_printf(subpool,
                                 _("Conflict discovered in '%s'.\n"),
                                 desc->path));
      while (TRUE)
        {
          svn_pool_clear(subpool);

          prompt = apr_pstrdup(subpool, _("Select: (p)ostpone"));
          if (desc->merged_file)
            prompt = apr_pstrcat(subpool, prompt, _(", (d)iff, (e)dit"),
                                 NULL);
          if (performed_edit)
            prompt = apr_pstrcat(subpool, prompt, _(", (r)esolved"), NULL);
          prompt = apr_pstrcat(subpool, prompt, _(", (h)elp : "), NULL);

          SVN_ERR(svn_cmdline_prompt_user2(&answer, prompt, pb, subpool));

          if ((strcmp(answer, "h") == 0) || (strcmp(answer, "?") == 0))
            {
              SVN_ERR(svn_cmdline_printf(subpool,
              _("  (p)ostpone - mark the conflict to be resolved later\n"
                "  (d)iff     - show all changes made to merged file\n"
                "  (e)dit     - change merged file in an editor\n"
                "  (r)esolved - accept merged version of file\n"
                "  (m)ine     - accept my version of file\n"
                "  (t)heirs   - accept repository's version of file\n"
                "  (l)aunch   - use third-party tool to resolve conflict\n"
                "  (h)elp     - show this list\n\n")));
            }
          if (strcmp(answer, "p") == 0)
            {
              /* Do nothing, let file be marked conflicted. */
              *result = svn_wc_conflict_result_conflicted;
              break;
            }
          if (strcmp(answer, "m") == 0)
            {
              *result = svn_wc_conflict_result_choose_user;
              break;
            }
          if (strcmp(answer, "t") == 0)
            {
              *result = svn_wc_conflict_result_choose_repos;
              break;
            }
          if (strcmp(answer, "d") == 0)
            {
              if (desc->merged_file && desc->base_file)
                {
                  svn_diff_t *diff;
                  svn_stream_t *output;
                  svn_diff_file_options_t *options =
                      svn_diff_file_options_create(subpool);
                  options->ignore_eol_style = TRUE;
                  SVN_ERR(svn_stream_for_stdout(&output, subpool));
                  SVN_ERR(svn_diff_file_diff_2(&diff, desc->base_file,
                                               desc->merged_file,
                                               options, subpool));
                  SVN_ERR(svn_diff_file_output_unified2(output, diff,
                                                        desc->base_file,
                                                        desc->merged_file,
                                                        NULL, NULL,
                                                        APR_LOCALE_CHARSET,
                                                        subpool));
                  performed_edit = TRUE;
                }
              else
                SVN_ERR(svn_cmdline_printf(subpool, _("Invalid option.\n\n")));
            }
          if (strcmp(answer, "e") == 0)
            {
              if (desc->merged_file)
                {
                  svn_error_t *eerr;
                  eerr = svn_cl__edit_file_externally(desc->merged_file,
                                                      NULL, NULL, subpool);
                  if (eerr && (eerr->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR))
                    {
                      SVN_ERR(svn_cmdline_printf(subpool,
                                                 eerr->message ? eerr->message :
                                                 _("No editor found.\n")));
                      svn_error_clear(eerr);
                    }
                  else if (eerr && (eerr->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
                    {
                      SVN_ERR(svn_cmdline_printf(subpool,
                                                 eerr->message ? eerr->message :
                                                 _("Error running editor.\n")));
                      svn_error_clear(eerr);
                    }
                  else if (eerr)
                    return eerr;
                  else
                    performed_edit = TRUE;
                }
              else
                SVN_ERR(svn_cmdline_printf(subpool, _("Invalid option.\n\n")));
            }
          if (strcmp(answer, "l") == 0)
            {
              if (desc->base_file && desc->repos_file && desc->user_file)
                {
                  /* ### TODO: launch $SVNMERGE tool here with 3 fulltexts. */
                  SVN_ERR(svn_cmdline_printf(
                              subpool, _("Feature not yet implemented.\n\n")));
                  performed_edit = TRUE;
                }
              else
                SVN_ERR(svn_cmdline_printf(subpool, _("Invalid option.\n\n")));
            }
          if (strcmp(answer, "r") == 0)
            {
              /* We only allow the user accept the merged version of
                 the file if they've edited it, or at least looked at
                 the diff. */
              if (performed_edit)
                {
                  *result = svn_wc_conflict_result_choose_merged;
                  break;
                }
              else
                SVN_ERR(svn_cmdline_printf(subpool, _("Invalid option.\n\n")));
            }
        }
    }
  /*
    Dealing with obstruction of additions can be tricky.  The
    obstructing item could be unversioned, versioned, or even
    schedule-add.  Here's a matrix of how the caller should behave,
    based on results we return.

                         Unversioned       Versioned       Schedule-Add

      choose_user       skip addition,    skip addition     skip addition
                        add existing item

      choose_repos      destroy file,    schedule-delete,   revert add,
                        add new item.    add new item.      rm file,
                                                            add new item

      postpone               [              bail out                 ]

   */
  else if ((desc->action == svn_wc_conflict_action_add)
           && (desc->reason == svn_wc_conflict_reason_obstructed))
    {
      const char *answer;
      const char *prompt;

      SVN_ERR(svn_cmdline_printf(subpool,
                                 _("Conflict discovered when trying to add '%s'.\n"
                                   "An object of the same name already exists.\n"),
                                 desc->path));
      prompt = _("Select: (p)ostpone, (m)ine, (t)heirs, (h)elp :");

      while (1)
        {
          svn_pool_clear(subpool);

          SVN_ERR(svn_cmdline_prompt_user2(&answer, prompt, pb, subpool));

          if ((strcmp(answer, "h") == 0) || (strcmp(answer, "?") == 0))
            {
              SVN_ERR(svn_cmdline_printf(subpool,
              _("  (p)ostpone - resolve the conflict later\n"
                "  (m)ine     - accept pre-existing item \n"
                "  (t)heirs   - accept incoming item\n"
                "  (h)elp     - show this list\n\n")));
            }
          if (strcmp(answer, "p") == 0)
            {
              *result = svn_wc_conflict_result_conflicted;
              break;
            }
          if (strcmp(answer, "m") == 0)
            {
              *result = svn_wc_conflict_result_choose_user;
              break;
            }
          if (strcmp(answer, "t") == 0)
            {
              *result = svn_wc_conflict_result_choose_repos;
              break;
            }
        }
    }

  else /* other types of conflicts -- do nothing about them. */
    {
      *result = svn_wc_conflict_result_conflicted;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}
