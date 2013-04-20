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

#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_hash.h"
#include "svn_cmdline.h"
#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "svn_utf.h"

#include "cl.h"
#include "cl-conflicts.h"

#include "private/svn_cmdline_private.h"

#include "svn_private_config.h"

#define ARRAY_LEN(ary) ((sizeof (ary)) / (sizeof ((ary)[0])))



struct svn_cl__interactive_conflict_baton_t {
  svn_cl__accept_t accept_which;
  apr_hash_t *config;
  const char *editor_cmd;
  svn_boolean_t external_failed;
  svn_cmdline_prompt_baton_t *pb;
  const char *path_prefix;
  svn_boolean_t quit;
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
  (*b)->quit = FALSE;

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


/* Print on stdout a diff that shows incoming conflicting changes
 * corresponding to the conflict described in DESC. */
static svn_error_t *
show_diff(const svn_wc_conflict_description2_t *desc,
          apr_pool_t *pool)
{
  const char *path1, *path2;
  svn_diff_t *diff;
  svn_stream_t *output;
  svn_diff_file_options_t *options;

  if (desc->merged_file)
    {
      /* For conflicts recorded by the 'merge' operation, show a diff between
       * 'mine' (the working version of the file as it appeared before the
       * 'merge' operation was run) and 'merged' (the version of the file
       * as it appears after the merge operation).
       *
       * For conflicts recorded by the 'update' and 'switch' operations,
       * show a diff beween 'theirs' (the new pristine version of the
       * file) and 'merged' (the version of the file as it appears with
       * local changes merged with the new pristine version).
       *
       * This way, the diff is always minimal and clearly identifies changes
       * brought into the working copy by the update/switch/merge operation. */
      if (desc->operation == svn_wc_operation_merge)
        path1 = desc->my_abspath;
      else
        path1 = desc->their_abspath;
      path2 = desc->merged_file;
    }
  else
    {
      /* There's no merged file, but we can show the
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
      err = svn_cmdline__edit_file_externally(desc->merged_file, b->editor_cmd,
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


/* Maximum line length for the prompt string. */
#define MAX_PROMPT_WIDTH 70

/* Description of a resolver option */
typedef struct resolver_option_t
{
  const char *code;        /* one or two characters */
  const char *short_desc;  /* label in prompt (localized) */
  const char *long_desc;   /* longer description (localized) */
  svn_wc_conflict_choice_t choice;  /* or -1 if not a simple choice */
} resolver_option_t;

/* Resolver options for a text conflict */
/* (opt->code == "" causes a blank line break in help_string()) */
static const resolver_option_t text_conflict_options[] =
{
  /* Translators: keep long_desc below 70 characters (wrap with a left
     margin of 9 spaces if needed); don't translate the words within square
     brackets. */
  { "e",  N_("edit file"),        N_("change merged file in an editor"
                                     "  [edit]"),
                                  -1 },
  { "df", N_("show diff"),        N_("show all changes made to merged file"),
                                  -1 },
  { "r",  N_("resolved"),         N_("accept merged version of file"),
                                  svn_wc_conflict_choose_merged },
  { "",   "",                     "", svn_wc_conflict_choose_unspecified },
  { "dc", N_("display conflict"), N_("show all conflicts "
                                     "(ignoring merged version)"), -1 },
  { "mc", N_("my side of conflict"), N_("accept my version for all conflicts "
                                        "(same)  [mine-conflict]"),
                                  svn_wc_conflict_choose_mine_conflict },
  { "tc", N_("their side of conflict"), N_("accept their version for all "
                                           "conflicts (same)"
                                           "  [theirs-conflict]"),
                                  svn_wc_conflict_choose_theirs_conflict },
  { "",   "",                     "", svn_wc_conflict_choose_unspecified },
  { "mf", N_("my version"),       N_("accept my version of entire file (even "
                                     "non-conflicts)  [mine-full]"),
                                  svn_wc_conflict_choose_mine_full },
  { "tf", N_("their version"),    N_("accept their version of entire file "
                                     "(same)  [theirs-full]"),
                                  svn_wc_conflict_choose_theirs_full },
  { "",   "",                     "", svn_wc_conflict_choose_unspecified },
  { "p",  N_("postpone"),         N_("mark the conflict to be resolved later"
                                     "  [postpone]"),
                                  svn_wc_conflict_choose_postpone },
  { "m",  N_("merge"),            N_("use internal merge tool to resolve "
                                     "conflict"), -1 },
  { "l",  N_("launch tool"),      N_("launch external tool to resolve "
                                     "conflict  [launch]"), -1 },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_wc_conflict_choose_postpone },
  { "s",  N_("show all options"), N_("show this list (also 'h', '?')"), -1 },
  { NULL }
};

/* Resolver options for a property conflict */
static const resolver_option_t prop_conflict_options[] =
{
  { "p",  N_("postpone"),         N_("mark the conflict to be resolved later"
                                     "  [postpone]"),
                                  svn_wc_conflict_choose_postpone },
  { "mf", N_("my version"),       N_("accept my version of entire property (even "
                                     "non-conflicts)  [mine-full]"),
                                  svn_wc_conflict_choose_mine_full },
  { "tf", N_("their version"),    N_("accept their version of entire property "
                                     "(same)  [theirs-full]"),
                                  svn_wc_conflict_choose_theirs_full },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_wc_conflict_choose_postpone },
  { "h",  N_("help"),             N_("show this help (also '?')"), -1 },
  { NULL }
};

/* Resolver options for an obstructued addition */
static const resolver_option_t obstructed_add_options[] =
{
  { "p",  N_("postpone"),         N_("mark the conflict to be resolved later"
                                     "  [postpone]"),
                                  svn_wc_conflict_choose_postpone },
  { "mf", N_("my version"),       N_("accept pre-existing item (ignore "
                                     "upstream addition)  [mine-full]"),
                                  svn_wc_conflict_choose_mine_full },
  { "tf", N_("their version"),    N_("accept incoming item (overwrite "
                                     "pre-existing item)  [theirs-full]"),
                                  svn_wc_conflict_choose_theirs_full },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_wc_conflict_choose_postpone },
  { "h",  N_("help"),             N_("show this help (also '?')"), -1 },
  { NULL }
};

