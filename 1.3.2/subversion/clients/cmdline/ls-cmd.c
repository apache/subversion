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
#include "svn_ebcdic.h"
#include "svn_utf.h"
#include "cl.h"

#include "svn_private_config.h"

#define AUTHOR_STR \
        "\x61\x75\x74\x68\x6f\x72"
        /* "author" */

#define COMMENT_STR \
        "\x63\x6f\x6d\x6d\x65\x6e\x74"
        /* "comment" */

#define COMMIT_STR \
        "\x63\x6f\x6d\x6d\x69\x74"
        /* "commit" */

#define CREATED_STR \
        "\x63\x72\x65\x61\x74\x65\x64"
        /* "created" */

#define DATE_STR \
        "\x64\x61\x74\x65"
        /* "date" */

#define DIR_STR \
        "\x64\x69\x72"
        /* "dir" */

#define ENTRY_STR \
        "\x65\x6e\x74\x72\x79"
        /* "entry" */

#define EXPIRES_STR \
        "\x65\x78\x70\x69\x72\x65\x73"
        /* "expires" */

#define FILE_STR \
        "\x66\x69\x6c\x65"
        /* "file" */

#define KIND_STR \
        "\x6b\x69\x6e\x64"
        /* "kind" */

#define LIST_STR \
        "\x6c\x69\x73\x74"
        /* "list" */

#define LISTS_STR \
        "\x6c\x69\x73\x74\x73"
        /* "lists" */

#define LOCK_STR \
        "\x6c\x6f\x63\x6b"
        /* "lock" */

#define NAME_STR \
        "\x6e\x61\x6d\x65"
        /* "name" */

#define OWNER_STR \
        "\x6f\x77\x6e\x65\x72"
        /* "owner" */

#define PATH_STR \
        "\x70\x61\x74\x68"
        /* "path" */

#define REVISION_STR \
        "\x72\x65\x76\x69\x73\x69\x6f\x6e"
        /* "revision" */

#define SIZE_STR \
        "\x73\x69\x7a\x65"

#define TOKEN_STR \
        "\x74\x6f\x6b\x65\x6e"
        /* "token" */

/*** Code. ***/

static svn_error_t *
print_dirents (apr_hash_t *dirents,
               apr_hash_t *locks,
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
          svn_lock_t *lock;

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

          sizestr = APR_PSPRINTF2 (subpool, "%" SVN_FILESIZE_T_FMT,
                                   dirent->size);
          lock = apr_hash_get (locks, utf8_entryname, item->klen);

          SVN_ERR (SVN_CMDLINE_PRINTF2
                   (subpool, "%7ld %-8.8s %c %10s %12s %s%s\n",
                    dirent->created_rev,
                    dirent->last_author ? dirent->last_author
                                        : SVN_UTF8_SPACE_STR \
                                          SVN_UTF8_QUESTION_STR \
                                          SVN_UTF8_SPACE_STR,
                    lock ? SVN_UTF8_O : SVN_UTF8_SPACE,
                    (dirent->kind == svn_node_file) ? sizestr : "",
                    utf8_timestr,
                    utf8_entryname,
                    (dirent->kind == svn_node_dir) ? SVN_UTF8_FSLASH_STR : ""));
        }
      else
        {
          SVN_ERR (SVN_CMDLINE_PRINTF2 (subpool, "%s%s\n", utf8_entryname,
                                        (dirent->kind == svn_node_dir)
                                        ? SVN_UTF8_FSLASH_STR : ""));
        }
    }

  svn_pool_destroy (subpool);
  
  return SVN_NO_ERROR;
}


static svn_error_t *
print_header_xml (apr_pool_t *pool)
{
  svn_stringbuf_t *sb = svn_stringbuf_create ("", pool);

  /* <?xml version="1.0" encoding="utf-8"?> */
  svn_xml_make_header (&sb, pool);
  
  /* "<lists>" */
  svn_xml_make_open_tag (&sb, pool, svn_xml_normal, LISTS_STR, NULL);
  
  return svn_cl__error_checked_fputs (sb->data, stdout);
}


