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
#include "private/svn_wc_private.h"

#include "svn_private_config.h"

#define ARRAY_LEN(ary) ((sizeof (ary)) / (sizeof ((ary)[0])))
#define MAX_ARRAY_LEN(aryx, aryz)               \
  (ARRAY_LEN((aryx)) > ARRAY_LEN((aryz))        \
   ? ARRAY_LEN((aryx)) : ARRAY_LEN((aryz)))



struct svn_cl__interactive_conflict_baton_t {
  svn_cl__accept_t accept_which;
  apr_hash_t *config;
  const char *editor_cmd;
  svn_boolean_t external_failed;
  svn_cmdline_prompt_baton_t *pb;
  const char *path_prefix;
  svn_boolean_t quit;
  svn_cl__conflict_stats_t *conflict_stats;
  svn_boolean_t printed_summary;
};

svn_error_t *
svn_cl__get_conflict_func_interactive_baton(
  svn_cl__interactive_conflict_baton_t **b,
  svn_cl__accept_t accept_which,
  apr_hash_t *config,
  const char *editor_cmd,
  svn_cl__conflict_stats_t *conflict_stats,
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
  (*b)->conflict_stats = conflict_stats;
  (*b)->printed_summary = FALSE;

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
 * corresponding to the conflict described in CONFLICT. */
static svn_error_t *
show_diff(const svn_client_conflict_t *conflict,
          const char *merged_abspath,
          const char *path_prefix,
          svn_cancel_func_t cancel_func,
          void *cancel_baton,
          apr_pool_t *pool)
{
  const char *path1, *path2;
  const char *label1, *label2;
  svn_diff_t *diff;
  svn_stream_t *output;
  svn_diff_file_options_t *options;
  const char *my_abspath;
  const char *their_abspath;

  SVN_ERR(svn_client_conflict_text_get_contents(NULL, &my_abspath, NULL,
                                                &their_abspath,
                                                conflict, pool, pool));
  if (merged_abspath)
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
      if (svn_client_conflict_get_operation(conflict) == svn_wc_operation_merge)
        {
          path1 = my_abspath;
          label1 = _("MINE");
        }
      else
        {
          path1 = their_abspath;
          label1 = _("THEIRS");
        }
      path2 = merged_abspath;
      label2 = _("MERGED");
    }
  else
    {
      /* There's no merged file, but we can show the
         difference between mine and theirs. */
      path1 = their_abspath;
      label1 = _("THEIRS");
      path2 = my_abspath;
      label2 = _("MINE");
    }

  label1 = apr_psprintf(pool, "%s\t- %s",
                        svn_cl__local_style_skip_ancestor(
                          path_prefix, path1, pool), label1);
  label2 = apr_psprintf(pool, "%s\t- %s",
                        svn_cl__local_style_skip_ancestor(
                          path_prefix, path2, pool), label2);

  options = svn_diff_file_options_create(pool);
  options->ignore_eol_style = TRUE;
  SVN_ERR(svn_stream_for_stdout(&output, pool));
  SVN_ERR(svn_diff_file_diff_2(&diff, path1, path2,
                               options, pool));
  return svn_diff_file_output_unified4(output, diff,
                                       path1, path2,
                                       label1, label2,
                                       APR_LOCALE_CHARSET,
                                       NULL,
                                       options->show_c_function,
                                       options->context_size,
                                       cancel_func, cancel_baton,
                                       pool);
}


/* Print on stdout just the conflict hunks of a diff among the 'base', 'their'
 * and 'my' files of CONFLICT. */
static svn_error_t *
show_conflicts(const svn_client_conflict_t *conflict,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *pool)
{
  svn_diff_t *diff;
  svn_stream_t *output;
  svn_diff_file_options_t *options;
  const char *base_abspath;
  const char *my_abspath;
  const char *their_abspath;

  SVN_ERR(svn_client_conflict_text_get_contents(NULL, &my_abspath,
                                                &base_abspath, &their_abspath,
                                                conflict, pool, pool));
  options = svn_diff_file_options_create(pool);
  options->ignore_eol_style = TRUE;
  SVN_ERR(svn_stream_for_stdout(&output, pool));
  SVN_ERR(svn_diff_file_diff3_2(&diff, base_abspath, my_abspath, their_abspath,
                                options, pool));
  /* ### Consider putting the markers/labels from
     ### svn_wc__merge_internal in the conflict description. */
  return svn_diff_file_output_merge3(
           output, diff, base_abspath, my_abspath, their_abspath,
           _("||||||| ORIGINAL"),
           _("<<<<<<< MINE (select with 'mc')"),
           _(">>>>>>> THEIRS (select with 'tc')"),
           "=======",
           svn_diff_conflict_display_only_conflicts,
           cancel_func,
           cancel_baton,
           pool);
}

/* Perform a 3-way merge of the conflicting values of a property,
 * and write the result to the OUTPUT stream.
 *
 * If MERGED_PROPVAL is non-NULL, use it as 'my' version instead of
 * MY_ABSPATH.
 *
 * Assume the values are printable UTF-8 text.
 */
