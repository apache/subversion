/*
 * ra_pipe.c : the main RA module for piped network access.
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

#include "svn_ra.h"
#include "svn_xml.h"
#include "svn_time.h"
#include "svn_io.h"
#include "ra_pipe.h"


/** Structures **/
typedef struct 
{
  apr_file_t *input;
  apr_file_t *output;
  apr_pool_t *pool;
  const char *url;
} svn_ra_pipe__session_baton_t;


/** Helper functions **/

static svn_error_t *
receive_revnum (svn_revnum_t *revnum, apr_file_t *input, apr_pool_t *pool)
{
  /* ### This needs to either disappear, or work! */
  *revnum = 0;
  return SVN_NO_ERROR;
}


/** The RA plugin routines **/

static svn_error_t *
svn_ra_pipe__open (void **session_baton,
                   svn_stringbuf_t *repos_URL,
                   const svn_ra_callbacks_t *callbacks,
                   void *callback_baton,
                   apr_pool_t *pool)
{
  svn_ra_pipe__session_baton_t *sess = apr_pcalloc (pool, sizeof
                                                   (svn_ra_pipe__session_baton_t));
  apr_status_t apr_err = apr_file_open_stdin (&sess->input, pool);

  if (apr_err)
    return svn_error_create (apr_err, 0, NULL,
                             "ra_pipe: Couldn't open stdin\n");

  apr_err = apr_file_open_stdout (&sess->output, pool);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL,
                             "ra_pipe: Couldn't open stdout\n");
  sess->pool = pool;
  sess->url = apr_pstrdup (pool, repos_URL->data);

  *session_baton = sess;

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_pipe__close (void *session_baton)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  svn_stringbuf_t *buf = NULL;
  apr_status_t apr_err;

  svn_xml_make_header (&buf, sess->pool);

  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__REQUEST_TAG, "xmlns:S",
                         SVN_RA_PIPE__NAMESPACE, NULL);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                         SVN_RA_PIPE__CLOSE_SESSION_TAG, NULL);

  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL,
                             "Could not close ra_pipe session");

  apr_err = apr_file_close (sess->output);

  apr_err = apr_file_close (sess->input);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_pipe__get_latest_revnum (void *session_baton,
                                svn_revnum_t *latest_revnum)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  svn_stringbuf_t *buf = NULL;
  apr_status_t apr_err;

  svn_xml_make_header (&buf, sess->pool);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__REQUEST_TAG, "xmlns:S",
                         SVN_RA_PIPE__NAMESPACE, NULL);

  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                         SVN_RA_PIPE__LATEST_REVNUM_TAG, NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL,
                             "ra_pipe: Could not request latest revision "
                             "number");

  /* ### This doesn't actually get anything yet */
  SVN_ERR (receive_revnum (latest_revnum, sess->input, sess->pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
svn_ra_pipe__get_dated_revision (void *session_baton,
                                  svn_revnum_t *revision,
                                  apr_time_t tm)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  svn_stringbuf_t *buf = NULL;
  apr_status_t apr_err;

  svn_xml_make_header (&buf, sess->pool);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__REQUEST_TAG, "xmlns:S",
                         SVN_RA_PIPE__NAMESPACE, NULL);

  /* ### Should we use a different date format here? */
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                         SVN_RA_PIPE__LATEST_REVNUM_TAG,
                         SVN_RA_PIPE__ATT_DATE,
                         svn_time_to_nts (tm, sess->pool),
                         NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL,
                             "ra_pipe: Could not request latest revision "
                             "number");

  /* ### This doesn't actually get anything yet. */
  SVN_ERR (receive_revnum (revision, sess->input, sess->pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_pipe__get_commit_editor (void *session_baton,
                                const svn_delta_editor_t **editor,
                                void **edit_baton,
                                svn_revnum_t *new_rev,
                                const char **committed_date,
                                const char **committed_author,
                                const char *log_msg)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  svn_stringbuf_t *buf = NULL;
  svn_stringbuf_t *logbuf = NULL;
  apr_status_t apr_err;

  svn_xml_escape_nts (&logbuf, log_msg, sess->pool);

  svn_xml_make_header (&buf, sess->pool);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__REQUEST_TAG, "xmlns:S",
                         SVN_RA_PIPE__NAMESPACE, NULL);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__COMMIT_TAG, 
                         SVN_RA_PIPE__ATT_LOG_MSG,
                         logbuf->data,
                         NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__COMMIT_TAG);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL,
                             "ra_pipe: Could not send commit info");

  /* ### Need to read in the values of new_rev, committed_date, and
   * committed_author, in ra_local this is done in cleanup_commit, a hook
   * passed into the editor.  Does that mean we need to get it after we
   * send the xml? */

  svn_delta_get_xml_editor (svn_stream_from_aprfile (sess->output,
                                                     sess->pool),
                            editor, edit_baton, sess->pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_pipe__do_checkout (void *session_baton,
                          svn_revnum_t revision,
                          svn_boolean_t recurse,
                          const svn_delta_editor_t *editor,
                          void *edit_baton)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  const svn_delta_edit_fns_t *old_editor;
  void *old_baton;
  svn_stringbuf_t *buf = NULL;
  apr_status_t apr_err;

  svn_xml_make_header (&buf, sess->pool);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__REQUEST_TAG, "xmlns:S",
                         SVN_RA_PIPE__NAMESPACE, NULL);

  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                         SVN_RA_PIPE__CHECKOUT_TAG,
                         SVN_RA_PIPE__ATT_REV,
                         apr_psprintf (sess->pool,
                                       "%" SVN_REVNUM_T_FMT, revision),
                         SVN_RA_PIPE__ATT_RECURSE,
                         recurse ? "true" : "false",
                         NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL,
                             "ra_pipe: Could not request check_path");

  svn_delta_compat_wrap (&old_editor,
                         &old_baton,
                         editor,
                         edit_baton,
                         sess->pool);

  SVN_ERR (svn_delta_xml_auto_parse (svn_stream_from_aprfile (sess->input,
                                                               sess->pool),
                                     old_editor,
                                     old_baton,
                                     sess->url,
                                     revision,
                                     sess->pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_pipe__do_update (void *session_baton,
                        const svn_ra_reporter_t **reporter,
                        void **report_baton,
                        svn_revnum_t update_revision,
                        svn_stringbuf_t *update_target,
                        svn_boolean_t recurse,
                        const svn_delta_edit_fns_t *update_editor,
                        void *update_baton)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  return svn_ra_pipe__get_reporter (reporter,
                                    report_baton,
                                    sess->input,
                                    sess->output,
                                    sess->url,
                                    update_target,
                                    NULL,
                                    update_revision,
                                    recurse,
                                    update_editor,
                                    update_baton,
                                    TRUE,
                                    sess->pool);
}

static svn_error_t *
svn_ra_pipe__do_switch (void *session_baton,
                        const svn_ra_reporter_t **reporter,
                        void **report_baton,
                        svn_revnum_t switch_revision,
                        svn_stringbuf_t *switch_target,
                        svn_boolean_t recurse,
                        svn_stringbuf_t *switch_url,
                        const svn_delta_edit_fns_t *switch_editor,
                        void *switch_baton)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  return svn_ra_pipe__get_reporter (reporter,
                                    report_baton,
                                    sess->input,
                                    sess->output,
                                    sess->url,
                                    switch_target,
                                    switch_url->data,
                                    switch_revision,
                                    recurse,
                                    switch_editor,
                                    switch_baton,
                                    TRUE,
                                    sess->pool);
}

