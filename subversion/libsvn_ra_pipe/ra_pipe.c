#include <svn_ra.h>
#include <svn_xml.h>
#include <svn_time.h>
#include <svn_io.h>


/** XML Stuff for this file (needs to go somewhere public, so libsvn_server
 * can see it.) */
#define SVN_RA_PIPE__REQUEST_TAG        "svn:pipe:request"
#define SVN_RA_PIPE__CLOSE_SESSION_TAG  "svn:pipe:close"
#define SVN_RA_PIPE__LATEST_REVNUM_TAG  "svn:pipe:latest-revnum"
#define SVN_RA_PIPE__GET_LOG_TAG        "svn:pipe:get-log"
#define SVN_RA_PIPE__PATH_TAG           "svn:pipe:path"
#define SVN_RA_PIPE__CHECK_PATH_TAG     "svn:pipe:check-path"
#define SVN_RA_PIPE__GET_FILE_TAG       "svn:pipe:get-file"
#define SVN_RA_PIPE__CHECKOUT_TAG       "svn:pipe:checkout"
#define SVN_RA_PIPE__COMMIT_TAG         "svn:pipe:commit"

#define SVN_RA_PIPE__ATT_DATE           "date"
#define SVN_RA_PIPE__ATT_REV            "rev"
#define SVN_RA_PIPE__ATT_STARTREV       "start-revision"
#define SVN_RA_PIPE__ATT_ENDREV         "end-revision"
#define SVN_RA_PIPE__ATT_CHANGED_PATHS  "changed-paths"
#define SVN_RA_PIPE__ATT_VALUE          "value"
#define SVN_RA_PIPE__ATT_PATH           "path"
#define SVN_RA_PIPE__ATT_RECURSE        "recurse"
#define SVN_RA_PIPE__ATT_LOG_MSG        "log-msg"


/** Structures **/
typedef struct 
{
  apr_file_t *input;
  apr_file_t *output;
  apr_pool_t *pool;
  svn_stringbuf_t *url;
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
    return svn_error_create (apr_err, 0, NULL, pool,
                             "ra_pipe: Couldn't open stdin\n");

  apr_err = apr_file_open_stdout (&sess->output, pool);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, pool,
                             "ra_pipe: Couldn't open stdout\n");
  sess->pool = pool;
  sess->url = svn_stringbuf_dup (repos_URL, pool);

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
                         SVN_RA_PIPE__REQUEST_TAG, NULL);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                         SVN_RA_PIPE__CLOSE_SESSION_TAG, NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__CLOSE_SESSION_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, sess->pool,
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
                         SVN_RA_PIPE__REQUEST_TAG, NULL);

  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                         SVN_RA_PIPE__LATEST_REVNUM_TAG, NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, sess->pool,
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
                         SVN_RA_PIPE__REQUEST_TAG, NULL);

  /* ### Should we use a different date format here? */
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                         SVN_RA_PIPE__LATEST_REVNUM_TAG,
                         SVN_RA_PIPE__ATT_DATE,
                         svn_stringbuf_create (svn_time_to_nts (tm, sess->pool),
                                               sess->pool),
                         NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, sess->pool,
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
                                svn_stringbuf_t *log_msg)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  svn_stringbuf_t *buf = NULL;
  svn_stringbuf_t *logbuf = NULL;
  apr_status_t apr_err;

  svn_xml_escape_stringbuf (&logbuf, log_msg, sess->pool);

  svn_xml_make_header (&buf, sess->pool);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__REQUEST_TAG, NULL);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__COMMIT_TAG, 
                         SVN_RA_PIPE__ATT_LOG_MSG,
                         logbuf,
                         NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__COMMIT_TAG);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, sess->pool,
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
                          const svn_delta_edit_fns_t *editor,
                          void *edit_baton)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  svn_stringbuf_t *buf = NULL;
  apr_status_t apr_err;

  svn_xml_make_header (&buf, sess->pool);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__REQUEST_TAG, NULL);

  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                         SVN_RA_PIPE__CHECKOUT_TAG,
                         SVN_RA_PIPE__ATT_REV,
                         svn_stringbuf_create (apr_psprintf (sess->pool,
                                                             "%ld",
                                                             revision),
                                               sess->pool),
                         SVN_RA_PIPE__ATT_RECURSE,
                         svn_stringbuf_create (recurse ? "true" : "false",
                                               sess->pool),
                         NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, sess->pool,
                             "ra_pipe: Could not request check_path");

  SVN_ERR (svn_delta_xml_auto_parse (svn_stream_from_aprfile (sess->input,
                                                               sess->pool),
                                     editor,
                                     edit_baton,
                                     sess->url->data,
                                     1, /* XXX: revision,*/
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
  svn_revnum_t target_revnum;
  if (! SVN_IS_VALID_REVNUM(update_revision))
    SVN_ERR (svn_ra_pipe__get_latest_revnum (sess, &target_revnum));
  else
    target_revnum = update_revision;

  /* ### Need to create a reporter that will spit out some xml stuff. */
  *reporter = NULL;
  *report_baton = NULL;

  /* ### Throw in the stuff to get the server started here. Basically, we need
   * to echo all of these arguments over to the server. */
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
                           sess->pool, "ra_pipe not implemented yet");
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
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
                           sess->pool, "ra_pipe not implemented yet");
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
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
                           sess->pool, "ra_pipe not implemented yet");
}

