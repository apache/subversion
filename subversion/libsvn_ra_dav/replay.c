/*
 * replay.c :  routines for replaying revisions
 *
 * ====================================================================
 * Copyright (c) 2005 CollabNet.  All rights reserved.
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

#include "svn_base64.h"
#include "svn_pools.h"
#include "svn_xml.h"

#include "../libsvn_ra/ra_loader.h"

#include "ra_dav.h"

typedef struct {
  /* The underlying editor and baton we're replaying into. */
  const svn_delta_editor_t *editor;
  void *edit_baton;

  /* Any error that occurs during the replay is stored here, so it can be
   * returned after we bail out of the XML parsing. */
  svn_error_t *err;

  /* Parent pool for the whole reply. */
  apr_pool_t *pool;

  /* Stack of in progress directories, holds dir_item_t objects. */
  apr_array_header_t *dirs;

  /* Cached file baton so we can pass it between the add/open file and
   * apply textdelta portions of the editor drive. */
  void *file_baton;

  /* Variables required to decode and apply our svndiff data off the wire. */
  svn_txdelta_window_handler_t whandler;
  void *whandler_baton;
  svn_stream_t *svndiff_decoder;
  svn_stream_t *base64_decoder;

  /* A scratch pool used to allocate property data. */
  apr_pool_t *prop_pool;

  /* The name of a property that's being modified. */
  const char *prop_name;

  /* A stringbuf that holds the contents of a property being changed, if this
   * is NULL it means that the property is being deleted. */
  svn_stringbuf_t *prop_accum;
} replay_baton_t;

#define TOP_DIR(rb) (((dir_item_t *)(rb)->dirs->elts)[(rb)->dirs->nelts - 1])

/* Info about a given directory we've seen. */
typedef struct {
  void *baton;
  const char *path;
  apr_pool_t *pool;
  apr_pool_t *file_pool;
} dir_item_t;

static void
push_dir(replay_baton_t *rb, void *baton, const char *path, apr_pool_t *pool)
{
  dir_item_t *di = apr_array_push(rb->dirs);

  di->baton = baton;
  di->path = apr_pstrdup(pool, path);
  di->pool = pool;
  di->file_pool = svn_pool_create(pool);
}

static const svn_ra_dav__xml_elm_t editor_report_elements[] =
{
  { SVN_XML_NAMESPACE, "editor-report",    ELEM_editor_report, 0 },
  { SVN_XML_NAMESPACE, "target-revision",  ELEM_target_revision, 0 },
  { SVN_XML_NAMESPACE, "open-root",        ELEM_open_root, 0 },
  { SVN_XML_NAMESPACE, "delete-entry",     ELEM_delete_entry, 0 },
  { SVN_XML_NAMESPACE, "open-directory",   ELEM_open_directory, 0 },
  { SVN_XML_NAMESPACE, "add-directory",    ELEM_add_directory, 0 },
  { SVN_XML_NAMESPACE, "open-file",        ELEM_open_file, 0 },
  { SVN_XML_NAMESPACE, "add-file",         ELEM_add_file, 0 },
  { SVN_XML_NAMESPACE, "close-file",       ELEM_close_file, 0 },
  { SVN_XML_NAMESPACE, "close-directory",  ELEM_close_directory, 0 },
  { SVN_XML_NAMESPACE, "apply-textdelta",  ELEM_apply_textdelta, 0 },
  { SVN_XML_NAMESPACE, "change-file-prop", ELEM_change_file_prop, 0 },
  { SVN_XML_NAMESPACE, "change-dir-prop",  ELEM_change_dir_prop, 0 },
  { NULL }
};

