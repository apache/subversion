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
} replay_baton_t;

#define TOP_DIR(rb) (((dir_item_t *)(rb)->dirs->elts)[(rb)->dirs->nelts - 1])

/* Info about a given directory we've seen. */
typedef struct {
  void *baton;
  const char *path;
  apr_pool_t *pool;
} dir_item_t;

static void
push_dir(replay_baton_t *rb, void *baton, const char *path, apr_pool_t *pool)
{
  dir_item_t *di = apr_array_push(rb->dirs);

  di->baton = baton;
  di->path = apr_pstrdup(pool, path);
  di->pool = pool;
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

  /* XXX validate element */

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
                                                    SVN_STR_TO_REV (crev),
                                                    rb->pool);
      }
      break;

    case ELEM_open_root:
      {
        const char *crev = svn_xml_get_attr_value("rev", atts);
        apr_pool_t *subpool = svn_pool_create(rb->pool);
        void *dir_baton;

        if (! crev)
          rb->err = svn_error_create
                     (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                      _("Missing revision attr in open-root element"));
        else
          rb->err = rb->editor->open_root(rb->edit_baton,
                                          SVN_STR_TO_REV (crev), subpool,
                                          &dir_baton);

        push_dir(rb, dir_baton, "", subpool);
      }
      break;

    case ELEM_delete_entry:
      /* XXX implement */
      break;

    case ELEM_open_directory:
    case ELEM_add_directory:
      {
        const char *crev = svn_xml_get_attr_value("rev", atts);
        const char *path = svn_xml_get_attr_value("path", atts);

        /* Pop off any dirs we're done with, i.e. anything our path isn't
         * under */
        for (;;)
          {
            if (rb->dirs->nelts == 1)
              break;
            else
              {
                dir_item_t *di = &TOP_DIR(rb);

                if (strcmp(path, di->path) != 0)
                  {
                    svn_pool_destroy(di->pool);
                    apr_array_pop(rb->dirs);
                  }
              }
          }

        if (! path)
          rb->err = svn_error_create
                     (SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                      _("Missing revision path in open-directory element"));
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
              rb->err = rb->editor->open_directory(path, parent->baton,
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

                rb->err = rb->editor->add_directory(path, parent->baton,
                                                    cpath, rev, subpool,
                                                    &dir_baton);
              }
            else
              abort();

            push_dir(rb, dir_baton, path, subpool);
          }
      }
      break;

    case ELEM_open_file:
      /* XXX implement */
      break;

    case ELEM_add_file:
      /* XXX implement */
      break;

    case ELEM_apply_textdelta:
      /* XXX implement */
      break;

    case ELEM_change_file_prop:
      /* XXX implement */
      break;

    case ELEM_change_dir_prop:
      /* XXX implement */
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
      /* XXX close remaining dirs? */
      rb->err = rb->editor->close_edit(rb->edit_baton, rb->pool);
      break;

    /* XXX anything else we need to handle in close?  apply textdelta maybe? */

    default:
      break;
    }

  return SVN_RA_DAV__XML_VALID;
}

static int
cdata_handler(void *baton, int state, const char *cdata, size_t len)
{
  switch (state)
    {
    case ELEM_apply_textdelta:
      /* XXX implement */
      break;
    }

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
  const char *vcc_url;
  replay_baton_t rb;

  const char *body
    = apr_psprintf(pool,
                   "<S:replay-report xmlns:S=\"svn:\">\n"
                   "  <S:revision>%ld</S:revision>\n"
                   "  <S:low-water-mark>%ld</S:low-water-mark>\n"
                   "  <S:send-deltas>%d</S:send-deltas>\n"
                   "</S:replay-report>",
                   revision, low_water_mark, send_deltas);

  SVN_ERR (svn_ra_dav__get_vcc(&vcc_url, ras->sess, ras->url->data, pool));

  rb.editor = editor;
  rb.edit_baton = edit_baton;
  rb.err = SVN_NO_ERROR;
  rb.pool = pool;
  rb.dirs = apr_array_make(pool, 5, sizeof (dir_item_t));

  SVN_ERR (svn_ra_dav__parsed_request(ras->sess, "REPORT", vcc_url, body,
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
