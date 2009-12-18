/*
 * info-cmd.c -- Display information about a resource
 *
 * ====================================================================
 * Copyright (c) 2002-2008 CollabNet.  All rights reserved.
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

#include "svn_string.h"
#include "svn_cmdline.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_xml.h"
#include "cl.h"

#include "svn_private_config.h"
#include "tree-conflicts.h"


/*** Code. ***/

static svn_error_t *
svn_cl__info_print_time(apr_time_t atime,
                        const char *desc,
                        apr_pool_t *pool)
{
  const char *time_utf8;

  time_utf8 = svn_time_to_human_cstring(atime, pool);
  return svn_cmdline_printf(pool, "%s: %s\n", desc, time_utf8);
}


/* Return string representation of SCHEDULE */
static const char *
schedule_str(svn_wc_schedule_t schedule)
{
  switch (schedule)
    {
    case svn_wc_schedule_normal:
      return "normal";
    case svn_wc_schedule_add:
      return "add";
    case svn_wc_schedule_delete:
      return "delete";
    case svn_wc_schedule_replace:
      return "replace";
    default:
      return "none";
    }
}


/* A callback of type svn_info_receiver_t.
   Prints svn info in xml mode to standard out */
static svn_error_t *
print_info_xml(void *baton,
               const char *target,
               const svn_info_t *info,
               apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create("", pool);
  const char *rev_str;

  if (SVN_IS_VALID_REVNUM(info->rev))
    rev_str = apr_psprintf(pool, "%ld", info->rev);
  else
    rev_str = apr_pstrdup(pool, _("Resource is not under version control."));

  /* "<entry ...>" */
  svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "entry",
                        "path", svn_path_local_style(target, pool),
                        "kind", svn_cl__node_kind_str_xml(info->kind),
                        "revision", rev_str,
                        NULL);

  svn_cl__xml_tagged_cdata(&sb, pool, "url", info->URL);

  if (info->repos_root_URL || info->repos_UUID)
    {
      /* "<repository>" */
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "repository", NULL);

      /* "<root> xx </root>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "root", info->repos_root_URL);

      /* "<uuid> xx </uuid>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "uuid", info->repos_UUID);

      /* "</repository>" */
      svn_xml_make_close_tag(&sb, pool, "repository");
    }

  if (info->has_wc_info)
    {
      /* "<wc-info>" */
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "wc-info", NULL);

      /* "<schedule> xx </schedule>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "schedule",
                               schedule_str(info->schedule));

      /* "<depth> xx </depth>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "depth",
                               svn_depth_to_word(info->depth));

      /* "<copy-from-url> xx </copy-from-url>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "copy-from-url",
                               info->copyfrom_url);

      /* "<copy-from-rev> xx </copy-from-rev>" */
      if (SVN_IS_VALID_REVNUM(info->copyfrom_rev))
        svn_cl__xml_tagged_cdata(&sb, pool, "copy-from-rev",
                                 apr_psprintf(pool, "%ld",
                                              info->copyfrom_rev));

      /* "<text-updated> xx </text-updated>" */
      if (info->text_time)
        svn_cl__xml_tagged_cdata(&sb, pool, "text-updated",
                                 svn_time_to_cstring(info->text_time, pool));

      /* "<checksum> xx </checksum>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "checksum", info->checksum);

      if (info->changelist)
        /* "<changelist> xx </changelist>" */
        svn_cl__xml_tagged_cdata(&sb, pool, "changelist", info->changelist);

      /* "</wc-info>" */
      svn_xml_make_close_tag(&sb, pool, "wc-info");
    }

  if (info->last_changed_author
      || SVN_IS_VALID_REVNUM(info->last_changed_rev)
      || info->last_changed_date)
    {
      svn_cl__print_xml_commit(&sb, info->last_changed_rev,
                               info->last_changed_author,
                               svn_time_to_cstring(info->last_changed_date,
                                                   pool),
                               pool);
    }

  if (info->conflict_old || info->conflict_wrk
      || info->conflict_new || info->prejfile)
    {
      /* "<conflict>" */
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "conflict", NULL);

      /* "<prev-base-file> xx </prev-base-file>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "prev-base-file",
                               info->conflict_old);

      /* "<prev-wc-file> xx </prev-wc-file>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "prev-wc-file",
                               info->conflict_wrk);

      /* "<cur-base-file> xx </cur-base-file>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "cur-base-file",
                               info->conflict_new);

      /* "<prop-file> xx </prop-file>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "prop-file", info->prejfile);

      /* "</conflict>" */
      svn_xml_make_close_tag(&sb, pool, "conflict");
    }

  if (info->lock)
    {
      /* "<lock>" */
      svn_xml_make_open_tag(&sb, pool, svn_xml_normal, "lock", NULL);

      /* "<token> xx </token>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "token", info->lock->token);

      /* "<owner> xx </owner>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "owner", info->lock->owner);

      /* "<comment ...> xxxx </comment>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "comment", info->lock->comment);

      /* "<created> xx </created>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "created",
                               svn_time_to_cstring
                               (info->lock->creation_date, pool));

      /* "<expires> xx </expires>" */
      svn_cl__xml_tagged_cdata(&sb, pool, "expires",
                               svn_time_to_cstring
                               (info->lock->expiration_date, pool));

      /* "</lock>" */
      svn_xml_make_close_tag(&sb, pool, "lock");
    }

  if (info->tree_conflict)
    SVN_ERR(svn_cl__append_tree_conflict_info_xml(sb, info->tree_conflict,
                                                  pool));

  /* "</entry>" */
  svn_xml_make_close_tag(&sb, pool, "entry");

  return svn_cl__error_checked_fputs(sb->data, stdout);
}