static int
start_element(void *baton, int parent_state, const char *nspace,
              const char *elt_name, const char **atts)
{
  replay_baton_t *rb = baton;

  const svn_ra_dav__xml_elm_t *elm
    = svn_ra_dav__lookup_xml_elem(editor_report_elements, nspace, elt_name);

  if (! elm)
    return NE_XML_DECLINE;

  if (parent_state == ELEM_root)
    {
      /* If we're at the root of the tree, the element has to be the editor
       * report itself. */
      if (elm->id != ELEM_editor_report)
        return SVN_RA_DAV__XML_INVALID;
    }
  else if (parent_state != ELEM_editor_report)
    {
      /* If we're not at the root, our parent has to be the editor report,
       * since we don't actually nest any elements. */
      return SVN_RA_DAV__XML_INVALID;
    }

  switch (elm->id)
    {
    case ELEM_target_revision:
      {
        const char *crev = svn_xml_get_attr_value("rev", atts);
        if (! crev)
          rb->err = svn_error_create
                     (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                      _("Missing revision attr in target-revision element"));
        else
          rb->err = rb->editor->set_target_revision(rb->edit_baton,
                                                    SVN_STR_TO_REV(crev),
                                                    rb->pool);
      }
      break;

    case ELEM_open_root:
      {
        const char *crev = svn_xml_get_attr_value("rev", atts);

        if (! crev)
          rb->err = svn_error_create
                     (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                      _("Missing revision attr in open-root element"));
        else
          {
            apr_pool_t *subpool = svn_pool_create(rb->pool);
            void *dir_baton;
            rb->err = rb->editor->open_root(rb->edit_baton,
                                            SVN_STR_TO_REV(crev), subpool,
                                            &dir_baton);
            push_dir(rb, dir_baton, "", subpool);
          }
      }
      break;

    case ELEM_delete_entry:
      {
        const char *path = svn_xml_get_attr_value("name", atts);
        const char *crev = svn_xml_get_attr_value("rev", atts);

        if (! path)
          rb->err = svn_error_create
                      (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                       _("Missing name attr in delete-entry element"));
        else if (! crev)
          rb->err = svn_error_create
                      (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                       _("Missing rev attr in delete-entry element"));
        else
          {
            dir_item_t *di = &TOP_DIR(rb);

            rb->err = rb->editor->delete_entry(path, SVN_STR_TO_REV(crev),
                                               di->baton, di->pool);
          }
      }
      break;

    case ELEM_open_directory:
    case ELEM_add_directory:
      {
        const char *crev = svn_xml_get_attr_value("rev", atts);
        const char *name = svn_xml_get_attr_value("name", atts);

        if (! name)
          rb->err = svn_error_create
                     (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                      _("Missing name attr in open-directory element"));
        else
          {
            dir_item_t *parent = &TOP_DIR(rb);
            apr_pool_t *subpool = svn_pool_create(parent->pool);
            svn_revnum_t rev;
            void *dir_baton;

            if (crev)
              rev = SVN_STR_TO_REV(crev);
            else
              rev = SVN_INVALID_REVNUM;

            if (elm->id == ELEM_open_directory)
              rb->err = rb->editor->open_directory(name, parent->baton,
                                                   rev, subpool, &dir_baton);
            else if (elm->id == ELEM_add_directory)
              {
                const char *cpath = svn_xml_get_attr_value("copyfrom-path",
                                                           atts);

                crev = svn_xml_get_attr_value("copyfrom-rev", atts);

                if (crev)
                  rev = SVN_STR_TO_REV(crev);
                else
                  rev = SVN_INVALID_REVNUM;

                rb->err = rb->editor->add_directory(name, parent->baton,
                                                    cpath, rev, subpool,
                                                    &dir_baton);
              }
            else
              abort();

            push_dir(rb, dir_baton, name, subpool);
          }
      }
      break;

    case ELEM_open_file:
    case ELEM_add_file:
      {
        const char *path = svn_xml_get_attr_value("name", atts);
        svn_revnum_t rev;

        dir_item_t *parent = &TOP_DIR(rb);

        if (! path)
          {
            rb->err = svn_error_createf
                        (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                         _("Missing name attr in %s element"),
                         elm->id == ELEM_open_file ? "open-file" : "add-file");
            break;
          }

        svn_pool_clear(parent->file_pool);

        if (elm->id == ELEM_add_file)
          {
            const char *cpath = svn_xml_get_attr_value("copyfrom-path", atts);
            const char *crev = svn_xml_get_attr_value("copyfrom-rev", atts);

            if (crev)
              rev = SVN_STR_TO_REV(crev);
            else
              rev = SVN_INVALID_REVNUM;

            rb->err = rb->editor->add_file(path, parent->baton, cpath, rev,
                                           parent->file_pool, &rb->file_baton);
          }
        else
          {
            const char *crev = svn_xml_get_attr_value("rev", atts);

            if (crev)
              rev = SVN_STR_TO_REV(crev);
            else
              rev = SVN_INVALID_REVNUM;

            rb->err = rb->editor->open_file(path, parent->baton, rev,
                                            parent->file_pool,
                                            &rb->file_baton);
          }
      }
      break;

    case ELEM_apply_textdelta:
      if (! rb->file_baton)
        rb->err = svn_error_create
                    (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                     _("Got apply-textdelta element without preceding "
                       "add-file or open-file"));
      else
        {
          const char *checksum = svn_xml_get_attr_value("checksum", atts);

          rb->err = rb->editor->apply_textdelta(rb->file_baton,
                                                checksum,
                                                TOP_DIR(rb).file_pool,
                                                &rb->whandler,
                                                &rb->whandler_baton);
          if (! rb->err)
            {
              rb->svndiff_decoder = svn_txdelta_parse_svndiff
                                      (rb->whandler, rb->whandler_baton,
                                       TRUE, TOP_DIR(rb).file_pool);
              rb->base64_decoder = svn_base64_decode(rb->svndiff_decoder,
                                                     TOP_DIR(rb).file_pool);
            }
        }
      break;

    case ELEM_close_file:
      if (! rb->file_baton)
        rb->err = svn_error_create
                    (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                     _("Got close-file element without preceding "
                       "add-file or open-file"));
      else
        {
          const char *checksum = svn_xml_get_attr_value("checksum", atts);

          rb->err = rb->editor->close_file(rb->file_baton,
                                           checksum,
                                           TOP_DIR(rb).file_pool);
          rb->file_baton = NULL;
        }
      break;

    case ELEM_close_directory:
      if (rb->dirs->nelts == 0)
        rb->err = svn_error_create
                    (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                     _("Got close-directory element without ever opening "
                       "a directory"));
      else
        {
          dir_item_t *di = &TOP_DIR(rb);

          rb->err = rb->editor->close_directory(di->baton, di->pool);

          svn_pool_destroy(di->pool);

          apr_array_pop(rb->dirs);
        }
      break;

    case ELEM_change_file_prop:
    case ELEM_change_dir_prop:
      {
        const char *name = svn_xml_get_attr_value("name", atts);

        if (! name)
          rb->err = svn_error_createf
                      (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                       _("Missing name attr in %s element"),
                       elm->id == ELEM_change_file_prop ? "change-file-prop"
                                                        : "change-dir-prop");
        else
          {
            svn_pool_clear(rb->prop_pool);

            if (svn_xml_get_attr_value("del", atts))
              rb->prop_accum = NULL;
            else
              rb->prop_accum = svn_stringbuf_create("", rb->prop_pool);

            rb->prop_name = apr_pstrdup(rb->prop_pool, name);
          }
      }
      break;
    }

  if (rb->err)
    return NE_XML_ABORT;

  return elm->id;
}

