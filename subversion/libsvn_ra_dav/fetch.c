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
  ELEM_resourcetype = DAV_ELM_207_UNUSED,
  ELEM_collection,
  ELEM_target,
  ELEM_activity_collection_set,
  ELEM_version_name
};

static const dav_propname fetch_props[] =
{
  { "DAV:", "activity-collection-set" },
  { "DAV:", "resourcetype" },
  { "DAV:", "target" },

  /* ### note: DAV:version-name is not necessarily located on the
     ### version-controlled resource. We know mod_dav_svn will do this,
     ### but this is a possible interop issue. Of course, simply the
     ### fact that we consider DAV:version-name to contain the revision
     ### number is pretty non-interoperable... */
  { "DAV:", "version-name" },
  { NULL }
};

static const struct hip_xml_elm fetch_elems[] =
{
  { "DAV:", "resourcetype", ELEM_resourcetype, 0 },
  { "DAV:", "collection", ELEM_collection, HIP_XML_CDATA },
  { "DAV:", "target", ELEM_target, 0 },
  { "DAV:", "activity-collection-set", ELEM_activity_collection_set, 0 },
  { "DAV:", "href", DAV_ELM_href, HIP_XML_CDATA },
  { "DAV:", "version-name", ELEM_version_name, HIP_XML_CDATA },
  { NULL }
};

typedef struct {
  /* what is the URL for this resource */
  const char *url;

  /* URL to the version resource */
  const char *vsn_url;

  /* is this resource a collection? (from the DAV:resourcetype element) */
  int is_collection;

  /* when we see a DAV:href element, what element is the parent? */
  int href_parent;

  /* what is the dir_baton for this resource's parent collection? */
  void *parent_baton;

} resource_t;

typedef struct {
  const char *cur_collection;   /* current URL on server */
  void *cur_baton;              /* current dir in WC */

  apr_array_header_t *subdirs;  /* subdirs to scan (resource_t *) */
  apr_array_header_t *files;    /* files to checkout (resource_t *) */

  const svn_delta_edit_fns_t *editor;
  void *root_dir_baton;

  apr_pool_t *pool;

  svn_string_t *activity_url;   /* where to create activities */

  /* the name of the local property to hold the version resource's URL */
  svn_string_t *vsn_url_name;

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

  svn_path_canonicalize(s, svn_path_url_style);

  /* ### creates yet another string. let's optimize this stuff... */
  return svn_path_last_component(s, svn_path_url_style, pool);
}

static void *
start_resource (void *userdata, const char *url)
{
  fetch_ctx_t *fc = userdata;
  resource_t *r = apr_pcalloc(fc->pool, sizeof(*r));

  /* printf("start_resource: %s\n", url); */

  r->parent_baton = fc->cur_baton;

  /* ### mod_dav returns absolute paths in the DAV:href element. that is
     ### fine for us, since we're based on top of mod_dav. however, this
     ### will have an impact on future interopability.

     ### i.e. r->url should be turned into an absolute path. (meaning it
     ### begins with "/"; not that it includes the scheme/host/port)
  */
  r->url = apr_pstrdup(fc->pool, url);

  return r;
}

