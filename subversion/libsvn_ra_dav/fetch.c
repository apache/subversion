/*
 * fetch.c :  routines for fetching updates and checkouts
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 *
 * Portions of this software were originally written by Greg Stein as a
 * sourceXchange project sponsored by SapphireCreek.
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

#include "ra_session.h"


enum {
  ELEM_resourcetype = 0x1000,
  ELEM_collection,
  ELEM_target
};

static const dav_propname fetch_props[] =
{
  { "DAV:", "resourcetype" },
  { "DAV:", "target" },
  { NULL }
};

static const struct hip_xml_elm fetch_elems[] =
{
  { "DAV:", "resourcetype", ELEM_resourcetype, 0 },
  { "DAV:", "collection", ELEM_collection, HIP_XML_CDATA },
  { "DAV:", "target", ELEM_target, 0 },
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

  if (elm->id == ELEM_collection)
    r->is_collection = 1;

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
    r->target_href = apr_pstrdup(fc->pool, cdata);

  return 0;
}

static svn_error_t *
fetch_dirents (svn_ra_session_t *ras,
               const char *url,
               fetch_ctx_t *fc)
{
  hip_xml_parser *hip;
  int rv;

  fc->cur_collection = url;
  fc->dph = dav_propfind_create(ras->sess, url, DAV_DEPTH_ONE);

  dav_propfind_set_resource_handlers(fc->dph, start_resource, end_resource);
  hip = dav_propfind_get_parser(fc->dph);

  hip_xml_add_handler(hip, fetch_elems,
                      validate_element, start_element, end_element, fc);

  rv = dav_propfind_named(fc->dph, fetch_props, fc);
  if (rv != HTTP_OK)
    {
      /* ### raise an error */
    }

  /* ### how to toss dph? */

  return NULL;
}

static void
fetch_file_reader(void *userdata, const char *buf, size_t len)
{
  fetch_ctx_t *fc = userdata;
  svn_txdelta_window_t window = { 0 };
  svn_txdelta_op_t op;
  svn_string_t data = { (char *)buf, len, len };
  svn_error_t *err;

  if (len == 0)
    {
      /* file is complete. */
      /* ### anything to do? */
      return;
    }

  op.action_code = svn_txdelta_new;
  op.offset = 0;
  op.length = len;

  window.num_ops = 1;
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
  svn_vernum_t ancestor_version;
  void *file_baton;
  int rv;

  /* ### */
  ancestor_path = svn_string_create("### ancestor_path ###", fc->pool);
  ancestor_version = 1;

  printf("fetching and saving %s\n", url);

  name = my_basename(url, fc->pool);
  err = (*fc->editor->add_file) (name, fc->edit_baton, fc->cur_baton,
                                 ancestor_path, ancestor_version,
                                 &file_baton);
  if (err)
    return svn_quick_wrap_error(err, "could not add a file");

  err = (*fc->editor->apply_textdelta) (fc->edit_baton, fc->cur_baton,
                                        file_baton,
                                        &fc->handler, &fc->handler_baton);
  if (err)
    return svn_quick_wrap_error(err, "could not save file");

  rv = http_read_file(ras->sess, url, fetch_file_reader, fc);
  if (rv != HTTP_OK)
    {
      /* ### other GET responses? */
    }

  /* ### how to close the handler? */

  /* ### fetch properties */
  /* ### store URL into a local, predefined property */

  /* done with the file */
  return (*fc->editor->finish_file)(fc->edit_baton, file_baton);
}

