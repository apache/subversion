/*
 * fetch.c :  routines for fetching updates and checkouts
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <string.h>     /* for strrchr() */

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_strings.h>

#include <http_basic.h>
#include <http_utils.h>
#include <dav_basic.h>
#include <dav_207.h>
#include <dav_props.h>
#include <hip_xml.h>

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_path.h"

#include "ra_dav.h"


enum {
  ELEM_resourcetype = 0x1000,
  ELEM_collection,
  ELEM_target,
  ELEM_activity_collection_set
};

static const dav_propname fetch_props[] =
{
  { "DAV:", "activity-collection-set" },
  { "DAV:", "resourcetype" },
  { "DAV:", "target" },
  { NULL }
};

static const struct hip_xml_elm fetch_elems[] =
{
  { "DAV:", "resourcetype", ELEM_resourcetype, 0 },
  { "DAV:", "collection", ELEM_collection, HIP_XML_CDATA },
  { "DAV:", "target", ELEM_target, 0 },
  { "DAV:", "activity-collection-set", ELEM_activity_collection_set, 0 },
  { "DAV:", "href", DAV_ELM_href, HIP_XML_CDATA },
  { NULL }
};

typedef struct {
  const char *href;
  const char *target_href;
} file_rec_t;

typedef struct {
  const char *href;
  void *parent_baton;   /* baton of parent dir */
} dir_rec_t;

typedef struct {
  const char *href;
  int is_collection;
  int href_parent;
  const char *target_href;
} resource_t;

typedef struct {
  const char *cur_collection;   /* current URL on server */
  void *cur_baton;              /* current dir in WC */

  apr_array_header_t *subdirs;  /* URL paths of subdirs to scan */
  apr_array_header_t *files;

  const svn_delta_edit_fns_t *editor;
  void *edit_baton;

  apr_pool_t *pool;

  const char *activity_href;    /* where to create activities */

  /* used during fetch_dirents() */
  dav_propfind_handler *dph;

  /* used during fetch_file() */
  svn_txdelta_window_handler_t *handler;
  void *handler_baton;

} fetch_ctx_t;


static svn_string_t *
my_basename(const char *url, apr_pool_t *pool)
{
  svn_string_t *s = svn_string_create(url, pool);

  /* ### creates yet another string. let's optimize this stuff... */
  return svn_path_last_component(s, 0, pool);
}

static void *
start_resource (void *userdata, const char *href)
{
  fetch_ctx_t *fc = userdata;
  resource_t *r = apr_pcalloc(fc->pool, sizeof(*r));

  /* printf("start_resource: %s\n", href); */

  /* ### mod_dav returns absolute paths in the DAV:href element. that is
     ### fine for us, since we're based on top of mod_dav. however, this
     ### will have an impact on future interopability.

     ### i.e. r->href should be turned into an absolute href
  */
  r->href = apr_pstrdup(fc->pool, href);

  return r;
}

static void
end_resource (void *userdata, void *resource, const char *status_line,
              const http_status *status, const char *description)
{
  fetch_ctx_t *fc = userdata;
  resource_t *r = resource;

#if 0
  printf("end_resource: %s\n", r->href);
  if (status_line != NULL)
    printf("    status line: %s\n", status_line);
  if (status != NULL) {
    printf("         status: HTTP/%d.%d  %d (%s)\n",
           status->major_version, status->minor_version, status->code,
           status->reason_phrase);
  }
  if (description != NULL)
    printf("    description: %s\n", description);
#endif

  if (r->is_collection)
    {
      struct uri href;

      uri_parse(r->href, &href, NULL);
      if (uri_compare(href.path, fc->cur_collection) == 0)
        {
          /* don't insert "this dir" into the set of subdirs */
        }
      else
        {
          dir_rec_t *dr = apr_push_array(fc->subdirs);
          dr->href = href.path;
          dr->parent_baton = fc->cur_baton;

          printf("  ... pushing subdir: %s\n", href.path);
        }

      /* ### we will need r->target_href to fetch properties */
    }
  else
    {
      file_rec_t *fr = apr_push_array(fc->files);
      fr->href = r->href;
      fr->target_href = r->target_href;

      printf("  ... found file: %s -> %s\n", r->href, r->target_href);
    }
}