static void
end_resource (void *userdata, void *resource, const char *status_line,
              const http_status *status, const char *description)
{
  fetch_ctx_t *fc = userdata;
  resource_t *r = resource;

#if 0
  printf("end_resource: %s\n", r->url);
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
      struct uri parsed_url;

      /* parse the PATH element out of the URL (in case it is absolute) */
      uri_parse(r->url, &parsed_url, NULL);
      if (uri_compare(parsed_url.path, fc->cur_collection) == 0)
        {
          /* don't insert "this dir" into the set of subdirs */

          /* ### it would be nice to use MSFT's "1,noroot" extension to
             ### the Depth header */
        }
      else
        {
          resource_t **subdir = apr_push_array(fc->subdirs);
          *subdir = r;

          /* revise the URL to just be a PATH portion */
          r->url = apr_pstrdup(fc->pool, parsed_url.path);

          printf("  ... pushing subdir: %s\n", parsed_url.path);
        }

      uri_free(&parsed_url);

      /* ### we will need r->vsn_url to fetch properties */
    }
  else
    {
      resource_t **file = apr_push_array(fc->files);
      *file = r;

      printf("  ... found file: %s -> %s\n", r->url, r->vsn_url);
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
          case ELEM_version_name:
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
        {
          /* <version-name><href>...cdata...</href></version-name> */

          /* Store the URL for the version resource */
          r->vsn_url = apr_pstrdup(fc->pool, cdata);
        }
      /* else: assert href_parent == ELEM_activity_collection_set */

      /* store the activity HREF only once */
      else if (fc->activity_url == NULL)
        {
          /* DAV:activity-collection-set

             ### should/how about dealing with multiple HREFs in here? */
          fc->activity_url = svn_string_create(cdata, fc->pool);
        }
    }
  else if (elm->id == ELEM_version_name)
    {
      /* DAV:version-name */

      /* ### store the revision number */
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

  hip_xml_push_handler(hip, fetch_elems,
                      validate_element, start_element, end_element, fc);

  if (fc->activity_url == NULL)
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
  window.new_data = &data;
  window.pool = fc->pool;

  err = (*fc->handler)(&window, fc->handler_baton);
  if (err)
    {
      /* ### how to abort the read loop? */
    }
}

static svn_error_t *
fetch_file (svn_ra_session_t *ras,
            const resource_t *rsrc,
            fetch_ctx_t *fc)
{
  svn_error_t *err;
  svn_error_t *err2;
  svn_string_t *name;
  svn_string_t *ancestor_path;
  svn_revnum_t ancestor_revision;
  void *file_baton;
  int rv;

  /* ### */
  ancestor_path = svn_string_create("### ancestor_path ###", fc->pool);
  ancestor_revision = 1;

  printf("fetching and saving %s\n", rsrc->url);

  name = my_basename(rsrc->url, fc->pool);
  err = (*fc->editor->add_file) (name, fc->cur_baton,
                                 /* ### NULL, SVN_INVALID_REVNUM, */
                                 ancestor_path, ancestor_revision,
                                 &file_baton);
  if (err)
    return svn_error_quick_wrap(err, "could not add a file");

  err = (*fc->editor->apply_textdelta) (file_baton,
                                        &fc->handler,
                                        &fc->handler_baton);
  if (err)
    {
      err = svn_error_quick_wrap(err, "could not save file");
      /* ### do we really need to bother with closing the file_baton? */
      goto error;
    }

  rv = http_read_file(ras->sess, rsrc->url, fetch_file_reader, fc);
  if (rv != HTTP_OK)
    {
      /* ### other GET responses? */
    }

  /* note: handler_baton was "closed" in fetch_file_reader() */

  /* ### fetch properties */

  /* store the version URL as a property */
  if (rsrc->vsn_url != NULL)
    {
      svn_string_t *vsn_url_value;

      vsn_url_value = svn_string_create(rsrc->vsn_url, fc->pool);
      err = (*fc->editor->change_file_prop)(file_baton,
                                            fc->vsn_url_name, vsn_url_value);
      if (err)
        {
          err = svn_error_quick_wrap(err,
                                     "could not save the URL of the "
                                     "version resource");
          /* ### do we really need to bother with closing the file_baton? */
          goto error;
        }
    }

  /* all done! */
  err = NULL;

 error:
  err2 = (*fc->editor->close_file)(file_baton);
  return err ? err : err2;
}

