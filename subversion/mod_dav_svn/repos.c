/*
 * repos.c: mod_dav_svn repository provider functions for Subversion
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



#include <httpd.h>
#include <http_protocol.h>
#include <http_log.h>
#include <mod_dav.h>

#include <apr_strings.h>

#include "svn_types.h"
#include "dav_svn.h"


struct dav_resource_private {
  apr_pool_t *pool;

  /* Path from the SVN repository root to this resource. */
  const char *path;

  /* Remember the root URI of this repository */
  const char *root_dir;

  /* resource-type-specific data */
  const char *object_name;
};

struct dav_stream {
  const dav_resource *res;
  int pos;
};

typedef struct {
  dav_resource res;
  dav_resource_private priv;
} dav_resource_combined;


static int dav_svn_setup_activity(dav_resource_combined *comb,
                                  const char *path)
{
  DBG1("ACTIVITY: %s", path);
  comb->res.type = DAV_RESOURCE_TYPE_ACTIVITY;

  /* ### parse path */
  comb->priv.object_name = path;

  return FALSE;
}

static int dav_svn_setup_version(dav_resource_combined *comb,
                                  const char *path)
{
  comb->res.type = DAV_RESOURCE_TYPE_VERSION;

  /* ### parse path */
  comb->priv.object_name = path;

  return FALSE;
}

static int dav_svn_setup_history(dav_resource_combined *comb,
                                 const char *path)
{
  comb->res.type = DAV_RESOURCE_TYPE_HISTORY;

  /* ### parse path */
  comb->priv.object_name = path;

  return FALSE;
}

static int dav_svn_setup_working(dav_resource_combined *comb,
                                 const char *path)
{
  comb->res.type = DAV_RESOURCE_TYPE_WORKING;
  comb->res.working = TRUE;

  /* ### parse path */
  comb->priv.object_name = path;

  return FALSE;
}

static const struct special_defn
{
  const char *name;
  int (*func)(dav_resource_combined *comb, const char *path);

} special_subdirs[] =
{
  { "act", dav_svn_setup_activity },
  { "ver", dav_svn_setup_version },
  { "his", dav_svn_setup_history },
  { "wrk", dav_svn_setup_working },
  { NULL }
};

static dav_resource * dav_svn_get_resource(request_rec *r,
                                           const char *root_dir,
                                           const char *workspace,
                                           const char *target,
                                           int is_label)
{
  dav_resource_combined *comb;
  apr_size_t len1;
  apr_size_t len2;
  char *uri;
  const char *relative;
  const char *special_uri;
  char ch;

  comb = apr_pcalloc(r->pool, sizeof(*comb));
  comb->res.info = &comb->priv;
  comb->res.hooks = &dav_svn_hooks_repos;
  comb->priv.pool = r->pool;

  /* make a copy so that we can do some work on it */
  uri = apr_pstrdup(r->pool, r->uri);

  /* remove duplicate slashes */
  ap_no2slash(uri);

  /* make sure the URI does not have a trailing "/" */
  len1 = strlen(uri);
  if (len1 > 1 && uri[len1 - 1] == '/')
    uri[len1 - 1] = '\0';

  comb->res.uri = uri;

  /* The URL space defined by the SVN provider is always a virtual
     space. Construct the path relative to the configured Location
     (root_dir). So... the relative location is simply the URL used,
     skipping the root_dir. */
  relative = ap_stripprefix(uri, root_dir);

  /* It is possible that some yin-yang used a trailing slash in their
     Location directive (which was then removed as part of the
     "prefix").  Back up a step if we don't have a leading slash. */
  if (*relative != '/')
      --relative;

  /* "relative" is part of the "uri" string, so it has the proper
     lifetime to store here. */
  comb->priv.path = relative;

  /* We are assuming the root_dir will live at least as long as this
     resource. Considering that it typically comes from the per-dir
     config in mod_dav, this is valid for now. */
  comb->priv.root_dir = root_dir;

  /* Figure out the type of the resource */

  special_uri = dav_svn_get_special_uri(r);

  /* "relative" will have a leading "/" while the special URI does
     not. Take particular care in this comparison. */
  len1 = strlen(relative);
  len2 = strlen(special_uri);
  DBG3("len1=%d  len2=%d  rel=\"%s\"", len1, len2, relative);
  if (len1 > len2
      && memcmp(relative + 1, special_uri, len2) == 0
      && ((ch = relative[1 + len2]) == '/' || ch == '\0'))
    {
      if (ch == '\0')
        {
          /* URI was "/root/$svn". It exists, but has restricted usage. */
          comb->res.type = DAV_RESOURCE_TYPE_PRIVATE;
        }
      else
        {
          const char *skip = relative + 1 + len2 + 1;
          apr_size_t skiplen = len1 - 1 - len2 - 1;
          const struct special_defn *defn = special_subdirs;

          for ( ; defn->name != NULL; ++defn)
            {
              apr_size_t len3 = strlen(defn->name);

              if (skiplen >= len3 && memcmp(skip, defn->name, len3) == 0)
                {
                  if (skip[len3] == '\0')
                    {
                      /* URI was "/root/$svn/XXX". The location exists, but
                         has restricted usage. */
                      comb->res.type = DAV_RESOURCE_TYPE_PRIVATE;
                    }
                  else if (skip[len3] == '/')
                    {
                      if ((*defn->func)(comb, skip + len3 + 1))
                        goto malformed_URI;
                    }
                  else
                    goto malformed_URI;

                  break;
                }
            }

          /* if completed the loop, then it is an unrecognized subdir */
          if (defn->name == NULL)
            goto malformed_URI;
        }
    }
  else
    {
      /* ### look up the resource in the SVN repository */
      comb->res.type = DAV_RESOURCE_TYPE_REGULAR;

      /* ### set comb->res.collection */
    }

#ifdef SVN_DEBUG
  if (comb->res.type == DAV_RESOURCE_TYPE_UNKNOWN)
    {
      DBG0("DESIGN FAILURE: should not be UNKNOWN at this point");
      goto unknown_URI;
    }
#endif

  /* if we are here, then the resource exists */
  comb->res.exists = TRUE;

  /* everything in this URL namespace is versioned */
  comb->res.versioned = TRUE;

  return &comb->res;

 malformed_URI:
  /* A malformed URI error occurs when a URI indicates the "special" area,
     yet it has an improper construction. Generally, this is because some
     doofus typed it in manually or has a buggy client. */
  ap_log_rerror(APLOG_MARK, APLOG_ERR | APLOG_NOERRNO, 0, r,
                "The URI indicated a resource within Subversion's special "
                "resource area, but does not exist. This is generally "
                "caused by a problem in the client software.");

  /* FALLTHROUGH */

 unknown_URI:
  /* Unknown URI. Return NULL to indicate "no resource" */
  return NULL;
}