static int
validate_element (hip_xml_elmid parent, hip_xml_elmid child)
{
  /*  printf("validate_element: #%d as child of #%d\n", child, parent); */
  
  switch (parent)
    {
    case DAV_ELM_prop:
        switch (child)
          {
          case ELEM_target:
          case ELEM_resourcetype:
          case ELEM_activity_collection_set:
            return HIP_XML_VALID;
          default:
            return HIP_XML_DECLINE;
          }
        
    case ELEM_target:
      if (child == DAV_ELM_href)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* not concerned with other types */
      
    case ELEM_resourcetype:
      if (child == ELEM_collection)
        return HIP_XML_VALID;
      else
        return HIP_XML_INVALID;

    case ELEM_activity_collection_set:
      if (child == DAV_ELM_href)
        return HIP_XML_VALID;
      else
        return HIP_XML_INVALID;

    default:
      return HIP_XML_DECLINE;
    }

  /* NOTREACHED */
}

static int
start_element (void *userdata, const struct hip_xml_elm *elm,
               const char **atts)
{
  fetch_ctx_t *fc = userdata;
  resource_t *r = dav_propfind_get_current_resource(fc->dph);

  /* ### need logic to determine whether this (property) element is in
     ### a successful propstat, or a failing one...
     ### waiting on Joe for feedback */

  /* printf("start_element: %s:%s  (#%d)\n", elm->nspace, elm->name, elm->id);

  while (*atts)
    {
      printf("    attr: %s='%s'\n", atts[0], atts[1]);
      atts += 2;
    }
  */

  switch (elm->id)
    {
    case ELEM_collection:
      r->is_collection = 1;
      break;

    case ELEM_target:
    case ELEM_activity_collection_set:
      r->href_parent = elm->id;
      break;

    default:
      /* nothing to do for these */
      break;
    }

  return 0;
}

static int
end_element (void *userdata, const struct hip_xml_elm *elm, const char *cdata)
{
  fetch_ctx_t *fc = userdata;
  resource_t *r = dav_propfind_get_current_resource(fc->dph);

#if 0
  printf("end_element: %s:%s  (#%d)\n", elm->nspace, elm->name, elm->id);

  if (cdata == NULL)
    {
      /* ### add something to fetch_ctx_t to signal an error */
      /*      return 1;*/
    }
  else {
    printf("      cdata: '%s'\n", cdata);
  }
#endif

  if (elm->id == DAV_ELM_href)
    {
      if (r->href_parent == ELEM_target)
        r->target_href = apr_pstrdup(fc->pool, cdata);

      /* store the activity HREF only once */
      else if (fc->activity_href == NULL)
        {
          /* DAV:activity-collection-set

             ### should/how about dealing with multiple HREFs in here? */
          fc->activity_href = apr_pstrdup(fc->pool, cdata);
        }
    }

  return 0;
}

static svn_error_t *
fetch_dirents (svn_ra_session_t *ras,
               const char *url,
               fetch_ctx_t *fc)
{
  hip_xml_parser *hip;
  int rv;
  const dav_propname *props;

  fc->cur_collection = url;
  fc->dph = dav_propfind_create(ras->sess, url, DAV_DEPTH_ONE);

  dav_propfind_set_resource_handlers(fc->dph, start_resource, end_resource);
  hip = dav_propfind_get_parser(fc->dph);

  hip_xml_add_handler(hip, fetch_elems,
                      validate_element, start_element, end_element, fc);

  if (fc->activity_href == NULL)
    props = fetch_props;
  else
    props = fetch_props + 1;    /* don't fetch the activity href */
  rv = dav_propfind_named(fc->dph, props, fc);

  dav_propfind_destroy(fc->dph);

  if (rv != HTTP_OK)
    {
      switch (rv)
        {
        case HTTP_CONNECT:
          return svn_error_createf(0, 0, NULL, fc->pool,
                                   "Could not connect to server (%s, port %d).",
                                   ras->root.host, ras->root.port);
        case HTTP_AUTH:
          return svn_error_create(SVN_ERR_NOT_AUTHORIZED, 0, NULL, 
                                  fc->pool,
                                  "Authentication failed on server.");
        default:
          return svn_error_create(0, 0, NULL, fc->pool,
                                  http_get_error(ras->sess));
        }
    }

  return SVN_NO_ERROR;
}

