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
#include "svn_xml.h"
#include "cl.h"

#include "svn_private_config.h"

/*** Code. ***/

static svn_error_t *
print_dirents (apr_hash_t *dirents,
               svn_boolean_t verbose,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  apr_array_header_t *array;
  int i;
  apr_pool_t *subpool = svn_pool_create (pool); 

  array = svn_sort__hash (dirents, svn_sort_compare_items_as_paths, pool);
  
  for (i = 0; i < array->nelts; ++i)
    {
      const char *utf8_entryname;
      svn_dirent_t *dirent;
      svn_sort__item_t *item;

      svn_pool_clear (subpool);

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
          SVN_ERR (svn_utf_cstring_to_utf8 (&utf8_timestr, timestr, subpool));

          sizestr = apr_psprintf (subpool, "%" SVN_FILESIZE_T_FMT,
                                  dirent->size);

          SVN_ERR (svn_cmdline_printf
                   (subpool, "%7ld %-8.8s %10s %12s %s%s\n",
                    dirent->created_rev,
                    dirent->last_author ? dirent->last_author : " ? ",
                    (dirent->kind == svn_node_file) ? sizestr : "",
                    utf8_timestr,
                    utf8_entryname,
                    (dirent->kind == svn_node_dir) ? "/" : ""));
        }
      else
        {
          SVN_ERR (svn_cmdline_printf (subpool, "%s%s\n", utf8_entryname,
                                       (dirent->kind == svn_node_dir)
                                       ? "/" : ""));
        }
    }

  svn_pool_destroy (subpool);
  
  return SVN_NO_ERROR;
}


static const char *
kind_str (svn_node_kind_t kind)
{
  switch (kind)
    {
    case svn_node_dir:
      return "dir";
    case svn_node_file:
      return "file";
    default:
      return "";
    }
}


static svn_error_t *
print_header_xml (apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create ("", pool);

  /* <?xml version="1.0" encoding="utf-8"?> */
  svn_xml_make_header (&sb, pool);
  
  /* "<lists>" */
  svn_xml_make_open_tag (&sb, pool, svn_xml_normal, "lists", NULL);
  
  return svn_cl__error_checked_fputs (sb->data, stdout);
}


