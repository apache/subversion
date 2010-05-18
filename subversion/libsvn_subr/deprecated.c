/*
 * deprecated.c:  holding file for all deprecated APIs.
 *                "we can't lose 'em, but we can shun 'em!"
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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

/* We define this here to remove any further warnings about the usage of
   deprecated functions in this file. */
#define SVN_DEPRECATED

#include "svn_subst.h"
#include "svn_path.h"
#include "svn_opt.h"
#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_dso.h"

#include "opt.h"
#include "private/svn_opt_private.h"

#include "svn_private_config.h"




/*** Code. ***/

/*** From subst.c ***/
/* Convert an old-style svn_subst_keywords_t struct * into a new-style
 * keywords hash.  Keyword values are shallow copies, so the produced
 * hash must not be assumed to have lifetime longer than the struct it
 * is based on.  A NULL input causes a NULL output. */
static apr_hash_t *
kwstruct_to_kwhash(const svn_subst_keywords_t *kwstruct,
                   apr_pool_t *pool)
{
  apr_hash_t *kwhash;

  if (kwstruct == NULL)
    return NULL;

  kwhash = apr_hash_make(pool);

  if (kwstruct->revision)
    {
      apr_hash_set(kwhash, SVN_KEYWORD_REVISION_LONG,
                   APR_HASH_KEY_STRING, kwstruct->revision);
      apr_hash_set(kwhash, SVN_KEYWORD_REVISION_MEDIUM,
                   APR_HASH_KEY_STRING, kwstruct->revision);
      apr_hash_set(kwhash, SVN_KEYWORD_REVISION_SHORT,
                   APR_HASH_KEY_STRING, kwstruct->revision);
    }
  if (kwstruct->date)
    {
      apr_hash_set(kwhash, SVN_KEYWORD_DATE_LONG,
                   APR_HASH_KEY_STRING, kwstruct->date);
      apr_hash_set(kwhash, SVN_KEYWORD_DATE_SHORT,
                   APR_HASH_KEY_STRING, kwstruct->date);
    }
  if (kwstruct->author)
    {
      apr_hash_set(kwhash, SVN_KEYWORD_AUTHOR_LONG,
                   APR_HASH_KEY_STRING, kwstruct->author);
      apr_hash_set(kwhash, SVN_KEYWORD_AUTHOR_SHORT,
                   APR_HASH_KEY_STRING, kwstruct->author);
    }
  if (kwstruct->url)
    {
      apr_hash_set(kwhash, SVN_KEYWORD_URL_LONG,
                   APR_HASH_KEY_STRING, kwstruct->url);
      apr_hash_set(kwhash, SVN_KEYWORD_URL_SHORT,
                   APR_HASH_KEY_STRING, kwstruct->url);
    }
  if (kwstruct->id)
    {
      apr_hash_set(kwhash, SVN_KEYWORD_ID,
                   APR_HASH_KEY_STRING, kwstruct->id);
    }

  return kwhash;
}


svn_error_t *
svn_subst_translate_stream3(svn_stream_t *src_stream,
                            svn_stream_t *dst_stream,
                            const char *eol_str,
                            svn_boolean_t repair,
                            apr_hash_t *keywords,
                            svn_boolean_t expand,
                            apr_pool_t *pool)
{
  /* The docstring requires that *some* translation be requested. */
  SVN_ERR_ASSERT(eol_str || keywords);

  /* We don't want the copy3 to close the provided streams. */
  src_stream = svn_stream_disown(src_stream, pool);
  dst_stream = svn_stream_disown(dst_stream, pool);

  /* Wrap the destination stream with our translation stream. It is more
     efficient than wrapping the source stream. */
  dst_stream = svn_subst_stream_translated(dst_stream, eol_str, repair,
                                           keywords, expand, pool);

  return svn_stream_copy3(src_stream, dst_stream, NULL, NULL, pool);
}