/* Resolver options for a tree conflict */
static const resolver_option_t tree_conflict_options[] =
{
  { "p",  N_("postpone"),         N_("resolve the conflict later  [postpone]"),
                                  svn_wc_conflict_choose_postpone },
  { "r",  N_("resolved"),         N_("accept current working copy state"),
                                  svn_wc_conflict_choose_merged },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_wc_conflict_choose_postpone },
  { "h",  N_("help"),             N_("show this help (also '?')"), -1 },
  { NULL }
};

static const resolver_option_t tree_conflict_options_update_moved_away[] =
{
  { "p",  N_("postpone"),         N_("resolve the conflict later  [postpone]"),
                                  svn_wc_conflict_choose_postpone },
  { "mc", N_("my side of conflict"), N_("apply update to the move destination"
                                        "  [mine-conflict]"),
                                  svn_wc_conflict_choose_mine_conflict },
  { "r",  N_("resolved"),         N_("mark resolved "
                                     "(the move will become a copy)"),
                                  svn_wc_conflict_choose_merged },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_wc_conflict_choose_postpone },
  { "h",  N_("help"),             N_("show this help (also '?')"), -1 },
  { NULL }
};

static const resolver_option_t tree_conflict_options_update_deleted[] =
{
  { "p",  N_("postpone"),         N_("resolve the conflict later  [postpone]"),
                                  svn_wc_conflict_choose_postpone },
  { "mc", N_("my side of conflict"), N_("keep any moves affected "
                                        "by this deletion  [mine-conflict]"),
                                  svn_wc_conflict_choose_mine_conflict },
  { "r",  N_("resolved"),         N_("mark resolved (any affected moves will "
                                     "become copies)"),
                                  svn_wc_conflict_choose_merged },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_wc_conflict_choose_postpone },
  { "h",  N_("help"),             N_("show this help (also '?')"), -1 },
  { NULL }
};

static const resolver_option_t tree_conflict_options_update_replaced[] =
{
  { "p",  N_("postpone"),         N_("resolve the conflict later  [postpone]"),
                                  svn_wc_conflict_choose_postpone },
  { "mc", N_("my side of conflict"), N_("keep any moves affected by this "
                                        "replacement  [mine-conflict]"),
                                  svn_wc_conflict_choose_mine_conflict },
  { "r",  N_("resolved"),         N_("mark resolved (any affected moves will "
                                     "become copies)"),
                                  svn_wc_conflict_choose_merged },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_wc_conflict_choose_postpone },
  { "h",  N_("help"),             N_("show this help (also '?')"), -1 },
  { NULL }
};


/* Return a pointer to the option description in OPTIONS matching the
 * one- or two-character OPTION_CODE.  Return NULL if not found. */