static svn_error_t *
merge_prop_conflict(svn_stream_t *output,
                    const svn_string_t *base_propval,
                    const svn_string_t *my_propval,
                    const svn_string_t *their_propval,
                    const svn_string_t *merged_propval,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *pool)
{
  svn_diff_file_options_t *options = svn_diff_file_options_create(pool);
  svn_diff_t *diff;

  /* If any of the property values is missing, use an empty value instead
   * for the purpose of showing a diff. */
  if (base_propval == NULL)
    base_propval = svn_string_create_empty(pool);
  if (my_propval == NULL)
    my_propval = svn_string_create_empty(pool);
  if (my_propval == NULL)
    my_propval = svn_string_create_empty(pool);
    
  options->ignore_eol_style = TRUE;
  SVN_ERR(svn_diff_mem_string_diff3(&diff, base_propval,
                                    merged_propval ?
                                      merged_propval : my_propval,
                                    their_propval, options, pool));
  SVN_ERR(svn_diff_mem_string_output_merge3(
            output, diff, base_propval,
            merged_propval ? merged_propval : my_propval, their_propval,
            _("||||||| ORIGINAL"),
            _("<<<<<<< MINE"),
            _(">>>>>>> THEIRS"),
            "=======",
            svn_diff_conflict_display_modified_original_latest,
            cancel_func,
            cancel_baton,
            pool));

  return SVN_NO_ERROR;
}

/* Display the conflicting values of a property as a 3-way diff.
 *
 * If MERGED_ABSPATH is non-NULL, show it as 'my' version instead of
 * DESC->MY_ABSPATH.
 *
 * Assume the values are printable UTF-8 text.
 */
static svn_error_t *
show_prop_conflict(const svn_string_t *base_propval,
                   const svn_string_t *my_propval,
                   const svn_string_t *their_propval,
                   const svn_string_t *merged_propval,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *pool)
{
  svn_stream_t *output;

  SVN_ERR(svn_stream_for_stdout(&output, pool));
  SVN_ERR(merge_prop_conflict(output, base_propval, my_propval, their_propval,
                              merged_propval, cancel_func, cancel_baton, pool));

  return SVN_NO_ERROR;
}

/* Run an external editor, passing it the MERGED_ABSPATH, or, if the
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
            const char *merged_abspath,
            const char *editor_cmd,
            apr_hash_t *config,
            apr_pool_t *pool)
{
  svn_error_t *err;

  if (merged_abspath)
    {
      err = svn_cmdline__edit_file_externally(merged_abspath, editor_cmd,
                                              config, pool);
      if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR ||
                  err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
        {
          char buf[1024];
          const char *message;

          message = svn_err_best_message(err, buf, sizeof(buf));
          SVN_ERR(svn_cmdline_fprintf(stderr, pool, "%s\n", message));
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

/* Run an external editor on the merged property value with conflict markers.
 * Return the edited result in *MERGED_PROPVAL.
 * If the edit is aborted, set *MERGED_ABSPATH and *MERGED_PROPVAL to NULL.
 * The tool to use is determined by B->editor_cmd, B->config and
 * environment variables; see svn_cl__edit_file_externally() for details. */
static svn_error_t *
edit_prop_conflict(const svn_string_t **merged_propval,
                   const svn_string_t *base_propval,
                   const svn_string_t *my_propval,
                   const svn_string_t *their_propval,
                   const char *editor_cmd,
                   apr_hash_t *config,
                   svn_cmdline_prompt_baton_t *pb,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  apr_file_t *file;
  const char *file_path;
  svn_boolean_t performed_edit = FALSE;
  svn_stream_t *merged_prop;

  SVN_ERR(svn_io_open_unique_file3(&file, &file_path, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   result_pool, scratch_pool));
  merged_prop = svn_stream_from_aprfile2(file, TRUE /* disown */,
                                         scratch_pool);
  SVN_ERR(merge_prop_conflict(merged_prop, base_propval, my_propval,
                              their_propval, NULL,
                              pb->cancel_func,
                              pb->cancel_baton,
                              scratch_pool));
  SVN_ERR(svn_stream_close(merged_prop));
  SVN_ERR(svn_io_file_flush(file, scratch_pool));
  SVN_ERR(open_editor(&performed_edit, file_path, editor_cmd,
                      config, scratch_pool));
  if (performed_edit && merged_propval)
    {
      svn_stringbuf_t *buf;

      SVN_ERR(svn_stringbuf_from_file2(&buf, file_path, scratch_pool));
      *merged_propval = svn_string_create_from_buf(buf, result_pool); 
    }

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
  svn_client_conflict_option_id_t choice;
                           /* or ..._undefined if not a simple choice */
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
                                  svn_client_conflict_option_undefined },
  { "df", N_("show diff"),        N_("show all changes made to merged file"),
                                  svn_client_conflict_option_undefined },
  { "r",  N_("mark resolved"),   N_("accept merged version of file  [working]"),
                                  svn_client_conflict_option_merged_text },
  { "",   "",                     "", svn_client_conflict_option_unspecified },
  { "dc", N_("display conflict"), N_("show all conflicts "
                                     "(ignoring merged version)"),
                                  svn_client_conflict_option_undefined },
  { "mc", N_("my side of conflict"), N_("accept my version for all conflicts "
                                        "(same)  [mine-conflict]"),
                                  svn_client_conflict_option_working_text_where_conflicted },
  { "tc", N_("their side of conflict"), N_("accept their version for all "
                                           "conflicts (same)"
                                           "  [theirs-conflict]"),
                                  svn_client_conflict_option_incoming_text_where_conflicted },
  { "",   "",                     "", svn_client_conflict_option_unspecified },
  { "mf", N_("my version"),       N_("accept my version of entire file (even "
                                     "non-conflicts)  [mine-full]"),
                                  svn_client_conflict_option_working_text },
  { "tf", N_("their version"),    N_("accept their version of entire file "
                                     "(same)  [theirs-full]"),
                                  svn_client_conflict_option_incoming_text },
  { "",   "",                     "", svn_client_conflict_option_unspecified },
  { "m",  N_("merge"),            N_("use merge tool to resolve conflict"),
                                  svn_client_conflict_option_undefined },
  { "l",  N_("launch tool"),      N_("launch external merge tool to resolve "
                                     "conflict  [launch]"),
                                  svn_client_conflict_option_undefined },
  { "i",  N_("internal merge tool"), N_("use built-in merge tool to "
                                     "resolve conflict"),
                                  svn_client_conflict_option_undefined },
  { "p",  N_("postpone"),         N_("mark the conflict to be resolved later"
                                     "  [postpone]"),
                                  svn_client_conflict_option_postpone },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_client_conflict_option_postpone },
  { "s",  N_("show all options"), N_("show this list (also 'h', '?')"),
                                  svn_client_conflict_option_undefined },
  { NULL }
};

