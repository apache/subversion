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



#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_strings.h>

#include <dav_basic.h>
#include <dav_207.h>
#include <hip_xml.h>
#include <http_utils.h>
#include <dav_props.h>

#include "svn_delta.h"
#include "svn_ra.h"

#include "ra_session.h"


enum {
  ELEM_resourcetype,
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
  { NULL }
};

typedef struct {
  const char *href;
  const char *target_href;
} file_rec_t;

typedef struct {
  const char *href;
  int is_collection;
  const char *target_href;
} resource_t;

typedef struct {
  const char *cur_collection;
  const char *cur_wc_dir;

  dav_propfind_handler *dph;

  apr_array_header_t *subdirs;  /* URL paths of subdirs to scan */
  apr_array_header_t *files;

  apr_pool_t *pool;
} fetch_ctx_t;

static void *
start_resource (void *userdata, const char *href)
{
  fetch_ctx_t *fc = userdata;
  resource_t *r = apr_pcalloc(fc->pool, sizeof(*r));

  /* printf("start_resource: %s\n", href); */

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
          *(const char **)apr_push_array(fc->subdirs) = href.path;

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

  if (parent == ELEM_target && child != DAV_ELM_href)
    return HIP_XML_INVALID;
  if (child == ELEM_collection && parent != ELEM_resourcetype)
    return HIP_XML_INVALID;

  return HIP_XML_VALID;
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

  /* ### we don't have DAV_ELM_href in fetch_elems, so we never see this!
     ### waiting on feedback from Joe */
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
    }

  /* ### how to toss dph? */

  return NULL;
}

static svn_error_t *
fetch_data (svn_ra_session_t *ras,
            const char *start_at,
            int recurse,
            svn_delta_walk_t *walker,
            void *walk_baton,
            void *dir_baton,
            apr_pool_t *pool)
{
  svn_error_t *err;
  fetch_ctx_t fc = { 0 };

  fc.pool = pool;
  fc.subdirs = apr_make_array(pool, 5, sizeof(const char *));
  fc.files = apr_make_array(pool, 10, sizeof(file_rec_t));

  /* ### join ras->rep_root, start_at */
  *(const char **)apr_push_array(fc.subdirs) = ras->root.path;

  while (fc.subdirs->nelts > 0)
    {
      const char *url;

      /* pop a subdir off the stack */
      url = ((const char **)fc.subdirs->elts)[--fc.subdirs->nelts];

      err = fetch_dirents(ras, url, &fc);
      if (err)
        return svn_quick_wrap_error(err, "could not fetch directory entries");

      /* process each of the files that were found */
      /* ### */ fc.files->nelts = 0;
    }

  return NULL;
}

svn_error_t *
svn_ra_update (svn_ra_session_t *ras,
               const char *start_at,
               int recurse,
               svn_delta_walk_t *walker,
               void *walk_baton,
               void *dir_baton,
               apr_pool_t *pool)
{
  return fetch_data(ras, start_at, recurse, walker, walk_baton,
                    dir_baton, pool);
}

svn_error_t *
svn_ra_checkout (svn_ra_session_t *ras,
                 const char *start_at,
                 int recurse,
                 svn_delta_walk_t *walker,
                 void *walk_baton,
                 void *dir_baton,
                 apr_pool_t *pool)
{
  return fetch_data(ras, start_at, recurse, walker, walk_baton,
                    dir_baton, pool);
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
