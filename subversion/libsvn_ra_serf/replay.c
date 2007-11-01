/*
 * replay.c :  entry point for replay RA functions for ra_serf
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and replays, available at http://subversion.tigris.org/.
 * ====================================================================
 */



#include <apr_uri.h>

#include <expat.h>

#include <serf.h>

#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_xml.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_base64.h"
#include "svn_version.h"
#include "svn_path.h"
#include "svn_private_config.h"

#include "ra_serf.h"


/*
 * This enum represents the current state of our XML parsing.
 */
typedef enum {
  NONE = 0,
  REPORT,
  OPEN_DIR,
  ADD_DIR,
  OPEN_FILE,
  ADD_FILE,
  DELETE_ENTRY,
  APPLY_TEXTDELTA,
  CHANGE_PROP,
} replay_state_e;

typedef struct replay_info_t replay_info_t;

struct replay_info_t {
  apr_pool_t *pool;

  void *baton;
  svn_stream_t *stream;

  replay_info_t *parent;
};

typedef svn_error_t *
(*change_prop_t)(void *baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool);

typedef struct {
  apr_pool_t *pool;

  change_prop_t change;

  const char *name;
  svn_boolean_t del_prop;

  const char *data;
  apr_size_t len;

  replay_info_t *parent;
} prop_info_t;

typedef struct {
  apr_pool_t *pool;

  /* are we done? */
  svn_boolean_t done;

  /* callback to get an editor */
  svn_ra_replay_revstart_callback_t revstart_func;
  svn_ra_replay_revfinish_callback_t revfinish_func;
  void *replay_baton;

  /* replay receiver function and baton */
  const svn_delta_editor_t *editor;
  void *editor_baton;

  /* current revision */
  svn_revnum_t revision;

  /* Information needed to create the replay report body */
  svn_revnum_t low_water_mark;
  svn_boolean_t send_deltas;
} replay_context_t;


static void *
push_state(svn_ra_serf__xml_parser_t *parser,
           replay_context_t *replay_ctx,
           replay_state_e state)
{
  svn_ra_serf__xml_push_state(parser, state);

  if (state == OPEN_DIR || state == ADD_DIR ||
      state == OPEN_FILE || state == ADD_FILE)
    {
      replay_info_t *info;

      info = apr_palloc(parser->state->pool, sizeof(*info));

      info->pool = parser->state->pool;
      info->parent = parser->state->private;
      info->baton = NULL;
      info->stream = NULL;

      parser->state->private = info;
    }
  else if (state == CHANGE_PROP)
    {
      prop_info_t *info;

      info = apr_pcalloc(parser->state->pool, sizeof(*info));

      info->pool = parser->state->pool;
      info->parent = parser->state->private;

      parser->state->private = info;
    }

  return parser->state->private;
}

