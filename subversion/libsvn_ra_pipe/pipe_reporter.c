/*
 * pipe_reporter.c : the reporter vtable used by ra_pipe to report changes
 * to a working copy.
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

#include "ra_pipe.h"
#include "svn_ra.h"
#include "svn_xml.h"




/** Structures **/
typedef struct 
{
  apr_file_t *input;
  apr_file_t *output;

  const svn_delta_edit_fns_t *editor;
  void *edit_baton;

  const char *url;
  
  apr_pool_t *pool;
} svn_ra_pipe__report_baton_t;


static svn_error_t *
svn_ra_pipe__set_path (void *report_baton,
                       const char *path,
                       svn_revnum_t revision)
{
  svn_ra_pipe__report_baton_t *baton = report_baton;
  apr_status_t apr_err;
  svn_stringbuf_t *buf = NULL;
  svn_stringbuf_t *qpath = NULL;

  svn_xml_escape_nts (&qpath, path, baton->pool);

  svn_xml_make_open_tag (&buf, baton->pool, svn_xml_normal,
                         SVN_RA_PIPE__ENTRY_TAG, SVN_RA_PIPE__ATT_REV,
                         apr_psprintf (baton->pool, "%" SVN_REVNUM_T_FMT,
                                       revision),
                         NULL);
  svn_stringbuf_appendstr (buf, qpath);
  svn_xml_make_close_tag (&buf, baton->pool, SVN_RA_PIPE__ENTRY_TAG);

  apr_err = apr_file_write_full (baton->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, baton->pool,
                             "Could not write an entry to the report");

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_pipe__delete_path (void *report_baton, const char *path)
{
  svn_ra_pipe__report_baton_t *baton = report_baton;
  apr_status_t apr_err;
  svn_stringbuf_t *buf = NULL;
  svn_stringbuf_t *qpath = NULL;

  svn_xml_escape_nts (&qpath, path, baton->pool);

  svn_xml_make_open_tag (&buf, baton->pool, svn_xml_normal,
                         SVN_RA_PIPE__MISSING_TAG, NULL);
  svn_stringbuf_appendstr (buf, qpath);
  svn_xml_make_close_tag (&buf, baton->pool, SVN_RA_PIPE__MISSING_TAG);

  apr_err = apr_file_write_full (baton->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, baton->pool,
                             "Could not delete an entry to the report");

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_pipe__link_path (void *report_baton,
                        const char *path,
                        const char *url,
                        svn_revnum_t revision)
{
  svn_ra_pipe__report_baton_t *baton = report_baton;
  apr_status_t apr_err;
  svn_stringbuf_t *buf = NULL;
  svn_stringbuf_t *qpath = NULL;
  svn_stringbuf_t *linkpath = NULL;

  svn_xml_escape_nts (&qpath, path, baton->pool);
  svn_xml_escape_nts (&linkpath, url, baton->pool);

  svn_xml_make_open_tag (&buf, baton->pool, svn_xml_normal,
                         SVN_RA_PIPE__ENTRY_TAG, SVN_RA_PIPE__ATT_REV,
                         apr_psprintf (baton->pool, "%" SVN_REVNUM_T_FMT,
                                       revision),
                         SVN_RA_PIPE__ATT_URL,
                         linkpath->data,
                         NULL);
  svn_stringbuf_appendstr (buf, qpath);
  svn_xml_make_close_tag (&buf, baton->pool, SVN_RA_PIPE__ENTRY_TAG);

  apr_err = apr_file_write_full (baton->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, baton->pool,
                             "Could not write an entry to the report");
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_pipe__finish_report (void *report_baton)
{
  svn_ra_pipe__report_baton_t *baton = report_baton;
  apr_status_t apr_err;
  svn_stringbuf_t *buf = NULL;

  svn_xml_make_close_tag (&buf, baton->pool, SVN_RA_PIPE__REPORT_TAG);
  apr_err = apr_file_write_full (baton->output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, baton->pool,
                             "Could not finish the report");
  
  /* Okay, we've sent our report to the server, now we can expect an xml
   * update back. */
  return svn_delta_xml_auto_parse (svn_stream_from_aprfile (baton->input,
                                                            baton->pool),
                                   baton->editor,
                                   baton->edit_baton,
                                   baton->url,
                                   SVN_INVALID_REVNUM,
                                   baton->pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_pipe__abort_report (void *report_baton)
{
  /* ### Should we just close the pipe? Or should we signal a failure
   * somehow first? Don't know what to do here. */

  return SVN_NO_ERROR;
}

static const svn_ra_reporter_t ra_pipe_reporter = 
{
  svn_ra_pipe__set_path,
  svn_ra_pipe__delete_path,
  svn_ra_pipe__link_path,
  svn_ra_pipe__finish_report,
  svn_ra_pipe__abort_report
};

svn_error_t *
svn_ra_pipe__get_reporter (const svn_ra_reporter_t **reporter,
                           void **report_baton,
                           apr_file_t *input,
                           apr_file_t *output,
                           const char *url,
                           svn_stringbuf_t *target,
                           const char *dst_path,
                           svn_revnum_t revision,
                           svn_boolean_t recurse,
                           const svn_delta_edit_fns_t *editor,
                           void *edit_baton,
                           svn_boolean_t fetch_text,
                           apr_pool_t *pool)
{
  svn_ra_pipe__report_baton_t *baton = apr_palloc (pool, sizeof (*baton));
  apr_status_t apr_err;
  svn_stringbuf_t *buf = NULL;

  baton->pool = pool;
  baton->input = input;
  baton->output = output;
  baton->editor = editor;
  baton->edit_baton = edit_baton;
  baton->url = url;

  svn_xml_make_open_tag (&buf, pool, svn_xml_normal,
                         SVN_RA_PIPE__REPORT_TAG, NULL);

  if (SVN_IS_VALID_REVNUM (revision))
    {
      /* ### Set the target revnum */
      svn_xml_make_open_tag (&buf, pool, svn_xml_normal,
                             SVN_RA_PIPE__TARGET_REVISION_TAG, NULL);
      svn_stringbuf_appendcstr (buf, apr_psprintf (pool,
                                                   "%" SVN_REVNUM_T_FMT,
                                                   revision));
      svn_xml_make_close_tag (&buf, pool, SVN_RA_PIPE__TARGET_REVISION_TAG);
    }

  if (target && target->data)
    {
      svn_stringbuf_t *escaped_target = NULL;
      svn_xml_escape_stringbuf (&escaped_target, target, pool);
      svn_xml_make_open_tag (&buf, pool, svn_xml_normal,
                             SVN_RA_PIPE__UPDATE_TARGET_TAG, NULL);
      svn_stringbuf_appendstr (buf, escaped_target);
      svn_xml_make_close_tag (&buf, pool,
                                SVN_RA_PIPE__UPDATE_TARGET_TAG);
    }

  if (dst_path)
    {
      svn_stringbuf_t *escaped_dst_path = NULL;
      svn_xml_escape_nts (&escaped_dst_path, dst_path, pool);
      svn_xml_make_open_tag (&buf, pool, svn_xml_normal,
                             SVN_RA_PIPE__DST_PATH_TAG, NULL);
      svn_stringbuf_appendstr (buf, escaped_dst_path);
      svn_xml_make_close_tag (&buf, pool, SVN_RA_PIPE__DST_PATH_TAG); 
    }

  svn_xml_make_open_tag (&buf, pool, svn_xml_normal,
                         SVN_RA_PIPE__RECURSIVE_TAG, NULL);
  svn_stringbuf_appendcstr (buf, recurse ? "yes" : "no");
  svn_xml_make_close_tag (&buf, pool, SVN_RA_PIPE__RECURSIVE_TAG);
    
  svn_xml_make_open_tag (&buf, pool, svn_xml_normal,
                         SVN_RA_PIPE__FETCH_TEXT_TAG, NULL);
  svn_stringbuf_appendcstr (buf, fetch_text ? "yes" : "no");
  svn_xml_make_close_tag (&buf, pool, SVN_RA_PIPE__FETCH_TEXT_TAG);

  apr_err = apr_file_write_full (output, buf->data, buf->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, pool,
                             "Could not start report");

  *reporter = &ra_pipe_reporter;
  *report_baton = baton;
  return SVN_NO_ERROR;
}

