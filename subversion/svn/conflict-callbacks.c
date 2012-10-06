/*
 * conflict-callbacks.c: conflict resolution callbacks specific to the
 * commandline client.
 *
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

#include <apr_xlate.h>  /* for APR_LOCALE_CHARSET */

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_sorts.h"

#include "cl.h"
#include "tree-conflicts.h"

#include "svn_private_config.h"



struct svn_cl__interactive_conflict_baton_t {
  svn_cl__accept_t accept_which;
  apr_hash_t *config;
  const char *editor_cmd;
  svn_boolean_t external_failed;
  svn_cmdline_prompt_baton_t *pb;
  const char *path_prefix;
};

svn_error_t *
svn_cl__get_conflict_func_interactive_baton(
  svn_cl__interactive_conflict_baton_t **b,
  svn_cl__accept_t accept_which,
  apr_hash_t *config,
  const char *editor_cmd,
  svn_cancel_func_t cancel_func,
  void *cancel_baton,
  apr_pool_t *result_pool)
{
  svn_cmdline_prompt_baton_t *pb = apr_palloc(result_pool, sizeof(*pb));
  pb->cancel_func = cancel_func;
  pb->cancel_baton = cancel_baton;

  *b = apr_palloc(result_pool, sizeof(**b));
  (*b)->accept_which = accept_which;
  (*b)->config = config;
  (*b)->editor_cmd = editor_cmd;
  (*b)->external_failed = FALSE;
  (*b)->pb = pb;
  SVN_ERR(svn_dirent_get_absolute(&(*b)->path_prefix, "", result_pool));

  return SVN_NO_ERROR;
}

svn_cl__accept_t
svn_cl__accept_from_word(const char *word)
{
  /* Shorthand options are consistent with  svn_cl__conflict_handler(). */
  if (strcmp(word, SVN_CL__ACCEPT_POSTPONE) == 0
      || strcmp(word, "p") == 0 || strcmp(word, ":-P") == 0)
    return svn_cl__accept_postpone;
  if (strcmp(word, SVN_CL__ACCEPT_BASE) == 0)
    /* ### shorthand? */
    return svn_cl__accept_base;
  if (strcmp(word, SVN_CL__ACCEPT_WORKING) == 0)
    /* ### shorthand? */
    return svn_cl__accept_working;
  if (strcmp(word, SVN_CL__ACCEPT_MINE_CONFLICT) == 0
      || strcmp(word, "mc") == 0 || strcmp(word, "X-)") == 0)
    return svn_cl__accept_mine_conflict;
  if (strcmp(word, SVN_CL__ACCEPT_THEIRS_CONFLICT) == 0
      || strcmp(word, "tc") == 0 || strcmp(word, "X-(") == 0)
    return svn_cl__accept_theirs_conflict;
  if (strcmp(word, SVN_CL__ACCEPT_MINE_FULL) == 0
      || strcmp(word, "mf") == 0 || strcmp(word, ":-)") == 0)
    return svn_cl__accept_mine_full;
  if (strcmp(word, SVN_CL__ACCEPT_THEIRS_FULL) == 0
      || strcmp(word, "tf") == 0 || strcmp(word, ":-(") == 0)
    return svn_cl__accept_theirs_full;
  if (strcmp(word, SVN_CL__ACCEPT_EDIT) == 0
      || strcmp(word, "e") == 0 || strcmp(word, ":-E") == 0)
    return svn_cl__accept_edit;
  if (strcmp(word, SVN_CL__ACCEPT_LAUNCH) == 0
      || strcmp(word, "l") == 0 || strcmp(word, ":-l") == 0)
    return svn_cl__accept_launch;
  /* word is an invalid action. */
  return svn_cl__accept_invalid;
}


/* Print on stdout a diff between the 'base' and 'merged' files, if both of
 * those are available, else between 'their' and 'my' files, of DESC. */