static int
end_element(void *baton, int state, const char *nspace, const char *elt_name)
{
  replay_baton_t *rb = baton;

  const svn_ra_dav__xml_elm_t *elm
    = svn_ra_dav__lookup_xml_elem(editor_report_elements, nspace, elt_name);

  if (! elm)
    return NE_XML_DECLINE;

  switch (elm->id)
    {
    case ELEM_editor_report:
      if (rb->dirs->nelts)
        svn_pool_destroy(APR_ARRAY_IDX(rb->dirs, 0, dir_item_t).pool);

      rb->err = SVN_NO_ERROR;
      break;

    case ELEM_apply_textdelta:
      rb->err = svn_stream_close(rb->base64_decoder);

      rb->whandler = NULL;
      rb->whandler_baton = NULL;
      rb->svndiff_decoder = NULL;
      rb->base64_decoder = NULL;
      break;

    case ELEM_change_file_prop:
    case ELEM_change_dir_prop:
      {
        const svn_string_t *decoded_value;
        svn_string_t prop;

        if (rb->prop_accum)
          {
            prop.data = rb->prop_accum->data;
            prop.len = rb->prop_accum->len;

            decoded_value = svn_base64_decode_string(&prop, rb->prop_pool);
          }
        else
          decoded_value = NULL; /* It's a delete */

        if (elm->id == ELEM_change_dir_prop)
          rb->err = rb->editor->change_dir_prop(TOP_DIR(rb).baton,
                                                rb->prop_name,
                                                decoded_value,
                                                TOP_DIR(rb).pool);
        else
          rb->err = rb->editor->change_file_prop(rb->file_baton,
                                                 rb->prop_name,
                                                 decoded_value,
                                                 TOP_DIR(rb).file_pool);
      }
      break;

    default:
      break;
    }

  if (rb->err)
    return NE_XML_ABORT;

  return SVN_RA_DAV__XML_VALID;
}