static svn_error_t *
start_replay(svn_ra_serf__xml_parser_t *parser,
             void *userData,
             svn_ra_serf__dav_props_t name,
             const char **attrs)
{
  replay_context_t *ctx = userData;
  replay_state_e state;

  state = parser->state->current_state;

  if (state == NONE &&
      strcmp(name.name, "editor-report") == 0)
    {
      SVN_ERR(ctx->revstart_func(ctx->revision, ctx->replay_baton, 
                                 &ctx->editor, &ctx->editor_baton, 
                                 ctx->pool));
      push_state(parser, ctx, REPORT);
    }
  else if (state == REPORT &&
           strcmp(name.name, "target-revision") == 0)
    {
      const char *rev;

      rev = svn_xml_get_attr_value("rev", attrs);
      if (!rev)
        {
          return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                    _("Missing revision attr in target-revision element"));
        }

      SVN_ERR(ctx->editor->set_target_revision(ctx->editor_baton,
                                               SVN_STR_TO_REV(rev),
                                               parser->state->pool));
    }
  else if (state == REPORT &&
           strcmp(name.name, "open-root") == 0)
    {
      const char *rev;
      replay_info_t *info;

      rev = svn_xml_get_attr_value("rev", attrs);

      if (!rev)
        {
          return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                    _("Missing revision attr in open-root element"));
        }

      info = push_state(parser, ctx, OPEN_DIR);

      SVN_ERR(ctx->editor->open_root(ctx->editor_baton,
                                     SVN_STR_TO_REV(rev), parser->state->pool,
                                     &info->baton));
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "delete-entry") == 0)
    {
      const char *file_name, *rev;
      replay_info_t *info;

      file_name = svn_xml_get_attr_value("name", attrs);
      if (!file_name)
        {
          return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                    _("Missing name attr in delete-entry element"));
        }
      rev = svn_xml_get_attr_value("rev", attrs);
      if (!rev)
        {
          return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                    _("Missing revision attr in delete-entry element"));
        }

      info = push_state(parser, ctx, DELETE_ENTRY);

      SVN_ERR(ctx->editor->delete_entry(file_name, SVN_STR_TO_REV(rev),
                                        info->baton, parser->state->pool));

      svn_ra_serf__xml_pop_state(parser);
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "open-directory") == 0)
    {
      const char *rev, *dirname;
      replay_info_t *info;

      dirname = svn_xml_get_attr_value("name", attrs);
      if (!dirname)
        {
          return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                    _("Missing name attr in open-directory element"));
        }
      rev = svn_xml_get_attr_value("rev", attrs);
      if (!rev)
        {
          return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                    _("Missing revision attr in open-directory element"));
        }

      info = push_state(parser, ctx, OPEN_DIR);

      SVN_ERR(ctx->editor->open_directory(dirname, info->parent->baton,
                                          SVN_STR_TO_REV(rev),
                                          parser->state->pool, &info->baton));
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "add-directory") == 0)
    {
      const char *dir_name, *copyfrom, *copyrev;
      svn_revnum_t rev;
      replay_info_t *info;

      dir_name = svn_xml_get_attr_value("name", attrs);
      if (!dir_name)
        {
          return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                    _("Missing name attr in add-directory element"));
        }
      copyfrom = svn_xml_get_attr_value("copyfrom-path", attrs);
      copyrev = svn_xml_get_attr_value("copyfrom-rev", attrs);

      if (copyrev)
        rev = SVN_STR_TO_REV(copyrev);
      else
        rev = SVN_INVALID_REVNUM;

      info = push_state(parser, ctx, ADD_DIR);

      SVN_ERR(ctx->editor->add_directory(dir_name, info->parent->baton,
                                         copyfrom, rev,
                                         parser->state->pool, &info->baton));
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "close-directory") == 0)
    {
      replay_info_t *info = parser->state->private;

      SVN_ERR(ctx->editor->close_directory(info->baton, parser->state->pool));

      svn_ra_serf__xml_pop_state(parser);
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "open-file") == 0)
    {
      const char *file_name, *rev;
      replay_info_t *info;

      file_name = svn_xml_get_attr_value("name", attrs);
      if (!file_name)
        {
          return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                    _("Missing name attr in open-file element"));
        }
      rev = svn_xml_get_attr_value("rev", attrs);
      if (!rev)
        {
          return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                    _("Missing revision attr in open-file element"));
        }

      info = push_state(parser, ctx, OPEN_FILE);

      SVN_ERR(ctx->editor->open_file(file_name, info->parent->baton,
                                     SVN_STR_TO_REV(rev),
                                     parser->state->pool, &info->baton));
    }
  else if ((state == OPEN_DIR || state == ADD_DIR) &&
           strcmp(name.name, "add-file") == 0)
    {
      const char *file_name, *copyfrom, *copyrev;
      svn_revnum_t rev;
      replay_info_t *info;

      file_name = svn_xml_get_attr_value("name", attrs);
      if (!file_name)
        {
          return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                    _("Missing name attr in add-file element"));
        }
      copyfrom = svn_xml_get_attr_value("copyfrom-path", attrs);
      copyrev = svn_xml_get_attr_value("copyfrom-rev", attrs);

      info = push_state(parser, ctx, ADD_FILE);

      if (copyrev)
        rev = SVN_STR_TO_REV(copyrev);
      else
        rev = SVN_INVALID_REVNUM;

      SVN_ERR(ctx->editor->add_file(file_name, info->parent->baton,
                                    copyfrom, rev,
                                    parser->state->pool, &info->baton));
    }
  else if ((state == OPEN_FILE || state == ADD_FILE) &&
           strcmp(name.name, "apply-textdelta") == 0)
    {
      const char *checksum;
      replay_info_t *info;
      svn_txdelta_window_handler_t textdelta;
      void *textdelta_baton;
      svn_stream_t *delta_stream;

      info = push_state(parser, ctx, APPLY_TEXTDELTA);

      checksum = svn_xml_get_attr_value("checksum", attrs);
      if (checksum)
        {
          checksum = apr_pstrdup(info->pool, checksum);
        }

      SVN_ERR(ctx->editor->apply_textdelta(info->baton, checksum,
                                           info->pool,
                                           &textdelta,
                                           &textdelta_baton));

      delta_stream = svn_txdelta_parse_svndiff(textdelta, textdelta_baton,
                                               TRUE, info->pool);
      info->stream = svn_base64_decode(delta_stream, info->pool);
    }
  else if ((state == OPEN_FILE || state == ADD_FILE) &&
           strcmp(name.name, "close-file") == 0)
    {
      replay_info_t *info = parser->state->private;
      const char *checksum;

      checksum = svn_xml_get_attr_value("checksum", attrs);

      SVN_ERR(ctx->editor->close_file(info->baton, checksum,
                                      parser->state->pool));

      svn_ra_serf__xml_pop_state(parser);
    }
  else if (((state == OPEN_FILE || state == ADD_FILE) &&
            strcmp(name.name, "change-file-prop") == 0) ||
           ((state == OPEN_DIR || state == ADD_DIR) &&
            strcmp(name.name, "change-dir-prop") == 0))
    {
      const char *prop_name;
      prop_info_t *info;

      prop_name = svn_xml_get_attr_value("name", attrs);
      if (!prop_name)
        {
          return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                   _("Missing name attr in %s element"),
                                   name.name);
        }

      info = push_state(parser, ctx, CHANGE_PROP);

      info->name = apr_pstrdup(parser->state->pool, prop_name);

      if (svn_xml_get_attr_value("del", attrs))
        info->del_prop = TRUE;
      else
        info->del_prop = FALSE;

      if (state == OPEN_FILE || state == ADD_FILE)
        info->change = ctx->editor->change_file_prop;
      else
        info->change = ctx->editor->change_dir_prop;

    }

  return SVN_NO_ERROR;
}