svn_error_t *
svn_subst_translate_stream2(svn_stream_t *s, /* src stream */
                            svn_stream_t *d, /* dst stream */
                            const char *eol_str,
                            svn_boolean_t repair,
                            const svn_subst_keywords_t *keywords,
                            svn_boolean_t expand,
                            apr_pool_t *pool)
{
  apr_hash_t *kh = kwstruct_to_kwhash(keywords, pool);

  return svn_subst_translate_stream3(s, d, eol_str, repair, kh, expand, pool);
}

svn_error_t *
svn_subst_translate_stream(svn_stream_t *s, /* src stream */
                           svn_stream_t *d, /* dst stream */
                           const char *eol_str,
                           svn_boolean_t repair,
                           const svn_subst_keywords_t *keywords,
                           svn_boolean_t expand)
{
  apr_pool_t *pool = svn_pool_create(NULL);
  svn_error_t *err = svn_subst_translate_stream2(s, d, eol_str, repair,
                                                 keywords, expand, pool);
  svn_pool_destroy(pool);
  return err;
}

svn_error_t *
svn_subst_translate_cstring(const char *src,
                            const char **dst,
                            const char *eol_str,
                            svn_boolean_t repair,
                            const svn_subst_keywords_t *keywords,
                            svn_boolean_t expand,
                            apr_pool_t *pool)
{
  apr_hash_t *kh = kwstruct_to_kwhash(keywords, pool);

  return svn_subst_translate_cstring2(src, dst, eol_str, repair,
                                      kh, expand, pool);
}

svn_error_t *
svn_subst_copy_and_translate(const char *src,
                             const char *dst,
                             const char *eol_str,
                             svn_boolean_t repair,
                             const svn_subst_keywords_t *keywords,
                             svn_boolean_t expand,
                             apr_pool_t *pool)
{
  return svn_subst_copy_and_translate2(src, dst, eol_str, repair, keywords,
                                       expand, FALSE, pool);
}

svn_error_t *
svn_subst_copy_and_translate2(const char *src,
                              const char *dst,
                              const char *eol_str,
                              svn_boolean_t repair,
                              const svn_subst_keywords_t *keywords,
                              svn_boolean_t expand,
                              svn_boolean_t special,
                              apr_pool_t *pool)
{
  apr_hash_t *kh = kwstruct_to_kwhash(keywords, pool);

  return svn_subst_copy_and_translate3(src, dst, eol_str,
                                       repair, kh, expand, special,
                                       pool);
}

svn_error_t *
svn_subst_stream_translated_to_normal_form(svn_stream_t **stream,
                                           svn_stream_t *source,
                                           svn_subst_eol_style_t eol_style,
                                           const char *eol_str,
                                           svn_boolean_t always_repair_eols,
                                           apr_hash_t *keywords,
                                           apr_pool_t *pool)
{
  if (eol_style == svn_subst_eol_style_native)
    eol_str = SVN_SUBST_NATIVE_EOL_STR;
  else if (! (eol_style == svn_subst_eol_style_fixed
              || eol_style == svn_subst_eol_style_none))
    return svn_error_create(SVN_ERR_IO_UNKNOWN_EOL, NULL, NULL);

 *stream = svn_subst_stream_translated(source, eol_str,
                                       eol_style == svn_subst_eol_style_fixed
                                       || always_repair_eols,
                                       keywords, FALSE, pool);

 return SVN_NO_ERROR;
}

svn_error_t *
svn_subst_stream_detranslated(svn_stream_t **stream_p,
                              const char *src,
                              svn_subst_eol_style_t eol_style,
                              const char *eol_str,
                              svn_boolean_t always_repair_eols,
                              apr_hash_t *keywords,
                              svn_boolean_t special,
                              apr_pool_t *pool)
{
  svn_stream_t *src_stream;

  if (special)
    return svn_subst_read_specialfile(stream_p, src, pool, pool);

  /* This will be closed by svn_subst_stream_translated_to_normal_form
     when the returned stream is closed. */
  SVN_ERR(svn_stream_open_readonly(&src_stream, src, pool, pool));

  return svn_subst_stream_translated_to_normal_form(stream_p, src_stream,
                                                    eol_style, eol_str,
                                                    always_repair_eols,
                                                    keywords, pool);
}