/* Resolver options for a binary file conflict. */
static const resolver_option_t binary_conflict_options[] =
{
  /* Translators: keep long_desc below 70 characters (wrap with a left
     margin of 9 spaces if needed); don't translate the words within square
     brackets. */
  { "r",  N_("mark resolved"),   N_("accept the working copy version of file "
                                    " [working]"),
                                  svn_client_conflict_option_merged_text },
  { "tf", N_("their version"),    N_("accept the incoming version of file "
                                     " [theirs-full]"),
                                  svn_client_conflict_option_incoming_text },
  { "p",  N_("postpone"),         N_("mark the conflict to be resolved later "
                                     " [postpone]"),
                                  svn_client_conflict_option_postpone },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_client_conflict_option_postpone },
  { "s",  N_("show all options"), N_("show this list (also 'h', '?')"),
                                  svn_client_conflict_option_undefined },
  { NULL }
};

/* Resolver options for a property conflict */
static const resolver_option_t prop_conflict_options[] =
{
  { "mf", N_("my version"),       N_("accept my version of entire property (even "
                                     "non-conflicts)  [mine-full]"),
                                  svn_client_conflict_option_working_text },
  { "tf", N_("their version"),    N_("accept their version of entire property "
                                     "(same)  [theirs-full]"),
                                  svn_client_conflict_option_incoming_text },
  { "dc", N_("display conflict"), N_("show conflicts in this property"),
                                  svn_client_conflict_option_undefined },
  { "e",  N_("edit property"),    N_("change merged property value in an editor"
                                     "  [edit]"),
                                  svn_client_conflict_option_undefined },
  { "r",  N_("mark resolved"),    N_("accept edited version of property"),
                                  svn_client_conflict_option_merged_text },
  { "p",  N_("postpone"),         N_("mark the conflict to be resolved later"
                                     "  [postpone]"),
                                  svn_client_conflict_option_postpone },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_client_conflict_option_postpone },
  { "h",  N_("help"),             N_("show this help (also '?')"),
                                  svn_client_conflict_option_undefined },
  { NULL }
};

/* Resolver options for a tree conflict */
static const resolver_option_t tree_conflict_options[] =
{
  { "r",  N_("mark resolved"),    N_("accept current working copy state"),
                                  svn_client_conflict_option_merged_text },
  { "p",  N_("postpone"),         N_("resolve the conflict later  [postpone]"),
                                  svn_client_conflict_option_postpone },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_client_conflict_option_postpone },
  { "h",  N_("help"),             N_("show this help (also '?')"),
                                  svn_client_conflict_option_undefined },
  { NULL }
};

static const resolver_option_t tree_conflict_options_update_moved_away[] =
{
  { "mc", N_("apply update to move destination (recommended)"),
                                  N_("apply incoming update to move destination"
                                     "  [mine-conflict]"),
                                  svn_client_conflict_option_working_text_where_conflicted },
  { "p",  N_("postpone"),         N_("resolve the conflict later  [postpone]"),
                                  svn_client_conflict_option_postpone },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_client_conflict_option_postpone },
  { "h",  N_("help"),             N_("show this help (also '?')"),
                                  svn_client_conflict_option_undefined },
  { NULL }
};

static const resolver_option_t tree_conflict_options_update_edit_deleted_dir[] =
{
  { "mc", N_("prepare for updating moved-away children, if any (recommended)"),
                                  N_("allow updating moved-away children "
                                     "with 'svn resolve' [mine-conflict]"),
                                  svn_client_conflict_option_working_text_where_conflicted },
  { "p",  N_("postpone"),         N_("resolve the conflict later  [postpone]"),
                                  svn_client_conflict_option_postpone },
  { "q",  N_("quit resolution"),  N_("postpone all remaining conflicts"),
                                  svn_client_conflict_option_postpone },
  { "h",  N_("help"),             N_("show this help (also '?')"),
                                  svn_client_conflict_option_undefined },
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
        result = apr_pstrcat(pool, result, ",", SVN_VA_NULL);
      s = apr_psprintf(pool, _(" (%s) %s"),
                       opt->code, _(opt->short_desc));
      slen = svn_utf_cstring_utf8_width(s);
      /* Break the line if adding the next option would make it too long */
      if (this_line_len + slen > MAX_PROMPT_WIDTH)
        {
          result = apr_pstrcat(pool, result, line_sep, SVN_VA_NULL);
          this_line_len = left_margin;
        }
      result = apr_pstrcat(pool, result, s, SVN_VA_NULL);
      this_line_len += slen;
      first = FALSE;
    }
  return apr_pstrcat(pool, result, ": ", SVN_VA_NULL);
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
          result = apr_pstrcat(pool, result, "\n", SVN_VA_NULL);
        }
    }
  result = apr_pstrcat(pool, result,
                       _("Words in square brackets are the corresponding "
                         "--accept option arguments.\n"),
                       SVN_VA_NULL);
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

/* Ask the user what to do about the text conflict described by CONFLICT.
 * Return the answer in RESULT. B is the conflict baton for this
 * conflict resolution session.
 * SCRATCH_POOL is used for temporary allocations. */