static void
fetch_file_reader(void *userdata, const char *buf, size_t len)
{
  fetch_ctx_t *fc = userdata;
  svn_txdelta_window_t window = { 0 };
  svn_txdelta_op_t op;
  svn_string_t data = { (char *)buf, len, len, fc->pool };
  svn_error_t *err;

  if (len == 0)
    {
      /* file is complete. */
      err = (*fc->handler)(NULL, fc->handler_baton);
      if (err)
        {
          /* ### how to abort the read loop? */
        }
      return;
    }

  op.action_code = svn_txdelta_new;
  op.offset = 0;
  op.length = len;

  window.tview_len = len;       /* result will be this long */
  window.num_ops = 1;
  window.ops_size = 1;          /* ### why is this here? */
  window.ops = &op;
  window.new = &data;
  window.pool = fc->pool;

  err = (*fc->handler)(&window, fc->handler_baton);
  if (err)
    {
      /* ### how to abort the read loop? */
    }
}

static svn_error_t *
fetch_file (svn_ra_session_t *ras,
            const char *url,
            fetch_ctx_t *fc)
{
  svn_error_t *err;
  svn_string_t *name;
  svn_string_t *ancestor_path;
  svn_revnum_t ancestor_revision;
  void *file_baton;
  int rv;

  /* ### */
  ancestor_path = svn_string_create("### ancestor_path ###", fc->pool);
  ancestor_revision = 1;

  printf("fetching and saving %s\n", url);

  name = my_basename(url, fc->pool);
  err = (*fc->editor->add_file) (name, fc->cur_baton,
                                 ancestor_path, ancestor_revision,
                                 &file_baton);
  if (err)
    return svn_error_quick_wrap(err, "could not add a file");

  err = (*fc->editor->apply_textdelta) (file_baton,
                                        &fc->handler,
                                        &fc->handler_baton);
  if (err)
    return svn_error_quick_wrap(err, "could not save file");

  rv = http_read_file(ras->sess, url, fetch_file_reader, fc);
  if (rv != HTTP_OK)
    {
      /* ### other GET responses? */
    }

  /* note: handler_baton was "closed" in fetch_file_reader() */

  /* ### fetch properties */
  /* ### store URL into a local, predefined property */

  /* done with the file */
  return (*fc->editor->close_file)(file_baton);
}

svn_error_t * svn_ra_dav__checkout (void *session_baton,
                                    const svn_delta_edit_fns_t *editor,
                                    void *edit_baton,
                                    svn_string_t *URL)
{
  svn_ra_session_t *ras = session_baton;
  int recurse = 1;      /* ### until it gets passed to us */

  svn_error_t *err;
  fetch_ctx_t fc = { 0 };
  dir_rec_t *dr;
  svn_string_t *ancestor_path;
  svn_revnum_t ancestor_revision;
  void *dir_baton;

  fc.editor = editor;
  fc.edit_baton = edit_baton;
  fc.pool = ras->pool;
  fc.subdirs = apr_make_array(ras->pool, 5, sizeof(dir_rec_t));
  fc.files = apr_make_array(ras->pool, 10, sizeof(file_rec_t));

  err = (*editor->replace_root)(edit_baton, &dir_baton);
  if (err != SVN_NO_ERROR)
    return err;

  /* ### join ras->rep_root, URL */
  dr = apr_push_array(fc.subdirs);
  dr->href = ras->root.path;
  dr->parent_baton = dir_baton;

  /* ### */
  ancestor_path = svn_string_create("### ancestor_path ###", ras->pool);
  ancestor_revision = 1;

  do
    {
      int idx;
      const char *url;
      void *parent_baton;
      void *this_baton;
      int i;
      svn_string_t *name;

      /* pop a subdir off the stack */
      while (1)
        {
          dr = &((dir_rec_t *)fc.subdirs->elts)[fc.subdirs->nelts - 1];
          url = dr->href;
          parent_baton = dr->parent_baton;
          --fc.subdirs->nelts;

          if (url != NULL)
            break;

          err = (*editor->close_directory) (parent_baton);
          if (err)
            return svn_error_quick_wrap(err, "could not finish directory");

          if (fc.subdirs->nelts == 0)
            return SVN_NO_ERROR;
        }

      /* add a placeholder. this will be used to signal a close_directory
         for this directory's baton. */
      dr = apr_push_array(fc.subdirs);
      dr->href = NULL;
      dr->parent_baton = NULL;
      idx = fc.subdirs->nelts - 1;

      err = fetch_dirents(ras, url, &fc);
      if (err)
        return svn_error_quick_wrap(err, "could not fetch directory entries");

      if (strlen(url) > strlen(ras->root.path))
        {
          /* We're not in the root, add a directory */
          name = my_basename(url, ras->pool);
          
          err = (*editor->add_directory) (name, parent_baton,
                                          ancestor_path, ancestor_revision,
                                          &this_baton);
          if (err)
            return svn_error_quick_wrap(err, "could not add directory");
        }
      else 
        {
          /* We are operating in the root of the repository */
          this_baton = dir_baton;
        }

      /* for each new directory added (including our marker), set its
         parent_baton */
      for (i = fc.subdirs->nelts; i-- > idx; )
        {
          dr = &((dir_rec_t *)fc.subdirs->elts)[i];
          dr->parent_baton = this_baton;
        }

      /* process each of the files that were found */
      fc.cur_baton = this_baton;
      for (i = fc.files->nelts; i--; )
        {
          file_rec_t *fr = &((file_rec_t *)fc.files->elts)[i];

          err = fetch_file(ras, fr->href, &fc);
          if (err)
            return svn_error_quick_wrap(err, "could not checkout a file");
        }
      /* reset the list of files */
      fc.files->nelts = 0;

    } while (recurse && fc.subdirs->nelts > 0);

  /* ### should never reach??? */
  return SVN_NO_ERROR;
}