static const resolver_option_t *
find_option(const resolver_option_t *options,
            const char *option_code)
{
  const resolver_option_t *opt;

  for (opt = options; opt->code; opt++)
    {
      /* Ignore code "" (blank lines) which is not a valid answer. */
      if (opt->code[0] && strcmp(opt->code, option_code) == 0)
        return opt;
    }
  return NULL;
}

/* Return a prompt string listing the options OPTIONS. If OPTION_CODES is
 * non-null, select only the options whose codes are mentioned in it. */
static const char *
prompt_string(const resolver_option_t *options,
              const char *const *option_codes,
              apr_pool_t *pool)
{
  const char *result = _("Select:");
  int left_margin = svn_utf_cstring_utf8_width(result);
  const char *line_sep = apr_psprintf(pool, "\n%*s", left_margin, "");
  int this_line_len = left_margin;
  svn_boolean_t first = TRUE;

  while (1)
    {
      const resolver_option_t *opt;
      const char *s;
      int slen;

      if (option_codes)
        {
          if (! *option_codes)
            break;
          opt = find_option(options, *option_codes++);
        }
      else
        {
          opt = options++;
          if (! opt->code)
            break;
        }

      if (! first)
        result = apr_pstrcat(pool, result, ",", (char *)NULL);
      s = apr_psprintf(pool, _(" (%s) %s"),
                       opt->code, _(opt->short_desc));
      slen = svn_utf_cstring_utf8_width(s);
      /* Break the line if adding the next option would make it too long */
      if (this_line_len + slen > MAX_PROMPT_WIDTH)
        {
          result = apr_pstrcat(pool, result, line_sep, (char *)NULL);
          this_line_len = left_margin;
        }
      result = apr_pstrcat(pool, result, s, (char *)NULL);
      this_line_len += slen;
      first = FALSE;
    }
  return apr_pstrcat(pool, result, ": ", (char *)NULL);
}

/* Return a help string listing the OPTIONS. */
static const char *
help_string(const resolver_option_t *options,
            apr_pool_t *pool)
{
  const char *result = "";
  const resolver_option_t *opt;

  for (opt = options; opt->code; opt++)
    {
      /* Append a line describing OPT, or a blank line if its code is "". */
      if (opt->code[0])
        {
          const char *s = apr_psprintf(pool, "  (%s)", opt->code);

          result = apr_psprintf(pool, "%s%-6s - %s\n",
                                result, s, _(opt->long_desc));
        }
      else
        {
          result = apr_pstrcat(pool, result, "\n", (char *)NULL);
        }
    }
  result = apr_pstrcat(pool, result,
                       _("Words in square brackets are the corresponding "
                         "--accept option arguments.\n"),
                       (char *)NULL);
  return result;
}

/* Prompt the user with CONFLICT_OPTIONS, restricted to the options listed
 * in OPTIONS_TO_SHOW if that is non-null.  Set *OPT to point to the chosen
 * one of CONFLICT_OPTIONS (not necessarily one of OPTIONS_TO_SHOW), or to
 * NULL if the answer was not one of them.
 *
 * If the answer is the (globally recognized) 'help' option, then display
 * the help (on stderr) and return with *OPT == NULL.
 */