static svn_error_t *
svn_ra_pipe__do_status (void *session_baton,
                        const svn_ra_reporter_t **reporter,
                        void **report_baton,
                        svn_stringbuf_t *status_target,
                        svn_boolean_t recurse,
                        const svn_delta_edit_fns_t *status_editor,
                        void *status_baton)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  return svn_ra_pipe__get_reporter (reporter,
                                    report_baton,
                                    sess->input,
                                    sess->output,
                                    sess->url,
                                    status_target,
                                    NULL,
                                    SVN_INVALID_REVNUM,
                                    recurse,
                                    status_editor,
                                    status_baton,
                                    FALSE,
                                    sess->pool);
}

static svn_error_t *
svn_ra_pipe__get_log (void *session_baton,
                      const apr_array_header_t *paths,
                      svn_revnum_t start,
                      svn_revnum_t end,
                      svn_boolean_t discover_changed_paths,
                      svn_log_message_receiver_t receiver,
                      void *receiver_baton)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  svn_stringbuf_t *buf = NULL;
  apr_status_t apr_err;
  int i;

  svn_xml_make_header (&buf, sess->pool);

  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__REQUEST_TAG, "xmlns:S",
                         SVN_RA_PIPE__NAMESPACE, xNULL);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__GET_LOG_TAG,
                         SVN_RA_PIPE__ATT_STARTREV,
                         apr_psprintf (sess->pool,
                                       "%" SVN_REVNUM_T_FMT, start),
                         SVN_RA_PIPE__ATT_ENDREV,
                         apr_psprintf (sess->pool,
                                       "%" SVN_REVNUM_T_FMT, end),
                         SVN_RA_PIPE__ATT_CHANGED_PATHS,
                         discover_changed_paths ?  "true" : "false",
                         NULL);
  for (i = 0; i < paths->nelts; ++i)
    {
      svn_stringbuf_t *pathbuf = NULL;
      svn_xml_escape_nts (&pathbuf, ((const char **)paths->elts)[i],
                          sess->pool);
      svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                             SVN_RA_PIPE__PATH_TAG,
                             SVN_RA_PIPE__ATT_VALUE,
                             pathbuf->data,
                             NULL);
    }
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__GET_LOG_TAG);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL,
                             "ra_pipe: Could not request log");

  /* ### Need to get the response. */
  abort();

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_pipe__do_check_path (svn_node_kind_t *kind,
                            void *session_baton,
                            const char *path,
                            svn_revnum_t revision)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  svn_stringbuf_t *buf = NULL;
  svn_stringbuf_t *pathbuf = NULL;
  apr_status_t apr_err;

  svn_xml_escape_nts (&pathbuf, path, sess->pool);

  svn_xml_make_header (&buf, sess->pool);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__REQUEST_TAG, "xmlns:S",
                         SVN_RA_PIPE__NAMESPACE, NULL);

  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                         SVN_RA_PIPE__CHECK_PATH_TAG, 
                         SVN_RA_PIPE__ATT_PATH,
                         pathbuf->data,
                         SVN_RA_PIPE__ATT_REV,
                         apr_psprintf (sess->pool,
                                       "%" SVN_REVNUM_T_FMT, revision),
                         NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL,
                             "ra_pipe: Could not request check_path");

  /* ### Need to get the response. */
  abort();
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_pipe__get_file (void *session_baton,
                       const char *path,
                       svn_revnum_t revision,
                       svn_stream_t *stream,
                       svn_revnum_t *fetched_rev,
                       apr_hash_t **props)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  svn_stringbuf_t *buf = NULL;
  svn_stringbuf_t *pathbuf = NULL;
  apr_status_t apr_err;

  svn_xml_escape_nts (&pathbuf, path, sess->pool);

  svn_xml_make_header (&buf, sess->pool);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__REQUEST_TAG, "xmlns:S",
                         SVN_RA_PIPE__NAMESPACE, NULL);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                         SVN_RA_PIPE__GET_FILE_TAG,
                         SVN_RA_PIPE__ATT_PATH,
                         pathbuf->data,
                         SVN_RA_PIPE__ATT_REV,
                         apr_psprintf (sess->pool,
                                       "%" SVN_REVNUM_T_FMT, revision),
                         NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);


  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL,
                             "ra_pipe: Could not request get_file");

  abort();
  return SVN_NO_ERROR;
}


/** The ra_plugin **/

static const svn_ra_plugin_t ra_pipe_plugin = 
{
  "ra_pipe",
  "Module for accessing a repository via stdin/stdout.",
  svn_ra_pipe__open,
  svn_ra_pipe__close,
  svn_ra_pipe__get_latest_revnum,
  svn_ra_pipe__get_dated_revision,
  svn_ra_pipe__get_commit_editor,
  svn_ra_pipe__get_file,
  svn_ra_pipe__do_checkout,
  svn_ra_pipe__do_update,
  svn_ra_pipe__do_switch,
  svn_ra_pipe__do_status,
  svn_ra_pipe__get_log,
  svn_ra_pipe__do_check_path,
};

svn_error_t *
svn_ra_pipe_init (int abi_version,
                  apr_pool_t *pool,
                  apr_hash_t *hash)
{
  apr_hash_set (hash, "pipe", APR_HASH_KEY_STRING, &ra_pipe_plugin);
  return SVN_NO_ERROR;
}