static svn_error_t *
handle_text_conflict(svn_client_conflict_option_id_t *option_id,
                     svn_boolean_t *save_merged,
                     svn_cl__accept_t *accept_which,
                     svn_boolean_t *quit,
                     const svn_client_conflict_t *conflict,
                     const char *path_prefix,
                     svn_cmdline_prompt_baton_t *pb,
                     const char *editor_cmd,
                     apr_hash_t *config,
                     apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_boolean_t diff_allowed = FALSE;
  /* Have they done something that might have affected the merged
     file (so that we need to save a .edited copy by setting the
     *save_merge flag)? */
  svn_boolean_t performed_edit = FALSE;
  /* Have they done *something* (edit, look at diff, etc) to
     give them a rational basis for choosing (r)esolved? */
  svn_boolean_t knows_something = FALSE;
  const char *local_relpath;
  const char *local_abspath = svn_client_conflict_get_local_abspath(conflict);
  const char *mime_type = svn_client_conflict_text_get_mime_type(conflict);
  svn_boolean_t is_binary = mime_type ? svn_mime_type_is_binary(mime_type)
                                      : FALSE;
  const char *base_abspath;
  const char *my_abspath;
  const char *their_abspath;
  const char *merged_abspath = svn_client_conflict_get_local_abspath(conflict);

  SVN_ERR(svn_client_conflict_text_get_contents(NULL, &my_abspath,
                                                &base_abspath, &their_abspath,
                                                conflict, scratch_pool,
                                                scratch_pool));

  local_relpath = svn_cl__local_style_skip_ancestor(path_prefix,
                                                    local_abspath,
                                                    scratch_pool);

  if (is_binary)
    SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                _("Conflict discovered in binary file '%s'.\n"),
                                local_relpath));
  else
    SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                                _("Conflict discovered in file '%s'.\n"),
                                local_relpath));

  /* ### TODO This whole feature availability check is grossly outdated.
     DIFF_ALLOWED needs either to be redefined or to go away.
   */

  /* Diffing can happen between base and merged, to show conflict
     markers to the user (this is the typical 3-way merge
     scenario), or if no base is available, we can show a diff
     between mine and theirs. */
  if (!is_binary &&
      ((merged_abspath && base_abspath)
      || (!base_abspath && my_abspath && their_abspath)))
    diff_allowed = TRUE;

  while (TRUE)
    {
      const char *options[1 + MAX_ARRAY_LEN(binary_conflict_options,
                                            text_conflict_options)];

      const resolver_option_t *conflict_options = is_binary
                                                    ? binary_conflict_options
                                                    : text_conflict_options;
      const char **next_option = options;
      const resolver_option_t *opt;

      svn_pool_clear(iterpool);

      *next_option++ = "p";
      if (diff_allowed)
        {
          /* We need one more path for this feature. */
          if (my_abspath)
            *next_option++ = "df";

          *next_option++ = "e";

          /* We need one more path for this feature. */
          if (my_abspath)
            *next_option++ = "m";

          if (knows_something)
            *next_option++ = "r";

          *next_option++ = "mc";
          *next_option++ = "tc";
        }
      else
        {
          if (knows_something || is_binary)
            *next_option++ = "r";

          /* The 'mine-full' option selects the ".mine" file so only offer
           * it if that file exists. It does not exist for binary files,
           * for example (questionable historical behaviour since 1.0). */
          if (my_abspath)
            *next_option++ = "mf";

          *next_option++ = "tf";
        }
      *next_option++ = "s";
      *next_option++ = NULL;

      SVN_ERR(prompt_user(&opt, conflict_options, options, pb, iterpool));
      if (! opt)
        continue;

      if (strcmp(opt->code, "q") == 0)
        {
          *option_id = opt->choice;
          *accept_which = svn_cl__accept_postpone;
          *quit = TRUE;
          break;
        }
      else if (strcmp(opt->code, "s") == 0)
        {
          SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "\n%s\n",
                                      help_string(conflict_options,
                                                  iterpool)));
        }
      else if (strcmp(opt->code, "dc") == 0)
        {
          if (is_binary)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                          _("Invalid option; cannot "
                                            "display conflicts for a "
                                            "binary file.\n\n")));
              continue;
            }
          else if (! (my_abspath && base_abspath && their_abspath))
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                          _("Invalid option; original "
                                            "files not available.\n\n")));
              continue;
            }
          SVN_ERR(show_conflicts(conflict,
                                 pb->cancel_func,
                                 pb->cancel_baton,
                                 iterpool));
          knows_something = TRUE;
        }
      else if (strcmp(opt->code, "df") == 0)
        {
          /* Re-check preconditions. */
          if (! diff_allowed || ! my_abspath)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                             _("Invalid option; there's no "
                                "merged version to diff.\n\n")));
              continue;
            }

          SVN_ERR(show_diff(conflict, merged_abspath, path_prefix,
                            pb->cancel_func, pb->cancel_baton,
                            iterpool));
          knows_something = TRUE;
        }
      else if (strcmp(opt->code, "e") == 0 || strcmp(opt->code, ":-E") == 0)
        {
          SVN_ERR(open_editor(&performed_edit, merged_abspath, editor_cmd,
                              config, iterpool));
          if (performed_edit)
            knows_something = TRUE;
        }
      else if (strcmp(opt->code, "m") == 0 || strcmp(opt->code, ":-g") == 0 ||
               strcmp(opt->code, "=>-") == 0 || strcmp(opt->code, ":>.") == 0)
        {
          svn_error_t *err;

          /* Re-check preconditions. */
          if (! my_abspath)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                             _("Invalid option; there's no "
                                "base path to merge.\n\n")));
              continue;
            }

          err = svn_cl__merge_file_externally(base_abspath,
                                              their_abspath,
                                              my_abspath,
                                              merged_abspath,
                                              local_abspath, config,
                                              NULL, iterpool);
          if (err)
            {
              if (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL)
                {
                  svn_boolean_t remains_in_conflict = TRUE;

                  /* Try the internal merge tool. */
                  svn_error_clear(err);
                  SVN_ERR(svn_cl__merge_file(&remains_in_conflict,
                                             base_abspath,
                                             their_abspath,
                                             my_abspath,
                                             merged_abspath,
                                             local_abspath,
                                             path_prefix,
                                             editor_cmd,
                                             config,
                                             pb->cancel_func,
                                             pb->cancel_baton,
                                             iterpool));
                  knows_something = !remains_in_conflict;
                }
              else if (err->apr_err == SVN_ERR_EXTERNAL_PROGRAM)
                {
                  char buf[1024];
                  const char *message;

                  message = svn_err_best_message(err, buf, sizeof(buf));
                  SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                              "%s\n", message));
                  svn_error_clear(err);
                  continue;
                }
              else
                return svn_error_trace(err);
            }
          else
            {
              /* The external merge tool's exit code was either 0 or 1.
               * The tool may leave the file conflicted by exiting with
               * exit code 1, and we allow the user to mark the conflict
               * resolved in this case. */
              performed_edit = TRUE;
              knows_something = TRUE;
            }
        }
      else if (strcmp(opt->code, "l") == 0 || strcmp(opt->code, ":-l") == 0)
        {
          /* ### This check should be earlier as it's nasty to offer an option
           *     and then when the user chooses it say 'Invalid option'. */
          /* ### 'merged_abspath' shouldn't be necessary *before* we launch the
           *     resolver: it should be the *result* of doing so. */
          if (base_abspath && their_abspath && my_abspath && merged_abspath)
            {
              svn_error_t *err;
              char buf[1024];
              const char *message;

              err = svn_cl__merge_file_externally(base_abspath,
                                                  their_abspath,
                                                  my_abspath,
                                                  merged_abspath,
                                                  local_abspath,
                                                  config, NULL, iterpool);
              if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL ||
                          err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
                {
                  message = svn_err_best_message(err, buf, sizeof(buf));
                  SVN_ERR(svn_cmdline_fprintf(stderr, iterpool, "%s\n",
                                              message));
                  svn_error_clear(err);
                }
              else if (err)
                return svn_error_trace(err);
              else
                performed_edit = TRUE;

              if (performed_edit)
                knows_something = TRUE;
            }
          else
            SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                                        _("Invalid option.\n\n")));
        }
      else if (strcmp(opt->code, "i") == 0)
        {
          svn_boolean_t remains_in_conflict = TRUE;

          SVN_ERR(svn_cl__merge_file(&remains_in_conflict,
                                     base_abspath,
                                     their_abspath,
                                     my_abspath,
                                     merged_abspath,
                                     local_abspath,
                                     path_prefix,
                                     editor_cmd,
                                     config,
                                     pb->cancel_func,
                                     pb->cancel_baton,
                                     iterpool));

          if (!remains_in_conflict)
            knows_something = TRUE;
        }
      else if (opt->choice != svn_client_conflict_option_undefined)
        {
          if ((opt->choice == svn_client_conflict_option_working_text_where_conflicted
               || opt->choice == svn_client_conflict_option_incoming_text_where_conflicted)
              && is_binary)
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
          if (opt->choice == svn_client_conflict_option_merged_text
              && ! knows_something && diff_allowed)
            {
              SVN_ERR(svn_cmdline_fprintf(
                        stderr, iterpool,
                        _("Invalid option; use diff/edit/merge/launch "
                          "before choosing 'mark resolved'.\n\n")));
              continue;
            }

          *option_id = opt->choice;
          if (performed_edit && save_merged)
            *save_merged = TRUE;
          break;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Ask the user what to do about the property conflict described by CONFLICT.
 * Return the answer in RESULT. B is the conflict baton for this
 * conflict resolution session.
 * SCRATCH_POOL is used for temporary allocations. */
static svn_error_t *
handle_prop_conflict(svn_client_conflict_option_id_t *option_id,
                     const svn_string_t **merged_value,
                     svn_cl__accept_t *accept_which,
                     svn_boolean_t *quit,
                     const char *path_prefix,
                     svn_cmdline_prompt_baton_t *pb,
                     const char *editor_cmd,
                     apr_hash_t *config,
                     const svn_client_conflict_t *conflict,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  const char *message;
  const svn_string_t *merged_propval = NULL;
  svn_boolean_t resolved_allowed = FALSE;
  const svn_string_t *base_propval;
  const svn_string_t *my_propval;
  const svn_string_t *their_propval;

  SVN_ERR(svn_client_conflict_prop_get_propvals(NULL, &my_propval,
                                                &base_propval, &their_propval,
                                                conflict, scratch_pool));

  SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool,
                              _("Conflict for property '%s' discovered"
                                " on '%s'.\n"),
                              svn_client_conflict_prop_get_propname(conflict),
                              svn_cl__local_style_skip_ancestor(
                                path_prefix,
                                svn_client_conflict_get_local_abspath(conflict),
                                scratch_pool)));

  SVN_ERR(svn_cl__get_human_readable_prop_conflict_description(&message,
                                                               conflict,
                                                               scratch_pool));
  SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s\n", message));

  iterpool = svn_pool_create(scratch_pool);
  while (TRUE)
    {
      const resolver_option_t *opt;
      const char *options[ARRAY_LEN(prop_conflict_options)];
      const char **next_option = options;

      *next_option++ = "p";
      *next_option++ = "mf";
      *next_option++ = "tf";
      *next_option++ = "dc";
      *next_option++ = "e";
      if (resolved_allowed)
        *next_option++ = "r";
      *next_option++ = "q";
      *next_option++ = "h";
      *next_option++ = NULL;

      svn_pool_clear(iterpool);

      SVN_ERR(prompt_user(&opt, prop_conflict_options, options, pb,
                          iterpool));
      if (! opt)
        continue;

      if (strcmp(opt->code, "q") == 0)
        {
          *option_id = opt->choice;
          *accept_which = svn_cl__accept_postpone;
          *quit = TRUE;
          break;
        }
      else if (strcmp(opt->code, "dc") == 0)
        {
          SVN_ERR(show_prop_conflict(base_propval, my_propval, their_propval,
                                     merged_propval,
                                     pb->cancel_func, pb->cancel_baton,
                                     scratch_pool));
        }
      else if (strcmp(opt->code, "e") == 0)
        {
          SVN_ERR(edit_prop_conflict(&merged_propval,
                                     base_propval, my_propval, their_propval,
                                     editor_cmd, config, pb,
                                     result_pool, scratch_pool));
          resolved_allowed = (merged_propval != NULL);
        }
      else if (strcmp(opt->code, "r") == 0)
        {
          if (! resolved_allowed)
            {
              SVN_ERR(svn_cmdline_fprintf(stderr, iterpool,
                             _("Invalid option; please edit the property "
                               "first.\n\n")));
              continue;
            }

          *merged_value = merged_propval;
          *option_id = svn_client_conflict_option_merged_text;
          break;
        }
      else if (opt->choice != svn_client_conflict_option_undefined)
        {
          *option_id = opt->choice;
          break;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Ask the user what to do about the tree conflict described by CONFLICT.
 * Return the answer in RESULT. B is the conflict baton for this
 * conflict resolution session.
 * SCRATCH_POOL is used for temporary allocations. */
static svn_error_t *
handle_tree_conflict(svn_client_conflict_option_id_t *option_id,
                     svn_cl__accept_t *accept_which,
                     svn_boolean_t *quit,
                     const svn_client_conflict_t *conflict,
                     const char *path_prefix,
                     svn_cmdline_prompt_baton_t *pb,
                     apr_pool_t *scratch_pool)
{
  const char *readable_desc;
  const char *src_left_version;
  const char *src_right_version;
  const char *repos_root_url;
  const char *repos_relpath;
  svn_revnum_t peg_rev;
  svn_node_kind_t node_kind;
  apr_pool_t *iterpool;
  
  SVN_ERR(svn_cl__get_human_readable_tree_conflict_description(
           &readable_desc, conflict, scratch_pool));
  SVN_ERR(svn_cmdline_fprintf(
               stderr, scratch_pool,
               _("Tree conflict on '%s'\n   > %s\n"),
               svn_cl__local_style_skip_ancestor(path_prefix,
                 svn_client_conflict_get_local_abspath(conflict), scratch_pool),
               readable_desc));

  SVN_ERR(svn_client_conflict_get_repos_info(&repos_root_url, NULL, conflict,
                                             scratch_pool, scratch_pool));
  SVN_ERR(svn_client_conflict_get_incoming_old_repos_location(&repos_relpath,
                                                              &peg_rev,
                                                              &node_kind,
                                                              conflict,
                                                              scratch_pool,
                                                              scratch_pool));
  src_left_version =
              svn_cl__node_description(repos_root_url, repos_relpath, peg_rev,
                                       node_kind, repos_root_url, scratch_pool);
  if (src_left_version)
    SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s: %s\n",
                                _("Source  left"), src_left_version));

  SVN_ERR(svn_client_conflict_get_incoming_new_repos_location(&repos_relpath,
                                                              &peg_rev,
                                                              &node_kind,
                                                              conflict,
                                                              scratch_pool,
                                                              scratch_pool));
  src_right_version =
              svn_cl__node_description(repos_root_url, repos_relpath, peg_rev,
                                       node_kind, repos_root_url, scratch_pool);
  if (src_right_version)
    SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s: %s\n",
                                _("Source right"), src_right_version));

  iterpool = svn_pool_create(scratch_pool);
  while (1)
    {
      const resolver_option_t *opt;
      const resolver_option_t *tc_opts;

      svn_pool_clear(iterpool);

      tc_opts = tree_conflict_options;

      if (svn_client_conflict_get_operation(conflict) ==
          svn_wc_operation_update ||
          svn_client_conflict_get_operation(conflict) ==
          svn_wc_operation_switch)
        {
          svn_wc_conflict_reason_t reason;

          reason = svn_client_conflict_get_local_change(conflict);
          if (reason == svn_wc_conflict_reason_moved_away)
            {
              tc_opts = tree_conflict_options_update_moved_away;
            }
          else if (reason == svn_wc_conflict_reason_deleted ||
                   reason == svn_wc_conflict_reason_replaced)
            {
              if (svn_client_conflict_get_incoming_change(conflict) ==
                  svn_wc_conflict_action_edit &&
                  svn_client_conflict_tree_get_victim_node_kind(conflict) ==
                  svn_node_dir)
                tc_opts = tree_conflict_options_update_edit_deleted_dir;
            }
        }

      SVN_ERR(prompt_user(&opt, tc_opts, NULL, pb, iterpool));
      if (! opt)
        continue;

      if (strcmp(opt->code, "q") == 0)
        {
          *option_id = opt->choice;
          *accept_which = svn_cl__accept_postpone;
          *quit = TRUE;
          break;
        }
      else if (opt->choice != svn_client_conflict_option_undefined)
        {
          *option_id = opt->choice;
          break;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* The body of svn_cl__conflict_func_interactive(). */
static svn_error_t *
conflict_func_interactive(svn_client_conflict_option_id_t *option_id,
                          svn_boolean_t *save_merged,
                          const svn_string_t **merged_propval,
                          const svn_client_conflict_t *conflict,
                          void *baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_cl__interactive_conflict_baton_t *b = baton;
  svn_error_t *err;
  const char *base_abspath = NULL;
  const char *my_abspath = NULL;
  const char *their_abspath = NULL;
  const char *merged_abspath = svn_client_conflict_get_local_abspath(conflict);

  if (svn_client_conflict_get_kind(conflict) == svn_wc_conflict_kind_text)
    SVN_ERR(svn_client_conflict_text_get_contents(NULL, &my_abspath,
                                                  &base_abspath,
                                                  &their_abspath,
                                                  conflict, scratch_pool,
                                                  scratch_pool));

  /* Start out assuming we're going to postpone the conflict. */
  *option_id = svn_client_conflict_option_postpone;

  switch (b->accept_which)
    {
    case svn_cl__accept_invalid:
    case svn_cl__accept_unspecified:
      /* No (or no valid) --accept option, fall through to prompting. */
      break;
    case svn_cl__accept_postpone:
      *option_id = svn_client_conflict_option_postpone;
      return SVN_NO_ERROR;
    case svn_cl__accept_base:
      *option_id = svn_client_conflict_option_base_text;
      return SVN_NO_ERROR;
    case svn_cl__accept_working:
      *option_id = svn_client_conflict_option_merged_text;
      return SVN_NO_ERROR;
    case svn_cl__accept_mine_conflict:
      *option_id = svn_client_conflict_option_working_text_where_conflicted;
      return SVN_NO_ERROR;
    case svn_cl__accept_theirs_conflict:
      *option_id = svn_client_conflict_option_incoming_text_where_conflicted;
      return SVN_NO_ERROR;
    case svn_cl__accept_mine_full:
      *option_id = svn_client_conflict_option_working_text;
      return SVN_NO_ERROR;
    case svn_cl__accept_theirs_full:
      *option_id = svn_client_conflict_option_incoming_text;
      return SVN_NO_ERROR;
    case svn_cl__accept_edit:
      if (merged_abspath)
        {
          if (b->external_failed)
            {
              *option_id = svn_client_conflict_option_postpone;
              return SVN_NO_ERROR;
            }

          err = svn_cmdline__edit_file_externally(merged_abspath,
                                                  b->editor_cmd, b->config,
                                                  scratch_pool);
          if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_EDITOR ||
                      err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
            {
              char buf[1024];
              const char *message;

              message = svn_err_best_message(err, buf, sizeof(buf));
              SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s\n",
                                          message));
              svn_error_clear(err);
              b->external_failed = TRUE;
            }
          else if (err)
            return svn_error_trace(err);
          *option_id = svn_client_conflict_option_merged_text;
          return SVN_NO_ERROR;
        }
      /* else, fall through to prompting. */
      break;
    case svn_cl__accept_launch:
      if (base_abspath && their_abspath && my_abspath && merged_abspath)
        {
          svn_boolean_t remains_in_conflict;
          const char *local_abspath;

          if (b->external_failed)
            {
              *option_id = svn_client_conflict_option_postpone;
              return SVN_NO_ERROR;
            }

          local_abspath = svn_client_conflict_get_local_abspath(conflict);
          err = svn_cl__merge_file_externally(base_abspath,
                                              their_abspath,
                                              my_abspath,
                                              merged_abspath,
                                              local_abspath,
                                              b->config,
                                              &remains_in_conflict,
                                              scratch_pool);
          if (err && (err->apr_err == SVN_ERR_CL_NO_EXTERNAL_MERGE_TOOL ||
                      err->apr_err == SVN_ERR_EXTERNAL_PROGRAM))
            {
              char buf[1024];
              const char *message;

              message = svn_err_best_message(err, buf, sizeof(buf));
              SVN_ERR(svn_cmdline_fprintf(stderr, scratch_pool, "%s\n",
                                          message));
              b->external_failed = TRUE;
              return svn_error_trace(err);
            }
          else if (err)
            return svn_error_trace(err);

          if (remains_in_conflict)
            *option_id = svn_client_conflict_option_postpone;
          else
            *option_id = svn_client_conflict_option_merged_text;
          return SVN_NO_ERROR;
        }
      /* else, fall through to prompting. */
      break;
    }

  /* Print a summary of conflicts before starting interactive resolution */
  if (! b->printed_summary)
    {
      SVN_ERR(svn_cl__print_conflict_stats(b->conflict_stats, scratch_pool));
      b->printed_summary = TRUE;
    }

  /* We're in interactive mode and either the user gave no --accept
     option or the option did not apply; let's prompt. */

  /* Handle the most common cases, which is either:

     Conflicting edits on a file's text, or
     Conflicting edits on a property.
  */
  if (((svn_client_conflict_get_kind(conflict) == svn_wc_conflict_kind_text)
       && (svn_client_conflict_get_incoming_change(conflict) ==
           svn_wc_conflict_action_edit)
       && (svn_client_conflict_get_local_change(conflict) ==
           svn_wc_conflict_reason_edited)))
    SVN_ERR(handle_text_conflict(option_id, save_merged, &b->accept_which,
                                 &b->quit, conflict, b->path_prefix, b->pb,
                                 b->editor_cmd, b->config, scratch_pool));
  else if (svn_client_conflict_get_kind(conflict) ==
           svn_wc_conflict_kind_property)
    SVN_ERR(handle_prop_conflict(option_id, merged_propval, &b->accept_which,
                                 &b->quit, b->path_prefix, b->pb,
                                 b->editor_cmd, b->config, conflict,
                                 result_pool, scratch_pool));
  else if (svn_client_conflict_get_kind(conflict) == svn_wc_conflict_kind_tree)
    SVN_ERR(handle_tree_conflict(option_id, &b->accept_which, &b->quit,
                                 conflict, b->path_prefix, b->pb,
                                 scratch_pool));

  else /* other types of conflicts -- do nothing about them. */
    {
      *option_id = svn_client_conflict_option_postpone;
    }

  return SVN_NO_ERROR;
}