static svn_error_t *
show_diff(const svn_wc_conflict_description2_t *desc,
          apr_pool_t *pool)
{
  const char *path1, *path2;
  svn_diff_t *diff;
  svn_stream_t *output;
  svn_diff_file_options_t *options;

  if (desc->merged_file && desc->base_abspath)
    {
      /* Show the conflict markers to the user */
      path1 = desc->base_abspath;
      path2 = desc->merged_file;
    }
  else
    {
      /* There's no base file, but we can show the
         difference between mine and theirs. */
      path1 = desc->their_abspath;
      path2 = desc->my_abspath;
    }

  options = svn_diff_file_options_create(pool);
  options->ignore_eol_style = TRUE;
  SVN_ERR(svn_stream_for_stdout(&output, pool));
  SVN_ERR(svn_diff_file_diff_2(&diff, path1, path2,
                               options, pool));
  return svn_diff_file_output_unified3(output, diff,
                                       path1, path2,
                                       NULL, NULL,
                                       APR_LOCALE_CHARSET,
                                       NULL, FALSE,
                                       pool);
}


/* Print on stdout just the conflict hunks of a diff among the 'base', 'their'
 * and 'my' files of DESC. */
static svn_error_t *
show_conflicts(const svn_wc_conflict_description2_t *desc,
               apr_pool_t *pool)
{
  svn_diff_t *diff;
  svn_stream_t *output;
  svn_diff_file_options_t *options;

  options = svn_diff_file_options_create(pool);
  options->ignore_eol_style = TRUE;
  SVN_ERR(svn_stream_for_stdout(&output, pool));
  SVN_ERR(svn_diff_file_diff3_2(&diff,
                                desc->base_abspath,
                                desc->my_abspath,
                                desc->their_abspath,
                                options, pool));
  /* ### Consider putting the markers/labels from
     ### svn_wc__merge_internal in the conflict description. */
  return svn_diff_file_output_merge2(output, diff,
                                     desc->base_abspath,
                                     desc->my_abspath,
                                     desc->their_abspath,
                                     _("||||||| ORIGINAL"),
                                     _("<<<<<<< MINE (select with 'mc')"),
                                     _(">>>>>>> THEIRS (select with 'tc')"),
                                     "=======",
                                     svn_diff_conflict_display_only_conflicts,
                                     pool);
}


/* Run an external editor, passing it the 'merged' file in DESC, or, if the
 * 'merged' file is null, return an error. The tool to use is determined by
 * B->editor_cmd, B->config and environment variables; see
 * svn_cl__edit_file_externally() for details.
 *
 * If the tool runs, set *PERFORMED_EDIT to true; if a tool is not
 * configured or cannot run, do not touch *PERFORMED_EDIT, report the error
 * on stderr, and return SVN_NO_ERROR; if any other error is encountered,
 * return that error. */
static svn_error_t *
open_editor(svn_boolean_t *performed_edit,
            const svn_wc_conflict_description2_t *desc,
            svn_cl__interactive_conflict_baton_t *b,
            apr_pool_t *pool)
{
  svn_error_t *err;

  if (desc->merged_file)
    {
      err = svn_cl__edit_file_externally(desc->merged_file, b->editor_cmd,
                                         b->config, pool);
      if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR))
        {
          svn_error_t *root_err = svn_error_root_cause(err);

          SVN_ERR(svn_cmdline_fprintf(stderr, pool, "%s\n",
                                      root_err->message ? root_err->message :
                                      _("No editor found.")));
          svn_error_clear(err);
        }
      else if (err && (err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
        {
          svn_error_t *root_err = svn_error_root_cause(err);

          SVN_ERR(svn_cmdline_fprintf(stderr, pool, "%s\n",
                                      root_err->message ? root_err->message :
                                      _("Error running editor.")));
          svn_error_clear(err);
        }
      else if (err)
        return svn_error_trace(err);
      else
        *performed_edit = TRUE;
    }
  else
    SVN_ERR(svn_cmdline_fprintf(stderr, pool,
                                _("Invalid option; there's no "
                                  "merged version to edit.\n\n")));

  return SVN_NO_ERROR;
}


/* Run an external merge tool, passing it the 'base', 'their', 'my' and
 * 'merged' files in DESC. The tool to use is determined by B->config and
 * environment variables; see svn_cl__merge_file_externally() for details.
 *
 * If the tool runs, set *PERFORMED_EDIT to true; if a tool is not
 * configured or cannot run, do not touch *PERFORMED_EDIT, report the error
 * on stderr, and return SVN_NO_ERROR; if any other error is encountered,
 * return that error.  */