static svn_error_t *
print_dirents_xml (apr_hash_t *dirents,
                   apr_hash_t *locks,
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
    svn_xml_make_open_tag (&sb, pool, svn_xml_normal, LIST_STR,
                           PATH_STR, path[0] == '\0' ? SVN_UTF8_DOT_STR : path,
                           NULL);
    SVN_ERR (svn_cl__error_checked_fputs (sb->data, stdout));
  }

  for (i = 0; i < array->nelts; ++i)
    {
      svn_stringbuf_t *sb;
      const char *utf8_entryname;
      svn_dirent_t *dirent;
      svn_sort__item_t *item;
      svn_lock_t *lock;

      svn_pool_clear (subpool);

      if (ctx->cancel_func)
        SVN_ERR (ctx->cancel_func (ctx->cancel_baton));
     
      item = &APR_ARRAY_IDX (array, i, svn_sort__item_t);

      utf8_entryname = item->key;

      dirent = apr_hash_get (dirents, utf8_entryname, item->klen);
      lock = apr_hash_get (locks, utf8_entryname, APR_HASH_KEY_STRING);

      sb = svn_stringbuf_create ("", subpool);

      /* "<entry ...>" */
      svn_xml_make_open_tag (&sb, subpool, svn_xml_normal, ENTRY_STR,
                             KIND_STR, svn_cl__node_kind_str (dirent->kind),
                             NULL);

      /* "<name>xxx</name> */
      svn_cl__xml_tagged_cdata (&sb, subpool, NAME_STR, utf8_entryname);

      /* "<size>xxx</size>" */
      if (dirent->kind == svn_node_file)
        {
          svn_cl__xml_tagged_cdata
            (&sb, subpool, SIZE_STR,
             apr_psprintf (subpool, "%" SVN_FILESIZE_T_FMT, dirent->size));
        }

      /* "<commit revision=...>" */
      svn_xml_make_open_tag (&sb, subpool, svn_xml_normal, COMMIT_STR,
                             REVISION_STR,
                             APR_PSPRINTF2 (subpool, "%ld",
                                            dirent->created_rev),
                             NULL);

      /* "<author>xxx</author>" */
      svn_cl__xml_tagged_cdata (&sb, subpool, AUTHOR_STR, dirent->last_author);

      /* "<date>xxx</date>" */
      svn_cl__xml_tagged_cdata (&sb, subpool, DATE_STR,
                                svn_time_to_cstring (dirent->time, subpool));

      /* "</commit>" */
      svn_xml_make_close_tag (&sb, subpool, COMMIT_STR);

      if (lock)
        {
          /* "<lock>" */
          svn_xml_make_open_tag (&sb, subpool, svn_xml_normal, LOCK_STR, NULL);

          svn_cl__xml_tagged_cdata (&sb, subpool, TOKEN_STR, lock->token);

          svn_cl__xml_tagged_cdata (&sb, subpool, OWNER_STR, lock->owner);

          svn_cl__xml_tagged_cdata (&sb, subpool, COMMENT_STR, lock->comment);

          svn_cl__xml_tagged_cdata (&sb, subpool, CREATED_STR,
                                    svn_time_to_cstring (lock->creation_date,
                                                         subpool));

          if (lock->expiration_date != 0)
            svn_cl__xml_tagged_cdata (&sb, subpool, EXPIRES_STR,
                                      svn_time_to_cstring
                                        (lock->expiration_date, subpool));

          /* "</lock>" */
          svn_xml_make_close_tag (&sb, subpool, LOCK_STR);
        }
      /* "</entry>" */
      svn_xml_make_close_tag (&sb, subpool, ENTRY_STR);

      SVN_ERR (svn_cl__error_checked_fputs (sb->data, stdout));
    }

  svn_pool_destroy (subpool);
  
  {
    /* "</list>" */
    svn_stringbuf_t *sb = svn_stringbuf_create ("", pool);
    svn_xml_make_close_tag (&sb, pool, LIST_STR);
    SVN_ERR (svn_cl__error_checked_fputs (sb->data, stdout));
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
print_footer_xml (apr_pool_t *pool)
{
  /* "</lists>" */
  svn_stringbuf_t *sb = svn_stringbuf_create ("", pool);
  svn_xml_make_close_tag (&sb, pool, LISTS_STR);
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
      apr_hash_t *locks;
      const char *target = ((const char **) (targets->elts))[i];
      const char *truepath;
      svn_opt_revision_t peg_revision;

      svn_pool_clear (subpool);
     
      SVN_ERR (svn_cl__check_cancel (ctx->cancel_baton));

      /* Get peg revisions. */
      SVN_ERR (svn_opt_parse_path (&peg_revision, &truepath, target,
                                   subpool));

      SVN_ERR (svn_client_ls3 (&dirents,
                               (opt_state->xml || opt_state->verbose)
                                 ? &locks : NULL,
                               truepath, &peg_revision,
                               &(opt_state->start_revision),
                               opt_state->recursive, ctx, subpool));

      if (opt_state->xml)
        SVN_ERR (print_dirents_xml (dirents, locks, truepath, ctx, subpool));
      else
        SVN_ERR (print_dirents (dirents, locks, opt_state->verbose, ctx, subpool));
    }

  svn_pool_destroy (subpool);
  
  if (opt_state->xml)
    {
      if (! opt_state->incremental)
        SVN_ERR (print_footer_xml (pool));
    }

  return SVN_NO_ERROR;
}