/*
 * Return a legacy conflict choice corresponding to OPTION_ID.
 * Return svn_wc_conflict_choose_undefined if no corresponding
 * legacy conflict choice exists.
 */
static svn_wc_conflict_choice_t
conflict_option_id_to_wc_conflict_choice(
  svn_client_conflict_option_id_t option_id)
{

  switch (option_id)
    {
      case svn_client_conflict_option_undefined:
        return svn_wc_conflict_choose_undefined;

      case svn_client_conflict_option_postpone:
        return svn_wc_conflict_choose_postpone;

      case svn_client_conflict_option_base_text:
        return svn_wc_conflict_choose_base;

      case svn_client_conflict_option_incoming_text:
        return svn_wc_conflict_choose_theirs_full;

      case svn_client_conflict_option_working_text:
        return svn_wc_conflict_choose_mine_full;

      case svn_client_conflict_option_incoming_text_where_conflicted:
        return svn_wc_conflict_choose_theirs_conflict;

      case svn_client_conflict_option_working_text_where_conflicted:
        return svn_wc_conflict_choose_mine_conflict;

      case svn_client_conflict_option_merged_text:
        return svn_wc_conflict_choose_merged;

      case svn_client_conflict_option_unspecified:
        return svn_wc_conflict_choose_unspecified;

      default:
        break;
    }

  return svn_wc_conflict_choose_undefined;
}