static svn_error_t *
end_replay(svn_ra_serf__xml_parser_t *parser,
           void *userData,
           svn_ra_serf__dav_props_t name)
{
  replay_context_t *ctx = userData;
  replay_state_e state;

  UNUSED_CTX(ctx);

  state = parser->state->current_state;

  if (state == REPORT &&
      strcmp(name.name, "editor-report") == 0)
    {
      svn_ra_serf__xml_pop_state(parser);
      SVN_ERR(ctx->revfinish_func(ctx->revision, ctx->replay_baton, 
                                  ctx->editor, ctx->editor_baton,
                                  ctx->pool));
    }
  else if (state == OPEN_DIR && strcmp(name.name, "open-directory") == 0)
    {
      /* Don't do anything. */
    }
  else if (state == ADD_DIR && strcmp(name.name, "add-directory") == 0)
    {
      /* Don't do anything. */
    }
  else if (state == OPEN_FILE && strcmp(name.name, "open-file") == 0)
    {
      /* Don't do anything. */
    }
  else if (state == ADD_FILE && strcmp(name.name, "add-file") == 0)
    {
      /* Don't do anything. */
    }
  else if ((state == OPEN_FILE || state == ADD_FILE) &&
           strcmp(name.name, "close-file") == 0)
    {
      /* Don't do anything. */
    }
  else if ((state == APPLY_TEXTDELTA) &&
           strcmp(name.name, "apply-textdelta") == 0)
    {
      replay_info_t *info = parser->state->private;
      SVN_ERR(svn_stream_close(info->stream));
      svn_ra_serf__xml_pop_state(parser);
    }
  else if (state == CHANGE_PROP &&
           (strcmp(name.name, "change-file-prop") == 0 ||
            strcmp(name.name, "change-dir-prop") == 0))
    {
      prop_info_t *info = parser->state->private;
      const svn_string_t *prop_val;

      if (info->del_prop == TRUE)
        {
          prop_val = NULL;
        }
      else
        {
          svn_string_t tmp_prop;

          tmp_prop.data = info->data;
          tmp_prop.len = info->len;

          prop_val = svn_base64_decode_string(&tmp_prop, parser->state->pool);
        }

      SVN_ERR(info->change(info->parent->baton, info->name, prop_val,
                           info->parent->pool));
      svn_ra_serf__xml_pop_state(parser);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
cdata_replay(svn_ra_serf__xml_parser_t *parser,
             void *userData,
             const char *data,
             apr_size_t len)
{
  replay_context_t *replay_ctx = userData;
  replay_state_e state;

  UNUSED_CTX(replay_ctx);

  state = parser->state->current_state;

  if (state == APPLY_TEXTDELTA)
    {
      replay_info_t *info = parser->state->private;
      apr_size_t written;

      written = len;

      SVN_ERR(svn_stream_write(info->stream, data, &written));

      if (written != len)
        return svn_error_create(SVN_ERR_STREAM_UNEXPECTED_EOF, NULL,
                                _("Error writing stream: unexpected EOF"));
    }
  else if (state == CHANGE_PROP)
    {
      prop_info_t *info = parser->state->private;

      svn_ra_serf__expand_string(&info->data, &info->len,
                                 data, len, parser->state->pool);
    }

  return SVN_NO_ERROR;
}

static serf_bucket_t *
create_replay_body(void *baton,
                   serf_bucket_alloc_t *alloc,
                   apr_pool_t *pool)
{
  replay_context_t *ctx = baton;
  serf_bucket_t *body_bkt, *tmp;

  body_bkt = serf_bucket_aggregate_create(alloc);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<S:replay-report xmlns:S=\"",
                                      sizeof("<S:replay-report xmlns:S=\"")-1,
                                      alloc);
  serf_bucket_aggregate_append(body_bkt, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(SVN_XML_NAMESPACE,
                                      sizeof(SVN_XML_NAMESPACE)-1,
                                      alloc);
  serf_bucket_aggregate_append(body_bkt, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\">",
                                      sizeof("\">")-1,
                                      alloc);
  serf_bucket_aggregate_append(body_bkt, tmp);

  svn_ra_serf__add_tag_buckets(body_bkt,
                               "S:revision", apr_ltoa(pool, ctx->revision),
                               alloc);
  svn_ra_serf__add_tag_buckets(body_bkt,
                               "S:low-water-mark",
                               apr_ltoa(pool, ctx->low_water_mark),
                               alloc);

  svn_ra_serf__add_tag_buckets(body_bkt,
                               "S:send-deltas",
                               apr_ltoa(pool, ctx->send_deltas),
                               alloc);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</S:replay-report>",
                                      sizeof("</S:replay-report>")-1,
                                      alloc);
  serf_bucket_aggregate_append(body_bkt, tmp);

  return body_bkt;
}

svn_error_t *
svn_ra_serf__replay(svn_ra_session_t *ra_session,
                    svn_revnum_t revision,
                    svn_revnum_t low_water_mark,
                    svn_boolean_t send_deltas,
                    const svn_delta_editor_t *editor,
                    void *edit_baton,
                    apr_pool_t *pool)
{
  replay_context_t *replay_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_parser_t *parser_ctx;

  replay_ctx = apr_pcalloc(pool, sizeof(*replay_ctx));
  replay_ctx->pool = pool;
  replay_ctx->editor = editor;
  replay_ctx->editor_baton = edit_baton;
  replay_ctx->done = FALSE;
  replay_ctx->low_water_mark = low_water_mark;
  replay_ctx->send_deltas = send_deltas;

  handler = apr_pcalloc(pool, sizeof(*handler));

  handler->method = "REPORT";
  handler->path = session->repos_url_str;
  handler->body_delegate = create_replay_body;
  handler->body_delegate_baton = replay_ctx;
  handler->body_type = "text/xml";
  handler->conn = session->conns[0];
  handler->session = session;

  parser_ctx = apr_pcalloc(pool, sizeof(*parser_ctx));

  parser_ctx->pool = pool;
  parser_ctx->user_data = replay_ctx;
  parser_ctx->start = start_replay;
  parser_ctx->end = end_replay;
  parser_ctx->cdata = cdata_replay;
  parser_ctx->done = &replay_ctx->done;

  handler->response_handler = svn_ra_serf__handle_xml_parser;
  handler->response_baton = parser_ctx;

  svn_ra_serf__request_create(handler);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__replay_range(svn_ra_session_t *ra_session,
                          svn_revnum_t start_revision,
                          svn_revnum_t end_revision,
                          svn_revnum_t low_water_mark,
                          svn_boolean_t send_deltas,
                          svn_ra_replay_revstart_callback_t revstart_func,
                          svn_ra_replay_revfinish_callback_t revfinish_func,
                          void *replay_baton,
                          apr_pool_t *pool)
{
  replay_context_t *replay_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_parser_t *parser_ctx;
  svn_revnum_t rev = start_revision;

  while (1)
    {
      apr_status_t status;

      /* Send pending requests, if any */
      if (rev <= end_revision)
        {
          replay_ctx = apr_pcalloc(pool, sizeof(*replay_ctx));
          replay_ctx->pool = pool;
          replay_ctx->revstart_func = revstart_func;
          replay_ctx->revfinish_func = revfinish_func;
          replay_ctx->replay_baton = replay_baton;
          replay_ctx->done = FALSE;
          replay_ctx->revision = rev;
          replay_ctx->low_water_mark = low_water_mark;
          replay_ctx->send_deltas = send_deltas;

          handler = apr_pcalloc(pool, sizeof(*handler));

          handler->method = "REPORT";
          handler->path = session->repos_url_str;
          handler->body_delegate = create_replay_body;
          handler->body_delegate_baton = replay_ctx;
          handler->conn = session->conns[0];
          handler->session = session;

          parser_ctx = apr_pcalloc(pool, sizeof(*parser_ctx));

          parser_ctx->pool = pool;
          parser_ctx->user_data = replay_ctx;
          parser_ctx->start = start_replay;
          parser_ctx->end = end_replay;
          parser_ctx->cdata = cdata_replay;
          parser_ctx->done = &replay_ctx->done;

          handler->response_handler = svn_ra_serf__handle_xml_parser;
          handler->response_baton = parser_ctx;

          svn_ra_serf__request_create(handler);

          rev++;
        }

      /* Run the serf loop, send outgoing and process incoming requests. 
         Continue doing this if there are enough requests sent or there are no
         more pending requests. */
      status = serf_context_run(session->context, SERF_DURATION_FOREVER, pool);
      if (APR_STATUS_IS_TIMEUP(status))
        {
          continue;
        }
      if (status)
        {
          SVN_ERR(session->pending_error);

          return svn_error_wrap_apr(status, 
                                    _("Error retrieving replay REPORT (%d)"),
                                    status);
        }
      if (replay_ctx->done)
        break;
    }

  return SVN_NO_ERROR;
}