static svn_error_t *
print_dirents_xml (apr_hash_t *dirents,
                   const char *path,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  apr_array_header_t *array;
  int i;
  apr_pool_t *subpool = svn_pool_create (pool); 

  array = svn_sort__hash (dirents, svn_sort_compare_items_as_paths, pool);

  {
    /* "<list path=...>" */
    svn_stringbuf_t *sb = svn_stringbuf_create ("", pool);
    svn_xml_make_open_tag (&sb, pool, svn_xml_normal, "list",
                           "path", path[0] == '\0' ? "." : path,
                           NULL);
    SVN_ERR (svn_cl__error_checked_fputs (sb->data, stdout));
  }

  for (i = 0; i < array->nelts; ++i)
    {
      svn_stringbuf_t *sb;
      const char *utf8_entryname;
      svn_dirent_t *dirent;
      svn_sort__item_t *item;

      svn_pool_clear (subpool);

      if (ctx->cancel_func)
        SVN_ERR (ctx->cancel_func (ctx->cancel_baton));
     
      item = &APR_ARRAY_IDX (array, i, svn_sort__item_t);

      utf8_entryname = item->key;

      dirent = apr_hash_get (dirents, utf8_entryname, item->klen);

      sb = svn_stringbuf_create ("", subpool);

      /* "<entry ...>" */
      svn_xml_make_open_tag (&sb, subpool, svn_xml_normal, "entry",
                             "kind", kind_str (dirent->kind),
                             NULL);

      /* "<name>xxx</name> */
      svn_xml_make_open_tag (&sb, subpool, svn_xml_protect_pcdata, "name",
                             NULL);
      svn_xml_escape_cdata_cstring (&sb, utf8_entryname, subpool);
      svn_xml_make_close_tag (&sb, subpool, "name");

      /* "<size>xxx</size>" */
      if (dirent->kind == svn_node_file)
        {
          svn_xml_make_open_tag (&sb, subpool, svn_xml_protect_pcdata, "size",
                                 NULL);
          svn_xml_escape_cdata_cstring
            (&sb, apr_psprintf (subpool, "%" SVN_FILESIZE_T_FMT, dirent->size),
             subpool);
          svn_xml_make_close_tag (&sb, subpool, "size");
        }

      /* "<commit revision=...>" */
      svn_xml_make_open_tag (&sb, subpool, svn_xml_normal, "commit",
                             "revision",
                             apr_psprintf (subpool, "%ld",
                                           dirent->created_rev),
                             NULL);
      if (dirent->last_author)
        {
          /* "<author>xxx</author>" */
          svn_xml_make_open_tag (&sb, subpool, svn_xml_protect_pcdata,
                                 "author", NULL);
          svn_xml_escape_cdata_cstring (&sb, dirent->last_author, subpool);
          svn_xml_make_close_tag (&sb, subpool, "author");
        }
      /* "<date>xxx</date>" */
      svn_xml_make_open_tag (&sb, subpool, svn_xml_protect_pcdata, "date",
                             NULL);
      svn_xml_escape_cdata_cstring
        (&sb, svn_time_to_cstring (dirent->time, subpool), subpool);
      svn_xml_make_close_tag (&sb, subpool, "date");
      /* "</commit>" */
      svn_xml_make_close_tag (&sb, subpool, "commit");

      /* "</entry>" */
      svn_xml_make_close_tag (&sb, subpool, "entry");

      SVN_ERR (svn_cl__error_checked_fputs (sb->data, stdout));
    }

  svn_pool_destroy (subpool);
  
  {
    /* "</list>" */
    svn_stringbuf_t *sb = svn_stringbuf_create ("", pool);
    svn_xml_make_close_tag (&sb, pool, "list");
    SVN_ERR (svn_cl__error_checked_fputs (sb->data, stdout));
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
print_footer_xml (apr_pool_t *pool)
{
  /* "</lists>" */
  svn_stringbuf_t *sb = svn_stringbuf_create ("", pool);
  svn_xml_make_close_tag (&sb, pool, "lists");
  return svn_cl__error_checked_fputs (sb->data, stdout);
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

  SVN_ERR (svn_opt_args_to_target_array2 (&targets, os, 
                                          opt_state->targets, pool));

  /* Add "." if user passed 0 arguments */
  svn_opt_push_implicit_dot_target (targets, pool);

  if (opt_state->xml)
    {
      /* The XML output contains all the information, so "--verbose"
        does not apply. */
      if (opt_state->verbose)
        return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'verbose' option invalid in XML mode"));

      /* If output is not incremental, output the XML header and wrap
         everything in a top-level element. This makes the output in
         its entirety a well-formed XML document. */
      if (! opt_state->incremental)
        SVN_ERR (print_header_xml (pool));
    }
  else
    {
      if (opt_state->incremental)
        return svn_error_create (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 _("'incremental' option only valid in XML "
                                   "mode"));
    }

  /* For each target, try to list it. */
  for (i = 0; i < targets->nelts; i++)
    {
      apr_hash_t *dirents;
      const char *target = ((const char **) (targets->elts))[i];
      const char *truepath;
      svn_opt_revision_t peg_revision;

      svn_pool_clear (subpool);
     
      SVN_ERR (svn_cl__check_cancel (ctx->cancel_baton));

      /* Get peg revisions. */
      SVN_ERR (svn_opt_parse_path (&peg_revision, &truepath, target,
                                   subpool));
      SVN_ERR (svn_client_ls2 (&dirents, truepath, &peg_revision,
                               &(opt_state->start_revision),
                               opt_state->recursive, ctx, subpool));

      if (opt_state->xml)
        SVN_ERR (print_dirents_xml (dirents, truepath, ctx, subpool));
      else
        SVN_ERR (print_dirents (dirents, opt_state->verbose, ctx, subpool));
    }

  svn_pool_destroy (subpool);
  
  if (opt_state->xml)
    {
      if (! opt_state->incremental)
        SVN_ERR (print_footer_xml (pool));
    }

  return SVN_NO_ERROR;
}