svn_error_t *
svn_cl__conflict_func_interactive(svn_wc_conflict_result_t **result,
                                  const svn_wc_conflict_description2_t *desc,
                                  void *baton,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_cl__interactive_conflict_baton_t *b = baton;
  svn_client_conflict_t *conflict;
  svn_client_conflict_option_id_t option_id;
  svn_boolean_t save_merged = FALSE;
  const svn_string_t *merged_propval = NULL;

  SVN_ERR(svn_client_conflict_from_wc_description2_t(&conflict, desc,
                                                     scratch_pool,
                                                     scratch_pool));
  *result = svn_wc_create_conflict_result(svn_client_conflict_option_postpone,
                                          NULL, result_pool);
  SVN_ERR(conflict_func_interactive(&option_id, &save_merged, &merged_propval,
                                    conflict, baton,
                                    result_pool, scratch_pool));
  (*result)->choice = conflict_option_id_to_wc_conflict_choice(option_id);
  (*result)->save_merged = save_merged;
  (*result)->merged_value = merged_propval;

  /* If we are resolving a conflict, adjust the summary of conflicts. */
  if ((*result)->choice != svn_client_conflict_option_postpone)
    {
      const char *local_path
        = svn_cl__local_style_skip_ancestor(
            b->path_prefix, svn_client_conflict_get_local_abspath(conflict),
            scratch_pool);

      svn_cl__conflict_stats_resolved(b->conflict_stats, local_path,
                                      svn_client_conflict_get_kind(conflict));
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_cl__resolve_conflict(svn_boolean_t *resolved,
                         svn_client_conflict_t *conflict,
                         svn_client_conflict_option_id_t option_id,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool)
{
  svn_cl__interactive_conflict_baton_t *b = ctx->conflict_baton2;

  if (option_id == svn_client_conflict_option_unspecified)
    SVN_ERR(conflict_func_interactive(&option_id, NULL, NULL, conflict, b,
                                      scratch_pool, scratch_pool));

  SVN_ERR_ASSERT(option_id != svn_client_conflict_option_unspecified);

  /* If we are resolving a conflict, adjust the summary of conflicts. */
  if (option_id != svn_client_conflict_option_postpone)
    {
      const char *local_relpath
        = svn_cl__local_style_skip_ancestor(
            b->path_prefix, svn_client_conflict_get_local_abspath(conflict),
            scratch_pool);
      svn_wc_conflict_kind_t conflict_kind;
      const char *local_abspath;
      const char *lock_abspath;
      svn_error_t *err;

      local_abspath = svn_client_conflict_get_local_abspath(conflict);

      /* ### for now, resolve conflict using legacy API */
      SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath,
                                                     ctx->wc_ctx,
                                                     local_abspath,
                                                     scratch_pool,
                                                     scratch_pool));
      conflict_kind = svn_client_conflict_get_kind(conflict);

      err = svn_wc_resolved_conflict5(
              ctx->wc_ctx, local_abspath, svn_depth_empty, /* ??? */
              conflict_kind == svn_wc_conflict_kind_text,
              conflict_kind == svn_wc_conflict_kind_property ? "" : NULL,
              conflict_kind == svn_wc_conflict_kind_tree,
              conflict_option_id_to_wc_conflict_choice(option_id),
              ctx->cancel_func, ctx->cancel_baton,
              ctx->notify_func2, ctx->notify_baton2,
              scratch_pool);

      err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                     lock_abspath,
                                                                     scratch_pool));
      svn_io_sleep_for_timestamps(local_abspath, scratch_pool);

      SVN_ERR(err);
    
      svn_cl__conflict_stats_resolved(b->conflict_stats, local_relpath,
                                      svn_client_conflict_get_kind(conflict));
      *resolved = TRUE;
    }
  else
    {
      *resolved = FALSE;
    }

  return SVN_NO_ERROR;
}