static svn_error_t *
launch_resolver(svn_boolean_t *performed_edit,
                const svn_wc_conflict_description2_t *desc,
                svn_cl__interactive_conflict_baton_t *b,
                apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_cl__merge_file_externally(desc->base_abspath, desc->their_abspath,
                                      desc->my_abspath, desc->merged_file,
                                      desc->local_abspath, b->config, NULL,
                                      pool);
  if (err && err->apr_err == SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL)
    {
      SVN_ERR(svn_cmdline_fprintf(stderr, pool, "%s\n",
                                  err->message ? err->message :
                                  _("No merge tool found, "
                                    "try '(m) merge' instead.\n")));
      svn_error_clear(err);
    }
  else if (err && err->apr_err == SVN_ERR_EXTERNAL_PROGRAM)
    {
      SVN_ERR(svn_cmdline_fprintf(stderr, pool, "%s\n",
                                  err->message ? err->message :
                             _("Error running merge tool, "
                               "try '(m) merge' instead.")));
      svn_error_clear(err);
    }
  else if (err)
    return svn_error_trace(err);
  else if (performed_edit)
    *performed_edit = TRUE;

  return SVN_NO_ERROR;
}

/* Ask the user what to do about the text conflict described by DESC.
 * Return the answer in RESULT. B is the conflict baton for this
 * conflict resolution session.
 * SCRATCH_POOL is used for temporary allocations. */