svn_error_t *
svn_subst_translate_to_normal_form(const char *src,
                                   const char *dst,
                                   svn_subst_eol_style_t eol_style,
                                   const char *eol_str,
                                   svn_boolean_t always_repair_eols,
                                   apr_hash_t *keywords,
                                   svn_boolean_t special,
                                   apr_pool_t *pool)
{

  if (eol_style == svn_subst_eol_style_native)
    eol_str = SVN_SUBST_NATIVE_EOL_STR;
  else if (! (eol_style == svn_subst_eol_style_fixed
              || eol_style == svn_subst_eol_style_none))
    return svn_error_create(SVN_ERR_IO_UNKNOWN_EOL, NULL, NULL);

  return svn_subst_copy_and_translate3(src, dst, eol_str,
                                       eol_style == svn_subst_eol_style_fixed
                                       || always_repair_eols,
                                       keywords,
                                       FALSE /* contract keywords */,
                                       special,
                                       pool);
}


/*** From opt.c ***/
/* Same as print_command_info2(), but with deprecated struct revision. */
static svn_error_t *
print_command_info(const svn_opt_subcommand_desc_t *cmd,
                   const apr_getopt_option_t *options_table,
                   svn_boolean_t help,
                   apr_pool_t *pool,
                   FILE *stream)
{
  svn_boolean_t first_time;
  apr_size_t i;

  /* Print the canonical command name. */
  SVN_ERR(svn_cmdline_fputs(cmd->name, stream, pool));

  /* Print the list of aliases. */
  first_time = TRUE;
  for (i = 0; i < SVN_OPT_MAX_ALIASES; i++)
    {
      if (cmd->aliases[i] == NULL)
        break;

      if (first_time) {
        SVN_ERR(svn_cmdline_fputs(" (", stream, pool));
        first_time = FALSE;
      }
      else
        SVN_ERR(svn_cmdline_fputs(", ", stream, pool));

      SVN_ERR(svn_cmdline_fputs(cmd->aliases[i], stream, pool));
    }

  if (! first_time)
    SVN_ERR(svn_cmdline_fputs(")", stream, pool));

  if (help)
    {
      const apr_getopt_option_t *option;
      svn_boolean_t have_options = FALSE;

      SVN_ERR(svn_cmdline_fprintf(stream, pool, ": %s", _(cmd->help)));

      /* Loop over all valid option codes attached to the subcommand */
      for (i = 0; i < SVN_OPT_MAX_OPTIONS; i++)
        {
          if (cmd->valid_options[i])
            {
              if (have_options == FALSE)
                {
                  SVN_ERR(svn_cmdline_fputs(_("\nValid options:\n"),
                                            stream, pool));
                  have_options = TRUE;
                }

              /* convert each option code into an option */
              option =
                svn_opt_get_option_from_code2(cmd->valid_options[i],
                                              options_table, NULL, pool);

              /* print the option's docstring */
              if (option && option->description)
                {
                  const char *optstr;
                  svn_opt_format_option(&optstr, option, TRUE, pool);
                  SVN_ERR(svn_cmdline_fprintf(stream, pool, "  %s\n",
                                              optstr));
                }
            }
        }

      if (have_options)
        SVN_ERR(svn_cmdline_fprintf(stream, pool, "\n"));
    }

  return SVN_NO_ERROR;
}

const svn_opt_subcommand_desc_t *
svn_opt_get_canonical_subcommand(const svn_opt_subcommand_desc_t *table,
                                 const char *cmd_name)
{
  int i = 0;

  if (cmd_name == NULL)
    return NULL;

  while (table[i].name) {
    int j;
    if (strcmp(cmd_name, table[i].name) == 0)
      return table + i;
    for (j = 0; (j < SVN_OPT_MAX_ALIASES) && table[i].aliases[j]; j++)
      if (strcmp(cmd_name, table[i].aliases[j]) == 0)
        return table + i;

    i++;
  }

  /* If we get here, there was no matching subcommand name or alias. */
  return NULL;
}