static svn_error_t *
svn_ra_pipe__do_merge (void *session_baton, 
                       const svn_ra_reporter_t **reporter,
                       void **report_baton,
                       const char *start_path,
                       svn_revnum_t start_revision,
                       const char *end_path,
                       svn_revnum_t end_revision,
                       const char *merge_target,
                       const svn_delta_edit_fns_t *setup_editor,
                       void *setup_edit_baton,
                       const svn_delta_edit_fns_t *finish_editor,
                       void *finish_edit_baton)
{
  svn_ra_pipe__session_baton_t *sess = session_baton;
  return svn_error_create (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
                           sess->pool, "ra_pipe not implemented yet");
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
                         SVN_RA_PIPE__REQUEST_TAG, NULL);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_normal,
                         SVN_RA_PIPE__GET_LOG_TAG,
                         SVN_RA_PIPE__ATT_STARTREV,
                         svn_stringbuf_create (apr_psprintf (sess->pool,
                                                             "%ld",
                                                             start),
                                               sess->pool),
                         SVN_RA_PIPE__ATT_ENDREV,
                         svn_stringbuf_create (apr_psprintf (sess->pool,
                                                             "%ld",
                                                             end),
                                               sess->pool),
                         SVN_RA_PIPE__ATT_CHANGED_PATHS,
                         svn_stringbuf_create
                            ( discover_changed_paths ?  "true" : "false",
                              sess->pool),
                         NULL);
  for (i = 0; i < paths->nelts; ++i)
    {
      svn_stringbuf_t *pathbuf = NULL;
      svn_xml_escape_stringbuf (&pathbuf, ((svn_stringbuf_t **)paths->elts)[i],
                                sess->pool);
      svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                             SVN_RA_PIPE__PATH_TAG,
                             SVN_RA_PIPE__ATT_VALUE,
                             pathbuf,
                             NULL);
    }
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__GET_LOG_TAG);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, sess->pool,
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
                         SVN_RA_PIPE__REQUEST_TAG, NULL);

  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                         SVN_RA_PIPE__CHECK_PATH_TAG, 
                         SVN_RA_PIPE__ATT_PATH,
                         pathbuf,
                         SVN_RA_PIPE__ATT_REV,
                         svn_stringbuf_create (apr_psprintf (sess->pool,
                                                             "%ld",
                                                             revision),
                                               sess->pool),
                         NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);

  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, sess->pool,
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
                         SVN_RA_PIPE__REQUEST_TAG, NULL);
  svn_xml_make_open_tag (&buf, sess->pool, svn_xml_self_closing,
                         SVN_RA_PIPE__GET_FILE_TAG,
                         SVN_RA_PIPE__ATT_PATH,
                         pathbuf,
                         SVN_RA_PIPE__ATT_REV,
                         svn_stringbuf_create (apr_psprintf (sess->pool,
                                                             "%ld",
                                                             revision),
                                               sess->pool),
                         NULL);
  svn_xml_make_close_tag (&buf, sess->pool, SVN_RA_PIPE__REQUEST_TAG);


  apr_err = apr_file_write_full (sess->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, sess->pool,
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
  svn_ra_pipe__do_merge,
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