static svn_error_t *
prompt_user(const resolver_option_t **opt,
            const resolver_option_t *conflict_options,
            const char *const *options_to_show,
            void *prompt_baton,
            apr_pool_t *scratch_pool)
{
  const char *prompt
    = prompt_string(conflict_options, options_to_show, scratch_pool);
  const char *answer;

  SVN_ERR(svn_cmdline_prompt_user2(&answer, prompt, prompt_baton, scratch_pool));
  if (strcmp(answer, "h") == 0 || strcmp(answer, "?") == 0)
    {
      SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "\n%s\n",
                                  help_string(conflict_options,
                                              scratch_pool)));
      *opt = NULL;
    }
  else
    {
      *opt = find_option(conflict_options, answer);
      if (! *opt)
        {
          SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                      _("Unrecognized option.\n\n")));
        }
    }
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
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
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
      const char *options[ARRAY_LEN(text_conflict_options)];
      const char **next_option = options;
      const resolver_option_t *opt;

      svn_pool_clear(iterpool);

      *next_option++ = "p";
      if (diff_allowed)
        {
          *next_option++ = "df";
          *next_option++ = "e";
          *next_option++ = "m";

          if (knows_something)
            *next_option++ = "r";

          if (! desc->is_binary)
            {
              *next_option++ = "mc";
              *next_option++ = "tc";
            }
        }
      else
        {
          if (knows_something)
            *next_option++ = "r";
          *next_option++ = "mf";
          *next_option++ = "tf";
        }
      *next_option++ = "s";
      *next_option++ = NULL;

      SVN_ERR(prompt_user(&opt, text_conflict_options, options, b->pb,
                          iterpool));
      if (! opt)
        continue;

      if (strcmp(opt->code, "q") == 0)
        {
          result->choice = opt->choice;
          b->accept_which = svn_cl__accept_postpone;
          b->quit = TRUE;
          break;
        }
      else if (strcmp(opt->code, "s") == 0)
        {
          SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "\n%s\n",
                                      help_string(text_conflict_options,
                                                  iterpool)));
        }
      else if (strcmp(opt->code, "dc") == 0)
        {
          if (desc->is_binary)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                          _("Invalid option; cannot "
                                            "display conflicts for a "
                                            "binary file.\n\n")));
              continue;
            }
          else if (! (desc->my_abspath && desc->base_abspath &&
                      desc->their_abspath))
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                          _("Invalid option; original "
                                            "files not available.\n\n")));
              continue;
            }
          SVN_ERR(show_conflicts(desc, iterpool));
          knows_something = TRUE;
        }
      else if (strcmp(opt->code, "df") == 0)
        {
          if (! diff_allowed)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                             _("Invalid option; there's no "
                                "merged version to diff.\n\n")));
              continue;
            }

          SVN_ERR(show_diff(desc, iterpool));
          knows_something = TRUE;
        }
      else if (strcmp(opt->code, "e") == 0 || strcmp(opt->code, ":-E") == 0)
        {
          SVN_ERR(open_editor(&performed_edit, desc, b, iterpool));
          if (performed_edit)
            knows_something = TRUE;
        }
      else if (strcmp(opt->code, "m") == 0 || strcmp(opt->code, ":-g") == 0 ||
               strcmp(opt->code, "=>-") == 0 || strcmp(opt->code, ":>.") == 0)
        {
          if (desc->kind != svn_wc_conflict_kind_text)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
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
                                         iterpool));
              knows_something = !remains_in_conflict;
            }
          else
            SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                        _("Invalid option.\n\n")));
        }
      else if (strcmp(opt->code, "l") == 0 || strcmp(opt->code, ":-l") == 0)
        {
          /* ### This check should be earlier as it's nasty to offer an option
           *     and then when the user chooses it say 'Invalid option'. */
          /* ### 'merged_file' shouldn't be necessary *before* we launch the
           *     resolver: it should be the *result* of doing so. */
          if (desc->base_abspath && desc->their_abspath &&
              desc->my_abspath && desc->merged_file)
            {
              SVN_ERR(launch_resolver(&performed_edit, desc, b, iterpool));
              if (performed_edit)
                knows_something = TRUE;
            }
          else
            SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                        _("Invalid option.\n\n")));
        }
      else if (opt->choice != -1)
        {
          if ((opt->choice == svn_wc_conflict_choose_mine_conflict
               || opt->choice == svn_wc_conflict_choose_theirs_conflict)
              && desc->is_binary)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                          _("Invalid option; cannot choose "
                                            "based on conflicts in a "
                                            "binary file.\n\n")));
              continue;
            }

          /* We only allow the user accept the merged version of
             the file if they've edited it, or at least looked at
             the diff. */
          if (result->choice == svn_wc_conflict_choose_merged
              && ! knows_something)
            {
              SVN_ERR(svn_cmdline_fprintf(
                        stderr, iterpool,
                        _("Invalid option; use diff/edit/merge/launch "
                          "before choosing 'resolved'.\n\n")));
              continue;
            }

          result->choice = opt->choice;
          if (performed_edit)
            result->save_merged = TRUE;
          break;
        }
    }
  svn_pool_destroy(iterpool);

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
      svn_stringbuf_t *prop_reject;

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
      const resolver_option_t *opt;

      svn_pool_clear(iterpool);

      SVN_ERR(prompt_user(&opt, prop_conflict_options, NULL, b->pb,
                          iterpool));
      if (! opt)
        continue;

      if (strcmp(opt->code, "q") == 0)
        {
          result->choice = opt->choice;
          b->accept_which = svn_cl__accept_postpone;
          b->quit = TRUE;
          break;
        }
      else if (opt->choice != -1)
        {
          result->choice = opt->choice;
          break;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Ask the user what to do about the tree conflict described by DESC.
 * Return the answer in RESULT. B is the conflict baton for this
 * conflict resolution session.
 * SCRATCH_POOL is used for temporary allocations. */
static svn_error_t *
handle_tree_conflict(svn_wc_conflict_result_t *result,
                     const svn_wc_conflict_description2_t *desc,
                     svn_cl__interactive_conflict_baton_t *b,
                     apr_pool_t *scratch_pool)
{
  const char *readable_desc;
  apr_pool_t *iterpool;

  SVN_ERR(svn_cl__get_human_readable_tree_conflict_description(
           &readable_desc, desc, scratch_pool));
  SVN_ERR(svn_cmdline_fprintf(
               stderr, scratch_pool,
               _("Tree conflict on '%s'\n   > %s\n"),
               svn_cl__local_style_skip_ancestor(b->path_prefix,
                                                 desc->local_abspath,
                                                 scratch_pool),
               readable_desc));

  iterpool = svn_pool_create(scratch_pool);
  while (1)
    {
      const resolver_option_t *opt;
      const resolver_option_t *tc_opts;

      svn_pool_clear(iterpool);

      if (desc->operation == svn_wc_operation_update ||
          desc->operation == svn_wc_operation_switch)
        {
          if (desc->reason == svn_wc_conflict_reason_moved_away)
            tc_opts = tree_conflict_options_update_moved_away;
          else if (desc->reason == svn_wc_conflict_reason_deleted)
            tc_opts = tree_conflict_options_update_deleted;
          else if (desc->reason == svn_wc_conflict_reason_replaced)
            tc_opts = tree_conflict_options_update_replaced;
          else
            tc_opts = tree_conflict_options;
        }
      else
        tc_opts = tree_conflict_options;

      SVN_ERR(prompt_user(&opt, tc_opts, NULL, b->pb, iterpool));
      if (! opt)
        continue;

      if (strcmp(opt->code, "q") == 0)
        {
          result->choice = opt->choice;
          b->accept_which = svn_cl__accept_postpone;
          b->quit = TRUE;
          break;
        }
      else if (opt->choice != -1)
        {
          result->choice = opt->choice;
          break;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Ask the user what to do about the obstructed add described by DESC.
 * Return the answer in RESULT. B is the conflict baton for this
 * conflict resolution session.
 * SCRATCH_POOL is used for temporary allocations. */
static svn_error_t *
handle_obstructed_add(svn_wc_conflict_result_t *result,
                      const svn_wc_conflict_description2_t *desc,
                      svn_cl__interactive_conflict_baton_t *b,
                      apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;

  SVN_ERR(svn_cmdline_fprintf(
               stderr, scratch_pool,
               _("Conflict discovered when trying to add '%s'.\n"
                 "An object of the same name already exists.\n"),
               svn_cl__local_style_skip_ancestor(b->path_prefix,
                                                 desc->local_abspath,
                                                 scratch_pool)));

  iterpool = svn_pool_create(scratch_pool);
  while (1)
    {
      const resolver_option_t *opt;

      svn_pool_clear(iterpool);

      SVN_ERR(prompt_user(&opt, obstructed_add_options, NULL, b->pb,
                          iterpool));
      if (! opt)
        continue;

      if (strcmp(opt->code, "q") == 0)
        {
          result->choice = opt->choice;
          b->accept_which = svn_cl__accept_postpone;
          b->quit = TRUE;
          break;
        }
      else if (opt->choice != -1)
        {
          result->choice = opt->choice;
          break;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cl__conflict_func_interactive(svn_wc_conflict_result_t **result,
                                  const svn_wc_conflict_description2_t *desc,
                                  void *baton,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_cl__interactive_conflict_baton_t *b = baton;
  svn_error_t *err;

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
      /* If the caller didn't merge the property values, then I guess
       * 'choose working' means 'choose mine'... */
      if (! desc->merged_file)
        (*result)->merged_file = desc->my_abspath;
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

          err = svn_cmdline__edit_file_externally(desc->merged_file,
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

  /* Handle the most common cases, which is either:

     Conflicting edits on a file's text, or
     Conflicting edits on a property.
  */
  if (((desc->node_kind == svn_node_file)
       && (desc->action == svn_wc_conflict_action_edit)
       && (desc->reason == svn_wc_conflict_reason_edited)))
    SVN_ERR(handle_text_conflict(*result, desc, b, scratch_pool));
  else if (desc->kind == svn_wc_conflict_kind_property)
    SVN_ERR(handle_prop_conflict(*result, desc, b, scratch_pool));

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
    SVN_ERR(handle_obstructed_add(*result, desc, b, scratch_pool));

  else if (desc->kind == svn_wc_conflict_kind_tree)
    SVN_ERR(handle_tree_conflict(*result, desc, b, scratch_pool));

  else /* other types of conflicts -- do nothing about them. */
    {
      (*result)->choice = svn_wc_conflict_choose_postpone;
    }

  return SVN_NO_ERROR;
}