void
svn_opt_subcommand_help2(const char *subcommand,
                         const svn_opt_subcommand_desc2_t *table,
                         const apr_getopt_option_t *options_table,
                         apr_pool_t *pool)
{
  svn_opt_subcommand_help3(subcommand, table, options_table,
                           NULL, pool);
}

void
svn_opt_subcommand_help(const char *subcommand,
                        const svn_opt_subcommand_desc_t *table,
                        const apr_getopt_option_t *options_table,
                        apr_pool_t *pool)
{
  const svn_opt_subcommand_desc_t *cmd =
    svn_opt_get_canonical_subcommand(table, subcommand);
  svn_error_t *err;

  if (cmd)
    err = print_command_info(cmd, options_table, TRUE, pool, stdout);
  else
    err = svn_cmdline_fprintf(stderr, pool,
                              _("\"%s\": unknown command.\n\n"), subcommand);

  if (err) {
    svn_handle_error2(err, stderr, FALSE, "svn: ");
    svn_error_clear(err);
  }
}

svn_error_t *
svn_opt_args_to_target_array3(apr_array_header_t **targets_p,
                              apr_getopt_t *os,
                              apr_array_header_t *known_targets,
                              apr_pool_t *pool)
{
  return svn_opt__args_to_target_array(targets_p, os,known_targets, pool);
}

svn_error_t *
svn_opt_args_to_target_array2(apr_array_header_t **targets_p,
                              apr_getopt_t *os,
                              apr_array_header_t *known_targets,
                              apr_pool_t *pool)
{
  svn_error_t *err = svn_opt_args_to_target_array3(targets_p, os,
                                                   known_targets, pool);

  if (err && err->apr_err == SVN_ERR_RESERVED_FILENAME_SPECIFIED)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  return err;
}