/* A callback of type svn_info_receiver_t. */
static svn_error_t *
print_info(void *baton,
           const char *target,
           const svn_info_t *info,
           apr_pool_t *pool)
{
  SVN_ERR(svn_cmdline_printf(pool, _("Path: %s\n"),
                             svn_path_local_style(target, pool)));

  /* ### remove this someday:  it's only here for cmdline output
     compatibility with svn 1.1 and older.  */
  if (info->kind != svn_node_dir)
    SVN_ERR(svn_cmdline_printf(pool, _("Name: %s\n"),
                               svn_path_basename(target, pool)));

  if (info->URL)
    SVN_ERR(svn_cmdline_printf(pool, _("URL: %s\n"), info->URL));

  if (info->repos_root_URL)
    SVN_ERR(svn_cmdline_printf(pool, _("Repository Root: %s\n"),
                               info->repos_root_URL));

  if (info->repos_UUID)
    SVN_ERR(svn_cmdline_printf(pool, _("Repository UUID: %s\n"),
                               info->repos_UUID));

  if (SVN_IS_VALID_REVNUM(info->rev))
    SVN_ERR(svn_cmdline_printf(pool, _("Revision: %ld\n"), info->rev));

  switch (info->kind)
    {
    case svn_node_file:
      SVN_ERR(svn_cmdline_printf(pool, _("Node Kind: file\n")));
      break;

    case svn_node_dir:
      SVN_ERR(svn_cmdline_printf(pool, _("Node Kind: directory\n")));
      break;

    case svn_node_none:
      SVN_ERR(svn_cmdline_printf(pool, _("Node Kind: none\n")));
      break;

    case svn_node_unknown:
    default:
      SVN_ERR(svn_cmdline_printf(pool, _("Node Kind: unknown\n")));
      break;
    }

  if (info->has_wc_info)
    {
      switch (info->schedule)
        {
        case svn_wc_schedule_normal:
          SVN_ERR(svn_cmdline_printf(pool, _("Schedule: normal\n")));
          break;

        case svn_wc_schedule_add:
          SVN_ERR(svn_cmdline_printf(pool, _("Schedule: add\n")));
          break;

        case svn_wc_schedule_delete:
          SVN_ERR(svn_cmdline_printf(pool, _("Schedule: delete\n")));
          break;

        case svn_wc_schedule_replace:
          SVN_ERR(svn_cmdline_printf(pool, _("Schedule: replace\n")));
          break;

        default:
          break;
        }

      switch (info->depth)
        {
        case svn_depth_unknown:
          /* Unknown depth is the norm for remote directories anyway
             (although infinity would be equally appropriate).  Let's
             not bother to print it. */
          break;

        case svn_depth_empty:
          SVN_ERR(svn_cmdline_printf(pool, _("Depth: empty\n")));
          break;

        case svn_depth_files:
          SVN_ERR(svn_cmdline_printf(pool, _("Depth: files\n")));
          break;

        case svn_depth_immediates:
          SVN_ERR(svn_cmdline_printf(pool, _("Depth: immediates\n")));
          break;

        case svn_depth_infinity:
          /* Infinity is the default depth for working copy
             directories.  Let's not print it, it's not special enough
             to be worth mentioning.  */
          break;

        default:
          /* Other depths should never happen here. */
          SVN_ERR(svn_cmdline_printf(pool, _("Depth: INVALID\n")));
        }

      if (info->copyfrom_url)
        SVN_ERR(svn_cmdline_printf(pool, _("Copied From URL: %s\n"),
                                   info->copyfrom_url));

      if (SVN_IS_VALID_REVNUM(info->copyfrom_rev))
        SVN_ERR(svn_cmdline_printf(pool, _("Copied From Rev: %ld\n"),
                                   info->copyfrom_rev));
    }

  if (info->last_changed_author)
    SVN_ERR(svn_cmdline_printf(pool, _("Last Changed Author: %s\n"),
                               info->last_changed_author));

  if (SVN_IS_VALID_REVNUM(info->last_changed_rev))
    SVN_ERR(svn_cmdline_printf(pool, _("Last Changed Rev: %ld\n"),
                               info->last_changed_rev));

  if (info->last_changed_date)
    SVN_ERR(svn_cl__info_print_time(info->last_changed_date,
                                    _("Last Changed Date"), pool));

  if (info->has_wc_info)
    {
      if (info->text_time)
        SVN_ERR(svn_cl__info_print_time(info->text_time,
                                        _("Text Last Updated"), pool));

      if (info->checksum)
        SVN_ERR(svn_cmdline_printf(pool, _("Checksum: %s\n"),
                                   info->checksum));

      if (info->conflict_old)
        SVN_ERR(svn_cmdline_printf(pool,
                                   _("Conflict Previous Base File: %s\n"),
                                   svn_path_local_style(info->conflict_old,
                                                        pool)));

      if (info->conflict_wrk)
        SVN_ERR(svn_cmdline_printf
                (pool, _("Conflict Previous Working File: %s\n"),
                 svn_path_local_style(info->conflict_wrk, pool)));

      if (info->conflict_new)
        SVN_ERR(svn_cmdline_printf(pool,
                                   _("Conflict Current Base File: %s\n"),
                                   svn_path_local_style(info->conflict_new,
                                                        pool)));

      if (info->prejfile)
        SVN_ERR(svn_cmdline_printf(pool, _("Conflict Properties File: %s\n"),
                                   svn_path_local_style(info->prejfile,
                                                        pool)));
    }

  if (info->lock)
    {
      if (info->lock->token)
        SVN_ERR(svn_cmdline_printf(pool, _("Lock Token: %s\n"),
                                   info->lock->token));

      if (info->lock->owner)
        SVN_ERR(svn_cmdline_printf(pool, _("Lock Owner: %s\n"),
                                   info->lock->owner));

      if (info->lock->creation_date)
        SVN_ERR(svn_cl__info_print_time(info->lock->creation_date,
                                        _("Lock Created"), pool));

      if (info->lock->expiration_date)
        SVN_ERR(svn_cl__info_print_time(info->lock->expiration_date,
                                        _("Lock Expires"), pool));

      if (info->lock->comment)
        {
          int comment_lines;
          /* NOTE: The stdio will handle newline translation. */
          comment_lines = svn_cstring_count_newlines(info->lock->comment) + 1;
          SVN_ERR(svn_cmdline_printf(pool,
                                     Q_("Lock Comment (%i line):\n%s\n",
                                        "Lock Comment (%i lines):\n%s\n",
                                        comment_lines),
                                     comment_lines,
                                     info->lock->comment));
        }
    }

  if (info->changelist)
    SVN_ERR(svn_cmdline_printf(pool, _("Changelist: %s\n"),
                               info->changelist));

  if (info->tree_conflict)
    {
      const char *desc, *src_left_version, *src_right_version;

      SVN_ERR(svn_cl__get_human_readable_tree_conflict_description(
                &desc, info->tree_conflict, pool));
      src_left_version =
        svn_cl__node_description(info->tree_conflict->src_left_version, pool);
      src_right_version =
        svn_cl__node_description(info->tree_conflict->src_right_version, pool);

      svn_cmdline_printf(pool,
                         "%s: %s\n",
                         _("Tree conflict"),
                         desc);

      if (src_left_version)
        svn_cmdline_printf(pool,
                           "  %s: %s\n",
                           _("Source  left"), /* (1) */
                           src_left_version);
        /* (1): Sneaking in a space in "Source  left" so that it is the
         * same length as "Source right" while it still starts in the same
         * column. That's just a tiny tweak in the English `svn'. */

      if (src_right_version)
        svn_cmdline_printf(pool,
                           "  %s: %s\n",
                           _("Source right"),
                           src_right_version);
    }

  /* Print extra newline separator. */
  return svn_cmdline_printf(pool, "\n");
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__info(apr_getopt_t *os,
             void *baton,
             apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  apr_array_header_t *targets = NULL;
  apr_pool_t *subpool = svn_pool_create(pool);
  int i;
  svn_error_t *err;
  svn_boolean_t saw_a_problem = FALSE;
  svn_opt_revision_t peg_revision;
  svn_info_receiver_t receiver;

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));

  /* Add "." if user passed 0 arguments. */
  svn_opt_push_implicit_dot_target(targets, pool);

  if (opt_state->xml)
    {
      receiver = print_info_xml;

      /* If output is not incremental, output the XML header and wrap
         everything in a top-level element. This makes the output in
         its entirety a well-formed XML document. */
      if (! opt_state->incremental)
        SVN_ERR(svn_cl__xml_print_header("info", pool));
    }
  else
    {
      receiver = print_info;

      if (opt_state->incremental)
        return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                _("'incremental' option only valid in XML "
                                  "mode"));
    }

  if (opt_state->depth == svn_depth_unknown)
    opt_state->depth = svn_depth_empty;

  for (i = 0; i < targets->nelts; i++)
    {
      const char *truepath;
      const char *target = APR_ARRAY_IDX(targets, i, const char *);

      svn_pool_clear(subpool);
      SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

      /* Get peg revisions. */
      SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target, subpool));

      /* If no peg-rev was attached to a URL target, then assume HEAD. */
      if ((svn_path_is_url(target))
          && (peg_revision.kind == svn_opt_revision_unspecified))
        peg_revision.kind = svn_opt_revision_head;

      err = svn_client_info2(truepath,
                             &peg_revision, &(opt_state->start_revision),
                             receiver, NULL, opt_state->depth,
                             opt_state->changelists, ctx, subpool);

      if (err)
        {
          /* If one of the targets is a non-existent URL or wc-entry,
             don't bail out.  Just warn and move on to the next target. */
          if (err->apr_err == SVN_ERR_UNVERSIONED_RESOURCE
              || err->apr_err == SVN_ERR_ENTRY_NOT_FOUND)
            {
              SVN_ERR(svn_cmdline_fprintf
                      (stderr, subpool,
                       _("%s:  (Not a versioned resource)\n\n"),
                       svn_path_local_style(target, pool)));
            }
          else if (err->apr_err == SVN_ERR_RA_ILLEGAL_URL)
            {
              SVN_ERR(svn_cmdline_fprintf
                      (stderr, subpool,
                       _("%s:  (Not a valid URL)\n\n"),
                       svn_path_local_style(target, pool)));
            }
          else
            {
              return err;
            }

          svn_error_clear(err);
          err = NULL;
          saw_a_problem = TRUE;
        }
    }
  svn_pool_destroy(subpool);

  if (opt_state->xml && (! opt_state->incremental))
    SVN_ERR(svn_cl__xml_print_footer("info", pool));

  if (saw_a_problem)
    return svn_error_create(SVN_ERR_BASE, NULL, NULL);
  else
    return SVN_NO_ERROR;
}