svn_error_t *
svn_ra_checkout (svn_ra_session_t *ras,
                 const char *start_at,
                 int recurse,
                 const svn_delta_edit_fns_t *editor,
                 void *edit_baton,
                 void *dir_baton)
{
  svn_error_t *err;
  fetch_ctx_t fc = { 0 };
  dir_rec_t *dr;
  svn_string_t *ancestor_path;
  svn_vernum_t ancestor_version;

  fc.editor = editor;
  fc.edit_baton = edit_baton;
  fc.pool = ras->pool;
  fc.subdirs = apr_make_array(ras->pool, 5, sizeof(dir_rec_t));
  fc.files = apr_make_array(ras->pool, 10, sizeof(file_rec_t));

  /* ### join ras->rep_root, start_at */
  dr = apr_push_array(fc.subdirs);
  dr->href = ras->root.path;
  dr->parent_baton = dir_baton;

  /* ### */
  ancestor_path = svn_string_create("### ancestor_path ###", ras->pool);
  ancestor_version = 1;

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

          err = (*editor->finish_directory) (edit_baton, parent_baton);
          if (err)
            return svn_quick_wrap_error(err, "could not finish directory");

          if (fc.subdirs->nelts == 0)
            goto traversal_complete;
        }

      /* add a placeholder. this will be used to signal a finish_directory
         for this directory's baton. */
      dr = apr_push_array(fc.subdirs);
      dr->href = NULL;
      dr->parent_baton = NULL;
      idx = fc.subdirs->nelts - 1;

      err = fetch_dirents(ras, url, &fc);
      if (err)
        return svn_quick_wrap_error(err, "could not fetch directory entries");

      /* we fetched information about the directory successfully. time to
         create the local directory. */
      name = my_basename(url, ras->pool);
      err = (*editor->add_directory) (name, edit_baton, parent_baton,
                                      ancestor_path, ancestor_version,
                                      &this_baton);
      if (err)
        return svn_quick_wrap_error(err, "could not add directory");

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
            return svn_quick_wrap_error(err, "could not checkout a file");
        }
      /* reset the list of files */
      fc.files->nelts = 0;

    } while (recurse && fc.subdirs->nelts > 0);

 traversal_complete:
  ;

  return NULL;
}

/* -------------------------------------------------------------------------
**
** UPDATE HANDLING
**
** ### docco...
*/

static svn_error_t *
update_delete (svn_string_t *name,
               void *edit_baton,
               void *parent_baton)
{
  return NULL;
}

static svn_error_t *
update_add_dir (svn_string_t *name,
                void *edit_baton,
                void *parent_baton,
                svn_string_t *ancestor_path,
                svn_vernum_t ancestor_version,
                void **child_baton)
{
  return NULL;
}

static svn_error_t *
update_rep_dir (svn_string_t *name,
                void *edit_baton,
                void *parent_baton,
                svn_string_t *ancestor_path,
                svn_vernum_t ancestor_version,
                void **child_baton)
{
  return NULL;
}

static svn_error_t *
update_change_dir_prop (void *edit_baton,
                        void *dir_baton,
                        svn_string_t *name,
                        svn_string_t *value)
{
  return NULL;
}

static svn_error_t *
update_change_dirent_prop (void *edit_baton,
                           void *dir_baton,
                           svn_string_t *entry,
                           svn_string_t *name,
                           svn_string_t *value)
{
  return NULL;
}

static svn_error_t *
update_finish_dir (void *edit_baton, void *dir_baton)
{
  return NULL;
}

static svn_error_t *
update_add_file (svn_string_t *name,
                 void *edit_baton,
                 void *parent_baton,
                 svn_string_t *ancestor_path,
                 svn_vernum_t ancestor_version,
                 void **file_baton)
{
  return NULL;
}

static svn_error_t *
update_rep_file (svn_string_t *name,
                 void *edit_baton,
                 void *parent_baton,
                 svn_string_t *ancestor_path,
                 svn_vernum_t ancestor_version,
                 void **file_baton)
{
  return NULL;
}

static svn_error_t *
update_apply_txdelta (void *edit_baton,
                      void *parent_baton,
                      void *file_baton, 
                      svn_txdelta_window_handler_t **handler,
                      void **handler_baton)
{
  return NULL;
}

static svn_error_t *
update_change_file_prop (void *edit_baton,
                         void *parent_baton,
                         void *file_baton,
                         svn_string_t *name,
                         svn_string_t *value)
{
  return NULL;
}

static svn_error_t *
update_finish_file (void *edit_baton, void *file_baton)
{
  return NULL;
}

/*
** This structure is used during the update process. An external caller
** uses these callbacks to describe all the changes in the working copy.
** These are communicated to the server, which then decides how to update
** the client to a specific version/latest/label/etc.
*/
static const svn_delta_edit_fns_t update_editor = {
  update_delete,
  update_add_dir,
  update_rep_dir,
  update_change_dir_prop,
  update_change_dirent_prop,
  update_finish_dir,
  update_add_file,
  update_rep_file,
  update_apply_txdelta,
  update_change_file_prop,
  update_finish_file
};

svn_error_t *
svn_ra_get_update_editor(const svn_delta_edit_fns_t **editor,
                         void **edit_baton,
                         ... /* more params */)
{
  *editor = &update_editor;
  *edit_baton = NULL;
  return NULL;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