static int
cdata_handler(void *baton, int state, const char *cdata, size_t len)
{
  replay_baton_t *rb = baton;
  apr_size_t nlen = len;

  switch (state)
    {
    case ELEM_apply_textdelta:
      rb->err = svn_stream_write(rb->base64_decoder, cdata, &nlen);
      if (! rb->err && nlen != len)
        rb->err = svn_error_createf
                    (SVN_ERR_STREAM_UNEXPECTED_EOF, NULL,
                     _("Error writing stream: unexpected EOF"));
      break;

    case ELEM_change_dir_prop:
    case ELEM_change_file_prop:
      if (! rb->prop_accum)
        rb->err = svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                    _("Got cdata content for a prop delete"));
      else
        svn_stringbuf_appendbytes(rb->prop_accum, cdata, len);
      break;
    }

  if (rb->err)
    return NE_XML_ABORT;

  return 0; /* no error */
}

svn_error_t *
svn_ra_dav__replay(svn_ra_session_t *session,
                   svn_revnum_t revision,
                   svn_revnum_t low_water_mark,
                   svn_boolean_t send_deltas,
                   const svn_delta_editor_t *editor,
                   void *edit_baton,
                   apr_pool_t *pool)
{
  svn_ra_dav__session_t *ras = session->priv;
  replay_baton_t rb;

  const char *body
    = apr_psprintf(pool,
                   "<S:replay-report xmlns:S=\"svn:\">\n"
                   "  <S:revision>%ld</S:revision>\n"
                   "  <S:low-water-mark>%ld</S:low-water-mark>\n"
                   "  <S:send-deltas>%d</S:send-deltas>\n"
                   "</S:replay-report>",
                   revision, low_water_mark, send_deltas);

  memset(&rb, 0, sizeof(rb));

  rb.editor = editor;
  rb.edit_baton = edit_baton;
  rb.err = SVN_NO_ERROR;
  rb.pool = pool;
  rb.dirs = apr_array_make(pool, 5, sizeof(dir_item_t));
  rb.prop_pool = svn_pool_create(pool);
  rb.prop_accum = svn_stringbuf_create("", rb.prop_pool);

  SVN_ERR(svn_ra_dav__parsed_request(ras->sess, "REPORT", ras->url->data, body,
                                     NULL, NULL,
                                     start_element,
                                     cdata_handler,
                                     end_element,
                                     &rb,
                                     NULL, /* extra headers */
                                     NULL, /* status code */
                                     FALSE, /* spool response */
                                     pool));

  return rb.err;
}