svn_error_t * svn_ra_dav__checkout (void *session_baton,
                                    const svn_delta_edit_fns_t *editor,
                                    void *root_dir_baton)
{
  svn_ra_session_t *ras = session_baton;
  int recurse = 1;      /* ### until it gets passed to us */

  svn_error_t *err;
  fetch_ctx_t fc = { 0 };
  svn_string_t *ancestor_path;
  svn_revnum_t ancestor_revision;
  svn_string_t *act_url_name;
  resource_t *rsrc;
  resource_t **prsrc;

  fc.editor = editor;
  fc.root_dir_baton = root_dir_baton;
  fc.pool = ras->pool;
  fc.subdirs = apr_make_array(ras->pool, 5, sizeof(resource_t *));
  fc.files = apr_make_array(ras->pool, 10, sizeof(resource_t *));
  fc.vsn_url_name = svn_string_create(SVN_RA_DAV__LP_VSN_URL, ras->pool);

  /* Build a directory resource for the root. We'll pop this off and fetch
     the information for it. */
  rsrc = apr_pcalloc(ras->pool, sizeof(*rsrc));
  rsrc->parent_baton = root_dir_baton;

  /* ### verify this the right place to start... */
  rsrc->url = ras->root.path;

  prsrc = apr_push_array(fc.subdirs);
  *prsrc = rsrc;

  /* ### */
  ancestor_path = svn_string_create("### ancestor_path ###", ras->pool);
  ancestor_revision = 1;

  act_url_name = svn_string_create(SVN_RA_DAV__LP_ACTIVITY_URL, ras->pool);

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
          rsrc = ((resource_t **)fc.subdirs->elts)[fc.subdirs->nelts - 1];
          url = rsrc->url;
          parent_baton = rsrc->parent_baton;
          --fc.subdirs->nelts;

          if (url != NULL)
            break;
          /* sentinel reached. close the dir. possibly done! */

          err = (*editor->close_directory) (parent_baton);
          if (err)
            return svn_error_quick_wrap(err, "could not finish directory");

          if (fc.subdirs->nelts == 0)
            return SVN_NO_ERROR;
        }

      if (strlen(url) > strlen(ras->root.path))
        {
          /* We're not in the root, add a directory */
          name = my_basename(url, ras->pool);
          
          printf("adding directory: %s\n", name->data);
          err = (*editor->add_directory) (name, parent_baton,
                                          /* ### NULL, SVN_INVALID_REVNUM, */
                                          ancestor_path, ancestor_revision,
                                          &this_baton);
          if (err)
            return svn_error_quick_wrap(err, "could not add directory");
        }
      else 
        {
          /* We are operating in the root of the repository */
          this_baton = root_dir_baton;
        }
      fc.cur_baton = this_baton;

      /* add a sentinel. this will be used to signal a close_directory
         for this directory's baton. */
      rsrc = apr_pcalloc(ras->pool, sizeof(*rsrc));
      rsrc->parent_baton = this_baton;
      prsrc = apr_push_array(fc.subdirs);
      *prsrc = rsrc;

      idx = fc.subdirs->nelts - 1;

      err = fetch_dirents(ras, url, &fc);
      if (err)
        return svn_error_quick_wrap(err, "could not fetch directory entries");

      /* we should have successfully fetched an activity URL */
      if (fc.activity_url != NULL)
        {
          /* store the activity URL as a property */
          err = (*editor->change_dir_prop)(this_baton,
                                           act_url_name, fc.activity_url);
          if (err)
            /* ### should we close the dir batons first? */
            return svn_error_quick_wrap(err,
                                        "could not save the URL to indicate "
                                        "where to create activities");
        }

      /* process each of the files that were found */
      for (i = fc.files->nelts; i--; )
        {
          rsrc = ((resource_t **)fc.files->elts)[i];

          err = fetch_file(ras, rsrc, &fc);
          if (err)
            /* ### should we close the dir batons first? */
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
update_delete_item (svn_string_t *name,
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
  update_delete_item,
  update_add_dir,
  update_rep_dir,
  update_change_dir_prop,
  update_close_dir,
  update_add_file,
  update_rep_file,
  update_apply_txdelta,
  update_change_file_prop,
  update_close_file,
};

#if 0
svn_error_t *
svn_ra_dav__get_update_editor(void *session_baton,
                              const svn_delta_edit_fns_t **editor,
                              void **root_dir_baton,
                              const svn_delta_edit_fns_t *wc_update,
                              void *wc_update_baton,
                              svn_string_t *URL)
{
  /* shove the session and wc_* values into our baton */

  *editor = &update_editor;
  *root_dir_baton = NULL;
  return SVN_NO_ERROR;
}
#endif


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
