/*
 * cat.c:  implementation of the 'cat' command
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

#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_subst.h"
#include "svn_io.h"
#include "svn_time.h"
#include "svn_path.h"
#include "client.h"


/*** Code. ***/

svn_error_t *
svn_client_cat (svn_stream_t *out,
                const char *path_or_url,
                const svn_opt_revision_t *revision,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;
  void *ra_baton, *session;
  svn_revnum_t rev;
  svn_node_kind_t url_kind;
  svn_string_t *mime_type;
  svn_string_t *eol_style;
  svn_string_t *keywords;
  apr_hash_t *props;
  const char *auth_dir;
  const char *url;

  SVN_ERR (svn_client_url_from_path (&url, path_or_url, pool));
  if (! url)
    return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, NULL,
                              "'%s' has no URL", path_or_url);


  /* Get the RA library that handles URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, url, pool));

  SVN_ERR (svn_client__dir_if_wc (&auth_dir, "", pool));

  /* Open a repository session to the URL. */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, url, auth_dir, NULL,
                                        NULL, FALSE, FALSE,
                                        ctx, pool));

  /* Resolve REVISION into a real revnum. */
  SVN_ERR (svn_client__get_revision_number (&rev, ra_lib, session,
                                            revision, path_or_url, pool));
  if (! SVN_IS_VALID_REVNUM (rev))
    SVN_ERR (ra_lib->get_latest_revnum (session, &rev, pool));

  /* Decide if the URL is a file or directory. */
  SVN_ERR (ra_lib->check_path (&url_kind, session, "", rev, pool));

  if (url_kind == svn_node_dir)
    return svn_error_createf(SVN_ERR_CLIENT_IS_DIRECTORY, NULL,
                             "URL \"%s\" refers to directory", url);

  /* Grab some properties we need to know in order to figure out if anything 
     special needs to be done with this file. */
  SVN_ERR (ra_lib->get_file (session, "", rev, NULL, NULL, &props, pool));

  mime_type = apr_hash_get (props, SVN_PROP_MIME_TYPE, APR_HASH_KEY_STRING);
  eol_style = apr_hash_get (props, SVN_PROP_EOL_STYLE, APR_HASH_KEY_STRING);
  keywords = apr_hash_get (props, SVN_PROP_KEYWORDS, APR_HASH_KEY_STRING);

  /* FIXME: Someday we should also check the keywords property and if it's 
   * set do keyword expansion, but that's a fair amount of work. */

  if ((mime_type && svn_mime_type_is_binary (mime_type->data))
      || (! eol_style && ! keywords))
    {
      /* Either it's a binary file, or it's a text file with no special eol 
         style. */
      SVN_ERR (ra_lib->get_file (session, "", rev, out, NULL, NULL, pool));
    }
  else
    {
      svn_subst_keywords_t kw = { 0 };
      svn_subst_eol_style_t style;
      const char *tmp_filename;
      svn_stream_t *tmp_stream;
      apr_file_t *tmp_file;
      apr_status_t apr_err;
      apr_off_t off = 0;
      const char *eol = NULL;

      /* grab a temporary file to write the target to. */
      SVN_ERR (svn_io_open_unique_file (&tmp_file, &tmp_filename, "", ".tmp", 
                                        TRUE, pool));

      tmp_stream = svn_stream_from_aprfile (tmp_file, pool);

      SVN_ERR (ra_lib->get_file (session, "", rev, tmp_stream, 
                                 NULL, NULL, pool));

      /* rewind our stream. */
      apr_err = apr_file_seek (tmp_file, APR_SET, &off);
      if (apr_err)
        return svn_error_createf (apr_err, NULL, "seek failed on '%s'.",
                                  tmp_filename);

      if (eol_style)
        svn_subst_eol_style_from_value (&style, &eol, eol_style->data);

      if (keywords)
        {
          apr_hash_t *revprops;
          const char *url;

          SVN_ERR (ra_lib->rev_proplist(session, rev, &revprops, pool));

          if (svn_path_is_url (path_or_url))
            url = path_or_url;
          else
            {
              svn_wc_adm_access_t *adm_access;
              const svn_wc_entry_t *entry;

              SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path_or_url,
                                              FALSE, FALSE, pool));
              SVN_ERR (svn_wc_entry (&entry, path_or_url, adm_access, FALSE, 
                                     pool));
              if (entry && entry->url)
                url = entry->url;
              else
                url = NULL;
            }

          {
            svn_string_t *date = apr_hash_get (revprops,
                                               SVN_PROP_REVISION_DATE,
                                               APR_HASH_KEY_STRING);
            svn_string_t *author = apr_hash_get (revprops,
                                                 SVN_PROP_REVISION_AUTHOR,
                                                 APR_HASH_KEY_STRING);

            apr_time_t when = 0;

            if (date)
              SVN_ERR (svn_time_from_cstring (&when, date->data, pool));

            SVN_ERR (svn_subst_build_keywords
                     (&kw, keywords->data, 
                      apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, rev),
                      url,
                      when,
                      author ? author->data : NULL,
                      pool));
          }
        }

      SVN_ERR (svn_subst_translate_stream (tmp_stream, out, eol, FALSE, &kw,
                                           TRUE));

      SVN_ERR (svn_stream_close (tmp_stream));
    }

  return SVN_NO_ERROR;
}
