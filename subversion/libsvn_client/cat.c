/*
 * cat.c:  implementation of the 'cat' command
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include "client.h"


/*** Code. ***/

svn_error_t *
svn_client_cat (svn_stream_t* out,
                const char *url,
                const svn_opt_revision_t *revision,
                svn_client_auth_baton_t *auth_baton,
                apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;
  void *ra_baton, *session;
  svn_revnum_t rev;
  svn_node_kind_t url_kind;
  svn_string_t *mime_type;
  svn_string_t *eol_style;
  apr_hash_t *props;

  /* Get the RA library that handles URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, url, pool));

  /* Open a repository session to the URL. */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, url, NULL, NULL,
                                        NULL, FALSE, FALSE, FALSE,
                                        auth_baton, pool));

  /* Resolve REVISION into a real revnum. */
  SVN_ERR (svn_client__get_revision_number (&rev, ra_lib, session,
                                            revision, NULL, pool));
  if (! SVN_IS_VALID_REVNUM (rev))
    SVN_ERR (ra_lib->get_latest_revnum (session, &rev));

  /* Decide if the URL is a file or directory. */
  SVN_ERR (ra_lib->check_path (&url_kind, session, "", rev));

  if (url_kind == svn_node_dir)
    return svn_error_createf(SVN_ERR_CLIENT_IS_DIRECTORY, NULL,
                             "URL \"%s\" refers to directory", url);

  /* Grab some properties we need to know in order to figure out if anything 
     special needs to be done with this file. */
  SVN_ERR (ra_lib->get_file (session, "", rev, NULL, NULL, &props));

  mime_type = apr_hash_get (props, SVN_PROP_MIME_TYPE, APR_HASH_KEY_STRING);
  eol_style = apr_hash_get (props, SVN_PROP_EOL_STYLE, APR_HASH_KEY_STRING);

  /* FIXME: Someday we should also check the keywords property and if it's 
   * set do keyword expansion, but that's a fair amount of work. */

  if ((mime_type && svn_mime_type_is_binary (mime_type->data))
      || (! eol_style))
    {
      /* Either it's a binary file, or it's a text file with no special eol 
         style. */
      SVN_ERR (ra_lib->get_file(session, "", rev, out, NULL, NULL));
    }
  else
    {
      svn_subst_keywords_t *kw = NULL;
      svn_subst_eol_style_t style;
      const char *tmp_filename;
      svn_stream_t *tmp_stream;
      apr_file_t *tmp_file;
      apr_status_t apr_err;
      apr_off_t off = 0;
      const char *eol;

      /* grab a temporary file to write the target to. */
      SVN_ERR (svn_io_open_unique_file (&tmp_file, &tmp_filename, "", ".tmp", 
                                        TRUE, pool));

      tmp_stream = svn_stream_from_aprfile (tmp_file, pool);

      SVN_ERR (ra_lib->get_file(session, "", rev, tmp_stream, NULL, NULL));

      /* rewind our stream. */
      apr_err = apr_file_seek (tmp_file, APR_SET, &off);
      if (apr_err)
        return svn_error_createf (apr_err, NULL, "seek failed on '%s'.",
                                  tmp_filename);

      /* FIXME: set the kw to the appropriate value as found in the keywords 
         property before translating it. */

      svn_subst_eol_style_from_value (&style, &eol, eol_style->data);

      SVN_ERR (svn_subst_translate_stream (tmp_stream, out, eol, FALSE, kw,
                                           TRUE));

      SVN_ERR (svn_stream_close (tmp_stream));
    }

  SVN_ERR (ra_lib->close (session));

  return SVN_NO_ERROR;
}
