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

/* A helper function to convert the date property to something suitable for 
   printing out.  If LONG_P is TRUE, use the long format, otherwise use a 
   shorter one. */
static svn_error_t *
date_prop_to_human (const char **human, svn_boolean_t long_p, const char *prop,
                    apr_pool_t *pool)
{
  apr_time_t when;

  SVN_ERR (svn_time_from_cstring (&when, prop, pool));

  if (long_p)
    *human = svn_time_to_human_cstring (when, pool);
  else
    {
      apr_time_exp_t exploded_time;

      apr_time_exp_gmt (&exploded_time, when);

      *human = apr_psprintf (pool, "%04d-%02d-%02d %02d:%02d:%02dZ",
                             exploded_time.tm_year + 1900,
                             exploded_time.tm_mon + 1,
                             exploded_time.tm_mday,
                             exploded_time.tm_hour,
                             exploded_time.tm_min,
                             exploded_time.tm_sec);
    }

  return SVN_NO_ERROR;
}

/* A helper function to fill in a a keywords struct KW with the appropriate 
   contents for a particular file. */
static svn_error_t *
build_keyword_struct (svn_subst_keywords_t *kw,
                      const char *keywords_val,
                      const svn_revnum_t rev,
                      const char *path_or_url,
                      apr_hash_t *revprops,
                      apr_pool_t *pool)
{
  apr_array_header_t *keyword_tokens;
  const svn_string_t *author = NULL;
  const svn_string_t *date = NULL;
  int i;

  keyword_tokens = svn_cstring_split (keywords_val, " \t\v\n\b\r\f",
                                      TRUE /* chop */, pool);

  for (i = 0; i < keyword_tokens->nelts; ++i)
    {
      const char *keyword = APR_ARRAY_IDX (keyword_tokens, i, const char *);

      if ((! strcmp (keyword, SVN_KEYWORD_REVISION_LONG))
          || (! strcasecmp (keyword, SVN_KEYWORD_REVISION_SHORT)))
        {
          kw->revision = svn_string_createf (pool, "%" SVN_REVNUM_T_FMT, rev);
        }      
      else if ((! strcmp (keyword, SVN_KEYWORD_DATE_LONG))
               || (! strcasecmp (keyword, SVN_KEYWORD_DATE_SHORT)))
        {
          if (! date)
            date = apr_hash_get (revprops, SVN_PROP_REVISION_DATE,
                                 APR_HASH_KEY_STRING);
          if (date)
            {
              const char *human_date;

              SVN_ERR (date_prop_to_human (&human_date, TRUE, date->data, 
                                           pool));

              kw->date = svn_string_create (human_date, pool);
            }
        }
      else if ((! strcmp (keyword, SVN_KEYWORD_AUTHOR_LONG))
               || (! strcasecmp (keyword, SVN_KEYWORD_AUTHOR_SHORT)))
        {
          if (! author)
            author = apr_hash_get (revprops, SVN_PROP_REVISION_AUTHOR,
                                   APR_HASH_KEY_STRING);
          kw->author = author;
        }
      else if ((! strcmp (keyword, SVN_KEYWORD_URL_LONG))
               || (! strcasecmp (keyword, SVN_KEYWORD_URL_SHORT)))
        {
          if (svn_path_is_url (path_or_url))
            kw->url = svn_string_create (path_or_url, pool);
          else
            {
              svn_wc_adm_access_t *adm_access;
              const svn_wc_entry_t *entry;

              SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, path_or_url,
                                              FALSE, FALSE, pool));
              SVN_ERR (svn_wc_entry (&entry, path_or_url, adm_access, FALSE, 
                                     pool));
              if (entry && entry->url)
                kw->url = svn_string_create (entry->url, pool);
            }
        }
      else if ((! strcasecmp (keyword, SVN_KEYWORD_ID)))
        {
          const char *base_name = svn_path_basename (path_or_url, pool);
          const char *human_date = NULL;

          if (! author)
            author = apr_hash_get (revprops, SVN_PROP_REVISION_AUTHOR,
                                   APR_HASH_KEY_STRING);
          if (! date)
            date = apr_hash_get (revprops, SVN_PROP_REVISION_DATE,
                                 APR_HASH_KEY_STRING);

          if (date)
            SVN_ERR (date_prop_to_human (&human_date, FALSE, date->data, pool));

          kw->id = svn_string_createf (pool, "%s %" SVN_REVNUM_T_FMT " %s %s",
                                       base_name,
                                       rev,
                                       human_date ? human_date : "",
                                       author ? author->data : "");
        }
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

          SVN_ERR (ra_lib->rev_proplist(session, rev, &revprops, pool));

          SVN_ERR (build_keyword_struct (&kw, keywords->data, rev, path_or_url,
                                         revprops, pool));
        }

      SVN_ERR (svn_subst_translate_stream (tmp_stream, out, eol, FALSE, &kw,
                                           TRUE));

      SVN_ERR (svn_stream_close (tmp_stream));
    }

  return SVN_NO_ERROR;
}