static svn_error_t *
handle_text_conflict(svn_wc_conflict_result_t *result,
                     const svn_wc_conflict_description2_t *desc,
                     svn_cl__interactive_conflict_baton_t *b,
                     apr_pool_t *scratch_pool)
{
  const char *answer;
  char *prompt;
  svn_boolean_t diff_allowed = FALSE;
  /* Have they done something that might have affected the merged
     file (so that we need to save a .edited copy)? */
  svn_boolean_t performed_edit = FALSE;
  /* Have they done *something* (edit, look at diff, etc) to
     give them a rational basis for choosing (r)esolved? */
  svn_boolean_t knows_something = FALSE;

  SVN_ERR_ASSERT(desc->kind == svn_wc_conflict_kind_text);

  SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                              _("Conflict discovered in file '%s'.\n"),
                              svn_cl__local_style_skip_ancestor(
                                b->path_prefix, desc->local_abspath,
                                scratch_pool)));

  /* Diffing can happen between base and merged, to show conflict
     markers to the user (this is the typical 3-way merge
     scenario), or if no base is available, we can show a diff
     between mine and theirs. */
  if ((desc->merged_file && desc->base_abspath)
      || (!desc->base_abspath && desc->my_abspath && desc->their_abspath))
    diff_allowed = TRUE;

  while (TRUE)
    {
      svn_pool_clear(scratch_pool);

      prompt = apr_pstrdup(scratch_pool, _("Select: (p) postpone"));

      if (diff_allowed)
        {
          prompt = apr_pstrcat(scratch_pool, prompt,
                               _(", (df) diff-full, (e) edit, (m) merge"),
                               (char *)NULL);

          if (knows_something)
            prompt = apr_pstrcat(scratch_pool, prompt, _(", (r) resolved"),
                                 (char *)NULL);

          if (! desc->is_binary)
            prompt = apr_pstrcat(scratch_pool, prompt,
                                 _(",\n        (mc) mine-conflict, "
                                   "(tc) theirs-conflict"),
                                 (char *)NULL);
        }
      else
        {
          if (knows_something)
            prompt = apr_pstrcat(scratch_pool, prompt, _(", (r) resolved"),
                                 (char *)NULL);
          prompt = apr_pstrcat(scratch_pool, prompt,
                               _(",\n        "
                                 "(mf) mine-full, (tf) theirs-full"),
                               (char *)NULL);
        }

      prompt = apr_pstrcat(scratch_pool, prompt, ",\n        ", (char *)NULL);
      prompt = apr_pstrcat(scratch_pool, prompt,
                           _("(s) show all options: "),
                           (char *)NULL);

      SVN_ERR(svn_cmdline_prompt_user2(&answer, prompt, b->pb, scratch_pool));

      if (strcmp(answer, "s") == 0)
        {
          /* These are used in svn_cl__accept_from_word(). */
          SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
          _("\n"
            "  (e)  edit             - change merged file in an editor\n"
            "  (df) diff-full        - show all changes made to merged "
                                      "file\n"
            "  (r)  resolved         - accept merged version of file\n"
            "\n"
            "  (dc) display-conflict - show all conflicts "
                                      "(ignoring merged version)\n"
            "  (mc) mine-conflict    - accept my version for all "
                                      "conflicts (same)\n"
            "  (tc) theirs-conflict  - accept their version for all "
                                      "conflicts (same)\n"
            "\n"
            "  (mf) mine-full        - accept my version of entire file "
                                      "(even non-conflicts)\n"
            "  (tf) theirs-full      - accept their version of entire "
                                      "file (same)\n"
            "\n"
            "  (p)  postpone         - mark the conflict to be "
                                      "resolved later\n"
            "  (m)  merge            - use internal merge tool to "
                                      "resolve conflict\n"
            "  (l)  launch           - launch external tool to "
                                      "resolve conflict\n"
            "  (s)  show all         - show this list\n\n")));
        }
      else if (strcmp(answer, "p") == 0 || strcmp(answer, ":-P") == 0)
        {
          /* Do nothing, let file be marked conflicted. */
          result->choice = svn_wc_conflict_choose_postpone;
          break;
        }
      else if (strcmp(answer, "mc") == 0 || strcmp(answer, "X-)") == 0)
        {
          if (desc->is_binary)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                          _("Invalid option; cannot choose "
                                            "based on conflicts in a "
                                            "binary file.\n\n")));
              continue;
            }
          result->choice = svn_wc_conflict_choose_mine_conflict;
          if (performed_edit)
            result->save_merged = TRUE;
          break;
        }
      else if (strcmp(answer, "tc") == 0 || strcmp(answer, "X-(") == 0)
        {
          if (desc->is_binary)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                          _("Invalid option; cannot choose "
                                            "based on conflicts in a "
                                            "binary file.\n\n")));
              continue;
            }
          result->choice = svn_wc_conflict_choose_theirs_conflict;
          if (performed_edit)
            result->save_merged = TRUE;
          break;
        }
      else if (strcmp(answer, "mf") == 0 || strcmp(answer, ":-)") == 0)
        {
          result->choice = svn_wc_conflict_choose_mine_full;
          if (performed_edit)
            result->save_merged = TRUE;
          break;
        }
      else if (strcmp(answer, "tf") == 0 || strcmp(answer, ":-(") == 0)
        {
          result->choice = svn_wc_conflict_choose_theirs_full;
          if (performed_edit)
            result->save_merged = TRUE;
          break;
        }
      else if (strcmp(answer, "dc") == 0)
        {
          if (desc->is_binary)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                          _("Invalid option; cannot "
                                            "display conflicts for a "
                                            "binary file.\n\n")));
              continue;
            }
          else if (! (desc->my_abspath && desc->base_abspath &&
                      desc->their_abspath))
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                          _("Invalid option; original "
                                            "files not available.\n\n")));
              continue;
            }
          SVN_ERR(show_conflicts(desc, scratch_pool));
          knows_something = TRUE;
        }
      else if (strcmp(answer, "df") == 0)
        {
          if (! diff_allowed)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                             _("Invalid option; there's no "
                                "merged version to diff.\n\n")));
              continue;
            }

          SVN_ERR(show_diff(desc, scratch_pool));
          knows_something = TRUE;
        }
      else if (strcmp(answer, "e") == 0 || strcmp(answer, ":-E") == 0)
        {
          SVN_ERR(open_editor(&performed_edit, desc, b, scratch_pool));
          if (performed_edit)
            knows_something = TRUE;
        }
      else if (strcmp(answer, "m") == 0 || strcmp(answer, ":-M") == 0)
        {
          if (desc->kind != svn_wc_conflict_kind_text)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                          _("Invalid option; can only "
                                            "resolve text conflicts with "
                                            "the internal merge tool."
                                            "\n\n")));
              continue;
            }

          if (desc->base_abspath && desc->their_abspath &&
              desc->my_abspath && desc->merged_file)
            {
              svn_boolean_t remains_in_conflict;

              SVN_ERR(svn_cl__merge_file(desc->base_abspath,
                                         desc->their_abspath,
                                         desc->my_abspath,
                                         desc->merged_file,
                                         desc->local_abspath,
                                         b->path_prefix,
                                         b->editor_cmd,
                                         b->config,
                                         &remains_in_conflict,
                                         scratch_pool));
              knows_something = !remains_in_conflict;
            }
          else
            SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                        _("Invalid option.\n\n")));
        }
      else if (strcmp(answer, "l") == 0 || strcmp(answer, ":-l") == 0)
        {
          if (desc->base_abspath && desc->their_abspath &&
              desc->my_abspath && desc->merged_file)
            {
              SVN_ERR(launch_resolver(&performed_edit, desc, b, scratch_pool));
              if (performed_edit)
                knows_something = TRUE;
            }
          else
            SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                        _("Invalid option.\n\n")));
        }
      else if (strcmp(answer, "r") == 0)
        {
          /* We only allow the user accept the merged version of
             the file if they've edited it, or at least looked at
             the diff. */
          if (knows_something)
            {
              result->choice = svn_wc_conflict_choose_merged;
              break;
            }
          else
            SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                        _("Invalid option.\n\n")));
        }
    }

  return SVN_NO_ERROR;
}

