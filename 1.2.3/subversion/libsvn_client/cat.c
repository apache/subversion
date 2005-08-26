/*
 * cat.c:  implementation of the 'cat' command
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

#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_subst.h"
#include "svn_io.h"
#include "svn_time.h"
#include "svn_path.h"
#include "svn_props.h"
#include "client.h"

#include "svn_private_config.h"


/*** Code. ***/

/* Helper function to handle copying a potentially translated verison of BASE
   or WORKING revision of a file to an output stream. */
static svn_error_t *
cat_local_file (const char *path,
                svn_stream_t *output,
                svn_wc_adm_access_t *adm_access,
                const svn_opt_revision_t *revision,
                apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  svn_subst_keywords_t kw = { 0 };
  svn_subst_eol_style_t style;
  apr_hash_t *props;
  const char *base;
  svn_string_t *eol_style, *keywords, *special;
  const char *eol = NULL;
  svn_boolean_t local_mod = FALSE;
  apr_time_t tm;
  apr_file_t *input_file;
  svn_stream_t *input;

  SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));

  if (! entry)
    return svn_error_createf (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                              _("'%s' is not under version control "
                                "or doesn't exist"),
                              svn_path_local_style (path, pool));

  if (entry->kind != svn_node_file)
    return svn_error_createf(SVN_ERR_CLIENT_IS_DIRECTORY, NULL,
                             _("'%s' refers to a directory"), path);

  if (revision->kind != svn_opt_revision_working)
    {
      SVN_ERR (svn_wc_get_pristine_copy_path (path, &base, pool));
      SVN_ERR (svn_wc_get_prop_diffs (NULL, &props, path, adm_access, pool));
    }
  else
    {
      svn_wc_status2_t *status;
      
      base = path;
      SVN_ERR (svn_wc_prop_list (&props, path, adm_access, pool));
      SVN_ERR (svn_wc_status2 (&status, path, adm_access, pool));
      if (status->text_status != svn_wc_status_normal)
        local_mod = TRUE;
    }

  eol_style = apr_hash_get (props, SVN_PROP_EOL_STYLE,
                            APR_HASH_KEY_STRING);
  keywords = apr_hash_get (props, SVN_PROP_KEYWORDS,
                           APR_HASH_KEY_STRING);
  special = apr_hash_get (props, SVN_PROP_SPECIAL,
                          APR_HASH_KEY_STRING);
  
  if (eol_style)
    svn_subst_eol_style_from_value (&style, &eol, eol_style->data);
  
  if (local_mod && (! special))
    {
      /* Use the modified time from the working copy if
         the file */
      SVN_ERR (svn_io_file_affected_time (&tm, path, pool));
    }
  else
    {
      tm = entry->cmt_date;
    }

  if (keywords)
    {
      const char *fmt;
      const char *author;

      if (local_mod)
        {
          /* For locally modified files, we'll append an 'M'
             to the revision number, and set the author to
             "(local)" since we can't always determine the
             current user's username */
          fmt = "%ldM";
          author = _("(local)");
        }
      else
        {
          fmt = "%ld";
          author = entry->cmt_author;
        }
      
      SVN_ERR (svn_subst_build_keywords 
               (&kw, keywords->data, 
                apr_psprintf (pool, fmt, entry->cmt_rev),
                entry->url, tm, author, pool));
    }

  SVN_ERR (svn_io_file_open (&input_file, base,
                             APR_READ, APR_OS_DEFAULT, pool));
  input = svn_stream_from_aprfile (input_file, pool);

  SVN_ERR (svn_subst_translate_stream2 (input, output, eol, FALSE, &kw, TRUE, 
                                        pool));

  SVN_ERR (svn_stream_close (input));
  SVN_ERR (svn_io_file_close (input_file, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_cat2 (svn_stream_t *out,
                 const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  svn_revnum_t rev;
  svn_node_kind_t url_kind;
  svn_string_t *eol_style;
  svn_string_t *keywords;
  apr_hash_t *props;
  const char *url;

  if (! svn_path_is_url (path_or_url)
      && (peg_revision->kind == svn_opt_revision_base
          || peg_revision->kind == svn_opt_revision_committed
          || peg_revision->kind == svn_opt_revision_unspecified)
      && (revision->kind == svn_opt_revision_base
          || revision->kind == svn_opt_revision_committed
          || revision->kind == svn_opt_revision_unspecified))
    {
      svn_wc_adm_access_t *adm_access;
    
      SVN_ERR (svn_wc_adm_open3 (&adm_access, NULL,
                                 svn_path_dirname (path_or_url, pool), FALSE,
                                 0, ctx->cancel_func, ctx->cancel_baton,
                                 pool));

      SVN_ERR (cat_local_file (path_or_url, out, adm_access, revision, pool));

      SVN_ERR (svn_wc_adm_close (adm_access));

      return SVN_NO_ERROR;
    }

  /* Get an RA plugin for this filesystem object. */
  SVN_ERR (svn_client__ra_session_from_path (&ra_session, &rev,
                                             &url, path_or_url, peg_revision,
                                             revision, ctx, pool));

  /* Make sure the object isn't a directory. */
  SVN_ERR (svn_ra_check_path (ra_session, "", rev, &url_kind, pool));
  if (url_kind == svn_node_dir)
    return svn_error_createf(SVN_ERR_CLIENT_IS_DIRECTORY, NULL,
                             _("URL '%s' refers to a directory"), url);

  /* Grab some properties we need to know in order to figure out if anything 
     special needs to be done with this file. */
  SVN_ERR (svn_ra_get_file (ra_session, "", rev, NULL, NULL, &props, pool));

  eol_style = apr_hash_get (props, SVN_PROP_EOL_STYLE, APR_HASH_KEY_STRING);
  keywords = apr_hash_get (props, SVN_PROP_KEYWORDS, APR_HASH_KEY_STRING);

  if (! eol_style && ! keywords)
    {
      /* It's a file with no special eol style or keywords. */
      SVN_ERR (svn_ra_get_file (ra_session, "", rev, out, NULL, NULL, pool));
    }
  else
    {
      svn_subst_keywords_t kw = { 0 };
      svn_subst_eol_style_t style;
      const char *temp_dir;
      const char *tmp_filename;
      svn_stream_t *tmp_stream;
      apr_file_t *tmp_file;
      apr_status_t apr_err;
      apr_off_t off = 0;
      const char *eol = NULL;

      /* grab a temporary file to write the target to. */
      SVN_ERR (svn_io_temp_dir (&temp_dir, pool));
      SVN_ERR (svn_io_open_unique_file (&tmp_file, &tmp_filename,
                 svn_path_join (temp_dir, "tmp", pool), ".tmp",
                 TRUE, pool));

      tmp_stream = svn_stream_from_aprfile (tmp_file, pool);

      SVN_ERR (svn_ra_get_file (ra_session, "", rev, tmp_stream, 
                                NULL, NULL, pool));

      /* rewind our stream. */
      apr_err = apr_file_seek (tmp_file, APR_SET, &off);
      if (apr_err)
        return svn_error_wrap_apr (apr_err, _("Can't seek in '%s'"),
                                   svn_path_local_style (tmp_filename, pool));

      if (eol_style)
        svn_subst_eol_style_from_value (&style, &eol, eol_style->data);

      if (keywords)
        {
          svn_string_t *cmt_rev, *cmt_date, *cmt_author;
          apr_time_t when = 0;

          cmt_rev = apr_hash_get (props, SVN_PROP_ENTRY_COMMITTED_REV,
                                  APR_HASH_KEY_STRING);
          cmt_date = apr_hash_get (props, SVN_PROP_ENTRY_COMMITTED_DATE,
                                   APR_HASH_KEY_STRING);
          cmt_author = apr_hash_get (props, SVN_PROP_ENTRY_LAST_AUTHOR,
                                     APR_HASH_KEY_STRING);
          if (cmt_date)
            SVN_ERR (svn_time_from_cstring (&when, cmt_date->data, pool));

          SVN_ERR (svn_subst_build_keywords
                   (&kw, keywords->data, 
                    cmt_rev->data,
                    url,
                    when,
                    cmt_author ? cmt_author->data : NULL,
                    pool));
        }

      SVN_ERR (svn_subst_translate_stream2 (tmp_stream, out, eol, FALSE, &kw,
                                            TRUE, pool));

      SVN_ERR (svn_stream_close (tmp_stream));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_cat (svn_stream_t *out,
                const char *path_or_url,
                const svn_opt_revision_t *revision,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_cat2 (out, path_or_url, revision, revision,
                          ctx, pool);
}