static dav_resource * dav_svn_get_parent_resource(const dav_resource *resource)
{
  /* ### fill this in */
  return NULL;
}

static int dav_svn_is_same_resource(const dav_resource *res1,
                                    const dav_resource *res2)
{
  /* ### fill this in */
  return 1;
}

static int dav_svn_is_parent_resource(const dav_resource *res1,
                                      const dav_resource *res2)
{
  /* ### fill this in */
  return 1;
}

static dav_error * dav_svn_open_stream(const dav_resource *resource,
                                       dav_stream_mode mode,
                                       dav_stream **stream)
{
  *stream = apr_pcalloc(resource->info->pool, sizeof(*stream));

  (*stream)->res = resource;

  return NULL;
}

static dav_error * dav_svn_close_stream(dav_stream *stream, int commit)
{
  /* ### fill this in */
  return NULL;
}

static dav_error * dav_svn_read_stream(dav_stream *stream, void *buf,
                                       apr_size_t *bufsize)
{
  if (stream->pos) {
    /* EOF */
    *bufsize = 0;
    return NULL;
  }

  if (*bufsize > 10)
    *bufsize = 10;
  memcpy(buf, "123456789\n", *bufsize);
  stream->pos = 1;

  return NULL;
}

static dav_error * dav_svn_write_stream(dav_stream *stream, const void *buf,
                                        apr_size_t bufsize)
{
  /* ### fill this in */
  return NULL;
}

static dav_error * dav_svn_seek_stream(dav_stream *stream,
                                       apr_off_t abs_position)
{
  /* ### fill this in */
  return NULL;
}

static const char * dav_svn_getetag(const dav_resource *resource)
{
  /* ### fix this */
  return "svn-etag";
}

static dav_error * dav_svn_set_headers(request_rec *r,
                                       const dav_resource *resource)
{
  if (!resource->exists)
    return NULL;

  /* make sure the proper mtime is in the request record */
#if 0
  ap_update_mtime(r, resource->info->finfo.mtime);
#endif

  /* ### note that these use r->filename rather than <resource> */
#if 0
  ap_set_last_modified(r);
#endif

  /* generate our etag and place it into the output */
  apr_table_set(r->headers_out, "ETag", dav_svn_getetag(resource));

  /* we accept byte-ranges */
  apr_table_setn(r->headers_out, "Accept-Ranges", "bytes");

  /* set up the Content-Length header */
#if 0
  ap_set_content_length(r, resource->info->finfo.size);
#endif

  /* ### how to set the content type? */
  /* ### until this is resolved, the Content-Type header is busted */

  return NULL;
}

static dav_error * dav_svn_create_collection(dav_resource *resource)
{
  /* ### fill this in */
  return NULL;
}

static dav_error * dav_svn_copy_resource(const dav_resource *src,
                                         dav_resource *dst,
                                         int depth,
                                         dav_response **response)
{
  /* ### fill this in */
  return NULL;
}

static dav_error * dav_svn_move_resource(dav_resource *src,
                                         dav_resource *dst,
                                         dav_response **response)
{
  /* ### fill this in */
  return NULL;
}

static dav_error * dav_svn_remove_resource(dav_resource *resource,
                                           dav_response **response)
{
  /* ### fill this in */
  return NULL;
}

static dav_error * dav_svn_walk(dav_walker_ctx *wctx, int depth)
{
  /* ### fill this in */
  return NULL;
}


const dav_hooks_repository dav_svn_hooks_repos =
{
  1,                            /* special GET handling */
  dav_svn_get_resource,
  dav_svn_get_parent_resource,
  dav_svn_is_same_resource,
  dav_svn_is_parent_resource,
  dav_svn_open_stream,
  dav_svn_close_stream,
  dav_svn_read_stream,
  dav_svn_write_stream,
  dav_svn_seek_stream,
  dav_svn_set_headers,
  NULL,                         /* get_pathname */
  NULL,                         /* free_file */
  dav_svn_create_collection,
  dav_svn_copy_resource,
  dav_svn_move_resource,
  dav_svn_remove_resource,
  dav_svn_walk,
  dav_svn_getetag,
};


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