/* Ask the user what to do about the property conflict described by DESC.
 * Return the answer in RESULT. B is the conflict baton for this
 * conflict resolution session.
 * SCRATCH_POOL is used for temporary allocations. */
static svn_error_t *
handle_prop_conflict(svn_wc_conflict_result_t *result,
                     const svn_wc_conflict_description2_t *desc,
                     svn_cl__interactive_conflict_baton_t *b,
                     apr_pool_t *scratch_pool)
{
  const char *answer;
  const char *prompt;
  svn_stringbuf_t *prop_reject;
  apr_pool_t *iterpool;

  SVN_ERR_ASSERT(desc->kind == svn_wc_conflict_kind_property);

  SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                              _("Conflict for property '%s' discovered"
                                " on '%s'.\n"),
                              desc->property_name,
                              svn_cl__local_style_skip_ancestor(
                                b->path_prefix, desc->local_abspath,
                                scratch_pool)));

  /* ### Currently, the only useful information in a prop conflict
   * ### description is the .prej file path, which, possibly due to
   * ### deceitful interference from outer space, is stored in the
   * ### 'their_abspath' field of the description.
   * ### This needs to be fixed so we can present better options here. */
  if (desc->their_abspath)
    {
      /* ### The library dumps an svn_string_t into a temp file, and
       * ### we read it back from the file into an svn_stringbuf_t here.
       * ### That's rather silly. We should be passed svn_string_t's
       * ### containing the old/mine/theirs values instead. */
      SVN_ERR(svn_stringbuf_from_file2(&prop_reject,
                                       desc->their_abspath,
                                       scratch_pool));
      /* Print reject file contents. */
      SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                  "%s\n", prop_reject->data));
    }
  else
    {
      /* Nothing much we can do without a prej file... */
      result->choice = svn_wc_conflict_choose_postpone;
      return SVN_NO_ERROR;
    }

  iterpool = svn_pool_create(scratch_pool);
  while (TRUE)
    {
      svn_pool_clear(iterpool);

      prompt = _("Select: (p) postpone, (mf) mine-full, (tf) theirs-full: ");

      SVN_ERR(svn_cmdline_prompt_user2(&answer, prompt, b->pb, iterpool));

      if (strcmp(answer, "p") == 0 || strcmp(answer, ":-P") == 0)
        {
          /* Do nothing, let property be marked conflicted. */
          result->choice = svn_wc_conflict_choose_postpone;
          break;
        }
      else if (strcmp(answer, "mf") == 0 || strcmp(answer, ":-)") == 0)
        {
          result->choice = svn_wc_conflict_choose_mine_full;
          break;
        }
      else if (strcmp(answer, "tf") == 0 || strcmp(answer, ":-(") == 0)
        {
          result->choice = svn_wc_conflict_choose_theirs_full;
          break;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Implement svn_wc_conflict_resolver_func2_t; resolves based on
   --accept option if given, else by prompting. */
svn_error_t *
svn_cl__conflict_func_interactive(svn_wc_conflict_result_t **result,
                                  const svn_wc_conflict_description2_t *desc,
                                  void *baton,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_cl__interactive_conflict_baton_t *b = baton;
  svn_error_t *err;
  apr_pool_t *subpool;

  /* Start out assuming we're going to postpone the conflict. */
  *result = svn_wc_create_conflict_result(svn_wc_conflict_choose_postpone,
                                          NULL, result_pool);

  switch (b->accept_which)
    {
    case svn_cl__accept_invalid:
    case svn_cl__accept_unspecified:
      /* No (or no valid) --accept option, fall through to prompting. */
      break;
    case svn_cl__accept_postpone:
      (*result)->choice = svn_wc_conflict_choose_postpone;
      return SVN_NO_ERROR;
    case svn_cl__accept_base:
      (*result)->choice = svn_wc_conflict_choose_base;
      return SVN_NO_ERROR;
    case svn_cl__accept_working:
      (*result)->choice = svn_wc_conflict_choose_merged;
      return SVN_NO_ERROR;
    case svn_cl__accept_mine_conflict:
      (*result)->choice = svn_wc_conflict_choose_mine_conflict;
      return SVN_NO_ERROR;
    case svn_cl__accept_theirs_conflict:
      (*result)->choice = svn_wc_conflict_choose_theirs_conflict;
      return SVN_NO_ERROR;
    case svn_cl__accept_mine_full:
      (*result)->choice = svn_wc_conflict_choose_mine_full;
      return SVN_NO_ERROR;
    case svn_cl__accept_theirs_full:
      (*result)->choice = svn_wc_conflict_choose_theirs_full;
      return SVN_NO_ERROR;
    case svn_cl__accept_edit:
      if (desc->merged_file)
        {
          if (b->external_failed)
            {
              (*result)->choice = svn_wc_conflict_choose_postpone;
              return SVN_NO_ERROR;
            }

          err = svn_cl__edit_file_externally(desc->merged_file,
                                             b->editor_cmd, b->config,
                                             scratch_pool);
          if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR))
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s\n",
                                          err->message ? err->message :
                                          _("No editor found;"
                                            " leaving all conflicts.")));
              svn_error_clear(err);
              b->external_failed = TRUE;
            }
          else if (err && (err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s\n",
                                          err->message ? err->message :
                                          _("Error running editor;"
                                            " leaving all conflicts.")));
              svn_error_clear(err);
              b->external_failed = TRUE;
            }
          else if (err)
            return svn_error_trace(err);
          (*result)->choice = svn_wc_conflict_choose_merged;
          return SVN_NO_ERROR;
        }
      /* else, fall through to prompting. */
      break;
    case svn_cl__accept_launch:
      if (desc->base_abspath && desc->their_abspath
          && desc->my_abspath && desc->merged_file)
        {
          svn_boolean_t remains_in_conflict;

          if (b->external_failed)
            {
              (*result)->choice = svn_wc_conflict_choose_postpone;
              return SVN_NO_ERROR;
            }

          err = svn_cl__merge_file_externally(desc->base_abspath,
                                              desc->their_abspath,
                                              desc->my_abspath,
                                              desc->merged_file,
                                              desc->local_abspath,
                                              b->config,
                                              &remains_in_conflict,
                                              scratch_pool);
          if (err && err->apr_err == SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s\n",
                                          err->message ? err->message :
                                          _("No merge tool found;"
                                            " leaving all conflicts.")));
              b->external_failed = TRUE;
              return svn_error_trace(err);
            }
          else if (err && err->apr_err == SVN_ERR_EXTERNAL_PROGRAM)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s\n",
                                          err->message ? err->message :
                                          _("Error running merge tool;"
                                            " leaving all conflicts.")));
              b->external_failed = TRUE;
              return svn_error_trace(err);
            }
          else if (err)
            return svn_error_trace(err);

          if (remains_in_conflict)
            (*result)->choice = svn_wc_conflict_choose_postpone;
          else
            (*result)->choice = svn_wc_conflict_choose_merged;
          return SVN_NO_ERROR;
        }
      /* else, fall through to prompting. */
      break;
    }

  /* We're in interactive mode and either the user gave no --accept
     option or the option did not apply; let's prompt. */
  subpool = svn_pool_create(scratch_pool);

  /* Handle the most common cases, which is either:

     Conflicting edits on a file's text, or
     Conflicting edits on a property.
  */
  if (((desc->node_kind == svn_node_file)
       && (desc->action == svn_wc_conflict_action_edit)
       && (desc->reason == svn_wc_conflict_reason_edited)))
    SVN_ERR(handle_text_conflict(*result, desc, b, subpool));
  else if (desc->kind == svn_wc_conflict_kind_property)
    SVN_ERR(handle_prop_conflict(*result, desc, b, subpool));

  /*
    Dealing with obstruction of additions can be tricky.  The
    obstructing item could be unversioned, versioned, or even
    schedule-add.  Here's a matrix of how the caller should behave,
    based on results we return.

                         Unversioned       Versioned       Schedule-Add

      choose_mine       skip addition,    skip addition     skip addition
                        add existing item

      choose_theirs     destroy file,    schedule-delete,   revert add,
                        add new item.    add new item.      rm file,
                                                            add new item

      postpone               [              bail out                 ]

   */
  else if ((desc->action == svn_wc_conflict_action_add)
           && (desc->reason == svn_wc_conflict_reason_obstructed))
    {
      const char *answer;
      const char *prompt;

      SVN_ERR(svn_cmdline_fprintf(
                   stderr, subpool,
                   _("Conflict discovered when trying to add '%s'.\n"
                     "An object of the same name already exists.\n"),
                   svn_cl__local_style_skip_ancestor(b->path_prefix,
                                                     desc->local_abspath,
                                                     subpool)));
      prompt = _("Select: (p) postpone, (mf) mine-full, "
                 "(tf) theirs-full, (h) help:");

      while (1)
        {
          svn_pool_clear(subpool);

          SVN_ERR(svn_cmdline_prompt_user2(&answer, prompt, b->pb, subpool));

          if (strcmp(answer, "h") == 0 || strcmp(answer, "?") == 0)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
              _("  (p)  postpone    - resolve the conflict later\n"
                "  (mf) mine-full   - accept pre-existing item "
                "(ignore upstream addition)\n"
                "  (tf) theirs-full - accept incoming item "
                "(overwrite pre-existing item)\n"
                "  (h)  help        - show this help\n\n")));
            }
          if (strcmp(answer, "p") == 0 || strcmp(answer, ":-P") == 0)
            {
              (*result)->choice = svn_wc_conflict_choose_postpone;
              break;
            }
          if (strcmp(answer, "mf") == 0 || strcmp(answer, ":-)") == 0)
            {
              (*result)->choice = svn_wc_conflict_choose_mine_full;
              break;
            }
          if (strcmp(answer, "tf") == 0 || strcmp(answer, ":-(") == 0)
            {
              (*result)->choice = svn_wc_conflict_choose_theirs_full;
              break;
            }
        }
    }

  else if (desc->kind == svn_wc_conflict_kind_tree)
    {
      const char *answer;
      const char *prompt;
      const char *readable_desc;

      SVN_ERR(svn_cl__get_human_readable_tree_conflict_description(
               &readable_desc, desc, scratch_pool));
      SVN_ERR(svn_cmdline_fprintf(
                   stderr, subpool,
                   _("Tree conflict on '%s'\n   > %s\n"),
                   svn_cl__local_style_skip_ancestor(b->path_prefix,
                                                     desc->local_abspath,
                                                     scratch_pool),
                   readable_desc));

      prompt = _("Select: (p) postpone, (r) mark-resolved, (h) help: ");

      while (1)
        {
          svn_pool_clear(subpool);

          SVN_ERR(svn_cmdline_prompt_user2(&answer, prompt, b->pb, subpool));

          if (strcmp(answer, "h") == 0 || strcmp(answer, "?") == 0)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
              _("  (p) postpone      - resolve the conflict later\n"
                "  (r) resolved      - accept current working tree\n")));
            }
          if (strcmp(answer, "p") == 0 || strcmp(answer, ":-p") == 0)
            {
              (*result)->choice = svn_wc_conflict_choose_postpone;
              break;
            }
          else if (strcmp(answer, "r") == 0)
            {
              (*result)->choice = svn_wc_conflict_choose_merged;
              break;
            }
        }
    }

  else /* other types of conflicts -- do nothing about them. */
    {
      (*result)->choice = svn_wc_conflict_choose_postpone;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Implement svn_wc_conflict_resolver_func2_t; postpones all conflicts
 * and remembers conflicted paths in BATON. */
svn_error_t *
svn_cl__conflict_func_postpone(svn_wc_conflict_result_t **result,
                               const svn_wc_conflict_description2_t *desc,
                               void *baton,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  apr_hash_t *conflicted_paths = baton;
  
  apr_hash_set(conflicted_paths,
               apr_pstrdup(apr_hash_pool_get(conflicted_paths),
                           desc->local_abspath),
               APR_HASH_KEY_STRING, "");

  *result = svn_wc_create_conflict_result(svn_wc_conflict_choose_postpone,
                                          NULL, result_pool);
  return SVN_NO_ERROR;
}

void *
svn_cl__get_conflict_func_postpone_baton(apr_pool_t *result_pool)
{
  return apr_hash_make(result_pool);
}

static apr_array_header_t *
get_postponed_conflicted_paths(void *baton, apr_pool_t *result_pool)
{
  apr_hash_t *conflicted_paths = baton;
  apr_array_header_t *sorted_array;
  apr_array_header_t *result_array;
  int i;

  if (apr_hash_count(conflicted_paths) == 0)
    return NULL;

  sorted_array = svn_sort__hash(conflicted_paths,
                                svn_sort_compare_items_as_paths,
                                apr_hash_pool_get(conflicted_paths));
  result_array = apr_array_make(result_pool, sorted_array->nelts,
                                sizeof(const char *));
  for (i = 0; i < sorted_array->nelts; i++)
    {
      svn_sort__item_t item;
      
      item = APR_ARRAY_IDX(sorted_array, i, svn_sort__item_t);
      APR_ARRAY_PUSH(result_array, const char *) = apr_pstrdup(result_pool,
                                                               item.key);
    }

  return result_array;
}

svn_error_t *
svn_cl__resolve_postponed_conflicts(void *baton,
                                    svn_depth_t depth,
                                    svn_cl__accept_t accept_which,
                                    const char *editor_cmd,
                                    svn_client_ctx_t *ctx,
                                    apr_pool_t *scratch_pool)
{
  apr_array_header_t *targets;
  int i;
  apr_pool_t *iterpool;

  targets = get_postponed_conflicted_paths(baton, scratch_pool);
  if (targets == NULL)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      svn_error_t *err = SVN_NO_ERROR;
      const char *local_abspath;
      svn_wc_conflict_resolver_func2_t conflict_func2;
      void *conflict_baton2;
      svn_cl__interactive_conflict_baton_t *b;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_dirent_get_absolute(&local_abspath, target, iterpool));

      /* Store old state */
      conflict_func2 = ctx->conflict_func2;
      conflict_baton2 = ctx->conflict_baton2;

      /* Set up the interactive resolver. */
      ctx->conflict_func2 = svn_cl__conflict_func_interactive;
      SVN_ERR(svn_cl__get_conflict_func_interactive_baton(&b, accept_which,
                                                          ctx->config,
                                                          editor_cmd,
                                                          ctx->cancel_func,
                                                          ctx->cancel_baton,
                                                          scratch_pool));
      ctx->conflict_baton2 = b;

      err = svn_client_resolve(local_abspath, depth,
                               svn_wc_conflict_choose_unspecified,
                               ctx, iterpool);

      /* Restore state */
      ctx->conflict_func2 = conflict_func2;
      ctx->conflict_baton2 = conflict_baton2;

      if (err)
        {
          if ((err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
              && (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND))
            return svn_error_trace(err);

          svn_error_clear(err);
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