svn_error_t *
svn_opt_args_to_target_array(apr_array_header_t **targets_p,
                             apr_getopt_t *os,
                             apr_array_header_t *known_targets,
                             svn_opt_revision_t *start_revision,
                             svn_opt_revision_t *end_revision,
                             svn_boolean_t extract_revisions,
                             apr_pool_t *pool)
{
  apr_array_header_t *output_targets;

  SVN_ERR(svn_opt_args_to_target_array2(&output_targets, os,
                                        known_targets, pool));

  if (extract_revisions)
    {
      svn_opt_revision_t temprev;
      const char *path;

      if (output_targets->nelts > 0)
        {
          path = APR_ARRAY_IDX(output_targets, 0, const char *);
          SVN_ERR(svn_opt_parse_path(&temprev, &path, path, pool));
          if (temprev.kind != svn_opt_revision_unspecified)
            {
              APR_ARRAY_IDX(output_targets, 0, const char *) = path;
              start_revision->kind = temprev.kind;
              start_revision->value = temprev.value;
            }
        }
      if (output_targets->nelts > 1)
        {
          path = APR_ARRAY_IDX(output_targets, 1, const char *);
          SVN_ERR(svn_opt_parse_path(&temprev, &path, path, pool));
          if (temprev.kind != svn_opt_revision_unspecified)
            {
              APR_ARRAY_IDX(output_targets, 1, const char *) = path;
              end_revision->kind = temprev.kind;
              end_revision->value = temprev.value;
            }
        }
    }

  *targets_p = output_targets;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_opt_print_help2(apr_getopt_t *os,
                    const char *pgm_name,
                    svn_boolean_t print_version,
                    svn_boolean_t quiet,
                    const char *version_footer,
                    const char *header,
                    const svn_opt_subcommand_desc2_t *cmd_table,
                    const apr_getopt_option_t *option_table,
                    const char *footer,
                    apr_pool_t *pool)
{
  return svn_opt_print_help3(os,
                             pgm_name,
                             print_version,
                             quiet,
                             version_footer,
                             header,
                             cmd_table,
                             option_table,
                             NULL,
                             footer,
                             pool);
}

svn_error_t *
svn_opt_print_help(apr_getopt_t *os,
                   const char *pgm_name,
                   svn_boolean_t print_version,
                   svn_boolean_t quiet,
                   const char *version_footer,
                   const char *header,
                   const svn_opt_subcommand_desc_t *cmd_table,
                   const apr_getopt_option_t *option_table,
                   const char *footer,
                   apr_pool_t *pool)
{
  apr_array_header_t *targets = NULL;
  int i;

  if (os)
    SVN_ERR(svn_opt_parse_all_args(&targets, os, pool));

  if (os && targets->nelts)  /* help on subcommand(s) requested */
    for (i = 0; i < targets->nelts; i++)
      {
        svn_opt_subcommand_help(APR_ARRAY_IDX(targets, i, const char *),
                                cmd_table, option_table, pool);
      }
  else if (print_version)   /* just --version */
    SVN_ERR(svn_opt__print_version_info(pgm_name, version_footer, quiet,
                                        pool));
  else if (os && !targets->nelts)            /* `-h', `--help', or `help' */
    svn_opt_print_generic_help(header,
                               cmd_table,
                               option_table,
                               footer,
                               pool,
                               stdout);
  else                                       /* unknown option or cmd */
    SVN_ERR(svn_cmdline_fprintf(stderr, pool,
                                _("Type '%s help' for usage.\n"), pgm_name));

  return SVN_NO_ERROR;
}

void
svn_opt_print_generic_help(const char *header,
                           const svn_opt_subcommand_desc_t *cmd_table,
                           const apr_getopt_option_t *opt_table,
                           const char *footer,
                           apr_pool_t *pool, FILE *stream)
{
  int i = 0;
  svn_error_t *err;

  if (header)
    if ((err = svn_cmdline_fputs(header, stream, pool)))
      goto print_error;

  while (cmd_table[i].name)
    {
      if ((err = svn_cmdline_fputs("   ", stream, pool))
          || (err = print_command_info(cmd_table + i, opt_table, FALSE,
                                       pool, stream))
          || (err = svn_cmdline_fputs("\n", stream, pool)))
        goto print_error;
      i++;
    }

  if ((err = svn_cmdline_fputs("\n", stream, pool)))
    goto print_error;

  if (footer)
    if ((err = svn_cmdline_fputs(footer, stream, pool)))
      goto print_error;

  return;

 print_error:
  svn_handle_error2(err, stderr, FALSE, "svn: ");
  svn_error_clear(err);
}

/*** From io.c ***/
svn_error_t *
svn_io_open_unique_file2(apr_file_t **file,
                         const char **temp_path,
                         const char *path,
                         const char *suffix,
                         svn_io_file_del_t delete_when,
                         apr_pool_t *pool)
{
  const char *dirpath;
  const char *filename;

  svn_path_split(path, &dirpath, &filename, pool);
  return svn_io_open_uniquely_named(file, temp_path,
                                    dirpath, filename, suffix,
                                    delete_when,
                                    pool, pool);
}

svn_error_t *
svn_io_open_unique_file(apr_file_t **file,
                        const char **temp_path,
                        const char *path,
                        const char *suffix,
                        svn_boolean_t delete_on_close,
                        apr_pool_t *pool)
{
  return svn_io_open_unique_file2(file, temp_path,
                                  path, suffix,
                                  delete_on_close
                                    ? svn_io_file_del_on_close
                                    : svn_io_file_del_none,
                                  pool);
}

svn_error_t *
svn_io_run_diff(const char *dir,
                const char *const *user_args,
                int num_user_args,
                const char *label1,
                const char *label2,
                const char *from,
                const char *to,
                int *pexitcode,
                apr_file_t *outfile,
                apr_file_t *errfile,
                const char *diff_cmd,
                apr_pool_t *pool)
{
  SVN_ERR(svn_path_cstring_to_utf8(&diff_cmd, diff_cmd, pool));

  return svn_io_run_diff2(dir, user_args, num_user_args, label1, label2,
                          from, to, pexitcode, outfile, errfile, diff_cmd,
                          pool);
}

svn_error_t *
svn_io_run_diff3_2(int *exitcode,
                   const char *dir,
                   const char *mine,
                   const char *older,
                   const char *yours,
                   const char *mine_label,
                   const char *older_label,
                   const char *yours_label,
                   apr_file_t *merged,
                   const char *diff3_cmd,
                   const apr_array_header_t *user_args,
                   apr_pool_t *pool)
{
  SVN_ERR(svn_path_cstring_to_utf8(&diff3_cmd, diff3_cmd, pool));

  return svn_io_run_diff3_3(exitcode, dir, mine, older, yours, mine_label,
                            older_label, yours_label, merged, diff3_cmd,
                            user_args, pool);
}

svn_error_t *
svn_io_run_diff3(const char *dir,
                 const char *mine,
                 const char *older,
                 const char *yours,
                 const char *mine_label,
                 const char *older_label,
                 const char *yours_label,
                 apr_file_t *merged,
                 int *exitcode,
                 const char *diff3_cmd,
                 apr_pool_t *pool)
{
  return svn_io_run_diff3_2(exitcode, dir, mine, older, yours,
                            mine_label, older_label, yours_label,
                            merged, diff3_cmd, NULL, pool);
}

/*** From constructors.c ***/
svn_log_changed_path_t *
svn_log_changed_path_dup(const svn_log_changed_path_t *changed_path,
                         apr_pool_t *pool)
{
  svn_log_changed_path_t *new_changed_path
    = apr_palloc(pool, sizeof(*new_changed_path));

  *new_changed_path = *changed_path;

  if (new_changed_path->copyfrom_path)
    new_changed_path->copyfrom_path =
      apr_pstrdup(pool, new_changed_path->copyfrom_path);

  return new_changed_path;
}

/*** From cmdline.c ***/
svn_error_t *
svn_cmdline_prompt_user(const char **result,
                        const char *prompt_str,
                        apr_pool_t *pool)
{
  return svn_cmdline_prompt_user2(result, prompt_str, NULL, pool);
}

svn_error_t *
svn_cmdline_setup_auth_baton(svn_auth_baton_t **ab,
                             svn_boolean_t non_interactive,
                             const char *auth_username,
                             const char *auth_password,
                             const char *config_dir,
                             svn_boolean_t no_auth_cache,
                             svn_config_t *cfg,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *pool)
{
  return svn_cmdline_create_auth_baton(ab, non_interactive,
                                       auth_username, auth_password,
                                       config_dir, no_auth_cache, FALSE,
                                       cfg, cancel_func, cancel_baton, pool);
}

/*** From dso.c ***/
void
svn_dso_initialize(void)
{
  svn_error_t *err = svn_dso_initialize2();
  if (err)
    {
      svn_error_clear(err);
      abort();
    }
}

/*** From simple_providers.c ***/
void
svn_auth_get_simple_provider(svn_auth_provider_object_t **provider,
                             apr_pool_t *pool)
{
  svn_auth_get_simple_provider2(provider, NULL, NULL, pool);
}

/*** From ssl_client_cert_pw_providers.c ***/
void
svn_auth_get_ssl_client_cert_pw_file_provider
  (svn_auth_provider_object_t **provider,
   apr_pool_t *pool)
{
  svn_auth_get_ssl_client_cert_pw_file_provider2(provider, NULL, NULL, pool);
}

/*** From path.c ***/
const char *
svn_path_url_add_component(const char *url,
                           const char *component,
                           apr_pool_t *pool)
{
  /* URL can have trailing '/' */
  url = svn_path_canonicalize(url, pool);

  return svn_path_url_add_component2(url, component, pool);
}