/* -------------------------------------------------------------------------
**
** UPDATE HANDLING
**
** ### docco...
*/

static svn_error_t *
update_delete (svn_string_t *name,
               void *parent_baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
update_add_dir (svn_string_t *name,
                void *parent_baton,
                svn_string_t *ancestor_path,
                svn_revnum_t ancestor_revision,
                void **child_baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
update_rep_dir (svn_string_t *name,
                void *parent_baton,
                svn_string_t *ancestor_path,
                svn_revnum_t ancestor_revision,
                void **child_baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
update_change_dir_prop (void *dir_baton,
                        svn_string_t *name,
                        svn_string_t *value)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
update_close_dir (void *dir_baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
update_add_file (svn_string_t *name,
                 void *parent_baton,
                 svn_string_t *ancestor_path,
                 svn_revnum_t ancestor_revision,
                 void **file_baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
update_rep_file (svn_string_t *name,
                 void *parent_baton,
                 svn_string_t *ancestor_path,
                 svn_revnum_t ancestor_revision,
                 void **file_baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
update_apply_txdelta (void *file_baton, 
                      svn_txdelta_window_handler_t **handler,
                      void **handler_baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
update_change_file_prop (void *file_baton,
                         svn_string_t *name,
                         svn_string_t *value)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
update_close_file (void *file_baton)
{
  return SVN_NO_ERROR;
}

/*
** This structure is used during the update process. An external caller
** uses these callbacks to describe all the changes in the working copy.
** These are communicated to the server, which then decides how to update
** the client to a specific version/latest/label/etc.
*/
static const svn_delta_edit_fns_t update_editor = {
  NULL,  /* update_replace_root */
  update_delete,
  update_add_dir,
  update_rep_dir,
  update_change_dir_prop,
  update_close_dir,
  update_add_file,
  update_rep_file,
  update_apply_txdelta,
  update_change_file_prop,
  update_close_file,
  NULL   /* update_close_edit */
};

#if 0
svn_error_t *
svn_ra_dav__get_update_editor(void *session_baton,
                              const svn_delta_edit_fns_t **editor,
                              void **edit_baton,
                              const svn_delta_edit_fns_t *wc_update,
                              void *wc_update_baton,
                              svn_string_t *URL)
{
  /* shove the session and wc_* values into our baton */

  *editor = &update_editor;
  *edit_baton = NULL;
  return SVN_NO_ERROR;
}
#endif


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
