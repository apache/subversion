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
#include "svn_error.h"
#include "svn_fs.h"

#include "dav_svn.h"


/* dav_svn_repos
 *
 * Record information about the repository that a resource belongs to.
 * This structure will be shared between multiple resources so that we
 * can optimized our FS access.
 *
 * Note that we do not refcount this structure. Presumably, we will need
 * it throughout the life of the request. Therefore, we can just leave it
 * for the request pool to cleanup/close.
 *
 * Also, note that it is possible that two resources may have distinct
 * dav_svn_repos structures, yet refer to the same repository. This is
 * allowed by the SVN FS interface.
 *
 * ### should we attempt to merge them when we detect this situation in
 * ### places like is_same_resource, is_parent_resource, or copy/move?
 * ### I say yes: the FS will certainly have an easier time if there is
 * ### only a single FS open; otherwise, it will have to work a bit harder
 * ### to keep the things in sync.
 */
typedef struct {
  apr_pool_t *pool;     /* request_rec -> pool */

  /* Remember the root URL path of this repository (just a path; no
     scheme, host, or port).

     Example: the URI is "http://host/repos/file", this will be "/repos".
  */
  const char *root_uri;

  /* This records the filesystem path to the SVN FS */
  const char *fs_path;

  /* the open repository */
  svn_fs_t *fs;

  /* NOTE: root_rev and root_dir may be 0/NULL if we don't open the root
     of the repository (e.g. we're dealing with activity resources) */
  /* ### these fields may make better sense elsewhere; a repository may
     ### need two roots open for some operations(?) */

  /* what revision did we open for the root? */
  svn_revnum_t root_rev;

  /* the root of the revision tree */
  svn_fs_dir_t *root_dir;
  
} dav_svn_repos;

/* internal structure to hold information about this resource */
struct dav_resource_private {
  apr_pool_t *pool;     /* request_rec -> pool */

  /* Path from the SVN repository root to this resource. This value has
     a leading slash. It will never have a trailing slash, even if the
     resource represents a collection.

     For example: URI is http://host/repos/file -- path will be "/file".

     Note that the SVN FS does not like absolute paths, so we
     generally skip the first char when talking with the FS.
  */
  svn_string_t *path;

  /* resource-type-specific data */
  const char *object_name;      /* ### not really defined right now */

  /* for REGULAR resources: an open node for the revision */
  svn_fs_node_t *node;

  dav_svn_repos *repos;
};

struct dav_stream {
  const dav_resource *res;
  svn_fs_file_t *file;
  svn_read_fn_t *readfn;
  void *baton;
};

typedef struct {
  dav_resource res;
  dav_resource_private priv;
} dav_resource_combined;


static int dav_svn_setup_activity(dav_resource_combined *comb,
                                  const char *path)
{
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
                                           const char *root_uri,
                                           const char *workspace,
                                           const char *target,
                                           int is_label)
{
  const char *fs_path;
  dav_resource_combined *comb;
  dav_svn_repos *repos;
  apr_size_t len1;
  apr_size_t len2;
  char *uri;
  const char *relative;
  const char *special_uri;
  char ch;
  svn_error_t *err;

  if ((fs_path = dav_svn_get_fs_path(r)) == NULL)
    {
      /* ### return an error rather than log it? */
      ap_log_rerror(APLOG_MARK, APLOG_ERR | APLOG_NOERRNO,
                    SVN_ERR_APMOD_MISSING_PATH_TO_FS, r,
                    "The server is misconfigured: an SVNPath directive is "
                    "required to specify the location of this resource's "
                    "repository.");
      return NULL;
    }

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
     (root_uri). So... the relative location is simply the URL used,
     skipping the root_uri.

     Note: mod_dav has canonialized root_uri. It will not have a trailing
           slash (unless it is "/").

     Note: given a URI of /something and a root of /some, then it is
           impossible to be here (and end up with "thing"). This is simply
           because we control /some and are dispatched to here for its
           URIs. We do not control /something, so we don't get here. Or,
           if we *do* control /something, then it is for THAT root.
  */
  relative = ap_stripprefix(uri, root_uri);

  /* We want a leading slash on the path specified by <relative>. This
     will almost always be the case since root_uri does not have a trailing
     slash. However, if the root is "/", then the slash will be removed
     from <relative>. Backing up a character will put the leading slash
     back. */
  if (*relative != '/')
      --relative;

  /* "relative" is part of the "uri" string, so it has the proper
     lifetime to store here. */
  comb->priv.path = svn_string_create(relative, r->pool);

  repos = apr_pcalloc(r->pool, sizeof(*repos));
  comb->priv.repos = repos;

  /* We are assuming the root_uri will live at least as long as this
     resource. Considering that it typically comes from the per-dir
     config in mod_dav, this is valid for now. */
  repos->root_uri = root_uri;

  /* where is the SVN FS for this resource? */
  repos->fs_path = fs_path;

  /* open the SVN FS */
  repos->fs = svn_fs_new(r->pool);
  err = svn_fs_open_berkeley(repos->fs, fs_path);
  if (err != NULL)
    {
      /* ### do something with err */
      /* ### return an error rather than log it? */
      ap_log_rerror(APLOG_MARK, APLOG_ERR | APLOG_NOERRNO, err->apr_err, r,
                    "Could not open the SVN filesystem at %s", fs_path);
      return NULL;
    }


  /* Figure out the type of the resource */

  special_uri = dav_svn_get_special_uri(r);

  /* "relative" will have a leading "/" while the special URI does
     not. Take particular care in this comparison. */
  len1 = strlen(relative);
  len2 = strlen(special_uri);
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
      /* ### no way to ask for "head" */
      /* ### note that we won't *always* go for the head... if this resource
         ### corresponds to a Version Resource, then we have a specific
         ### version to ask for.
      */
      repos->root_rev = 1;

      /* get the root of the tree */
      err = svn_fs_open_root(&repos->root_dir, repos->fs, repos->root_rev);
      if (err != NULL)
        {
          /* ### do something with err */
          /* ### return an error rather than log it? */
          ap_log_rerror(APLOG_MARK, APLOG_ERR | APLOG_NOERRNO, err->apr_err, r,
                        "Could not open the root of the repository");
          return NULL;
        }

      /* open the node itself */
      /* ### what happens if we want to modify this node?
         ### well, you can't change a REGULAR resource, so this is probably
         ### going to be fine. a WORKING resource will have more work
      */
      if (strcmp(relative, "/") == 0)
        {
          comb->priv.node = svn_fs_dir_to_node(repos->root_dir);
          comb->res.collection = 1;
        }
      else
        {
          svn_string_t relpath = *comb->priv.path;

          /* open the requested resource. note that we skip the leading "/" */
          ++relpath.data;
          --relpath.len;
          err = svn_fs_open_node(&comb->priv.node, repos->root_dir, &relpath,
                                 r->pool);
          if (err != NULL)
            {
              /* ### do something with err */
              /* ### return an error rather than log it? */
              ap_log_rerror(APLOG_MARK, APLOG_ERR | APLOG_NOERRNO,
                            err->apr_err, r,
                            "Could not open the resource '%s'", relative);
              return NULL;
            }

          comb->res.collection = svn_fs_node_is_dir(comb->priv.node);
        }

      comb->res.type = DAV_RESOURCE_TYPE_REGULAR;
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
  /* ### is it? why are activities, version, and working resources marked
     ### as "versioned"? */
  comb->res.versioned = TRUE;

  return &comb->res;

 malformed_URI:
  /* A malformed URI error occurs when a URI indicates the "special" area,
     yet it has an improper construction. Generally, this is because some
     doofus typed it in manually or has a buggy client. */
  /* ### return an error rather than log it? */
  ap_log_rerror(APLOG_MARK, APLOG_ERR | APLOG_NOERRNO,
                SVN_ERR_APMOD_MALFORMED_URI, r,
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
  svn_string_t *path = resource->info->path;

  /* the root of the repository has no parent */
  if (path->len == 1 && *path->data == '/')
    return NULL;

  /* ### fill this in */
  /* ### note: only needed for methods which modify the repository */
  return NULL;
}

/* does RES2 live in the same repository as RES1? */
static int is_our_resource(const dav_resource *res1,
                           const dav_resource *res2)
{
  if (res1->hooks != res2->hooks
      || strcmp(res1->info->repos->fs_path, res2->info->repos->fs_path) != 0)
    {
      /* a different provider, or a different FS repository */
      return 0;
    }

  /* coalesce the repository */
  if (res1->info->repos != res2->info->repos)
    {
        /* ### crap. what to do with this... */
        (void) svn_fs_close_fs(res2->info->repos->fs);
        res2->info->repos = res1->info->repos;
    }

  return 1;
}

static int dav_svn_is_same_resource(const dav_resource *res1,
                                    const dav_resource *res2)
{
  if (!is_our_resource(res1, res2))
    return 0;

  return svn_string_compare(res1->info->path, res2->info->path);
}

static int dav_svn_is_parent_resource(const dav_resource *res1,
                                      const dav_resource *res2)
{
  apr_size_t len1 = strlen(res1->info->path->data);
  apr_size_t len2;

  if (!is_our_resource(res1, res2))
    return 0;

  /* res2 is one of our resources, we can use its ->info ptr */
  len2 = strlen(res2->info->path->data);

  return (len2 > len1
          && memcmp(res1->info->path->data, res2->info->path->data, len1) == 0
          && res2->info->path->data[len1] == '/');
}

static dav_error * dav_svn_open_stream(const dav_resource *resource,
                                       dav_stream_mode mode,
                                       dav_stream **stream)
{
  dav_resource_private *info = resource->info;
#if 0
  svn_error_t *err;
#endif

  /* start building the stream structure */
  *stream = apr_pcalloc(info->pool, sizeof(**stream));
  (*stream)->res = resource;

#if 0
  /* get an FS file object for the resource [from the node] */
  (*stream)->file = svn_fs_node_to_file(info->node);
  /* assert: file != NULL   (we shouldn't be here if node is a DIR) */

  /* ### assuming mode == read for now */

  err = svn_fs_file_contents(&(*stream)->readfn, &(*stream)->baton,
                             (*stream)->file, info->pool);
  if (err != NULL)
    {
      return dav_svn_convert_err(err, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not prepare to read the file");
    }
#endif

  return NULL;
}

static dav_error * dav_svn_close_stream(dav_stream *stream, int commit)
{
  /* ### anything that needs to happen with stream->baton? */

#if 0
  svn_fs_close_file(stream->file);
#endif

  return NULL;
}

static dav_error * dav_svn_read_stream(dav_stream *stream, void *buf,
                                       apr_size_t *bufsize)
{
  svn_error_t *err;

  err = (*stream->readfn)(stream->baton, buf, bufsize,
                          stream->res->info->pool);
  if (err != NULL)
    {
      return dav_svn_convert_err(err, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not read the file contents");
    }

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
  const svn_fs_id_t id[] = { 1, 1, -1 }; /* ### temp, until we can fetch */
  svn_string_t *idstr;

  if (!resource->exists)
    return "";

  /* ### what kind of etag to return for collections, activities, etc? */

  /* ### fetch the id from the node */

  idstr = svn_fs_unparse_id(id, resource->info->pool);
  return apr_psprintf(resource->info->pool, "\"%s\"", idstr);
}

static dav_error * dav_svn_set_headers(request_rec *r,
                                       const dav_resource *resource)
{
#if 0
  apr_off_t length;
#endif

  if (!resource->exists)
    return NULL;

  /* ### what to do for collections, activities, etc */

  /* make sure the proper mtime is in the request record */
#if 0
  ap_update_mtime(r, resource->info->finfo.mtime);
#endif

  /* ### note that these use r->filename rather than <resource> */
#if 0
  ap_set_last_modified(r);
#endif

  /* generate our etag and place it into the output */
  apr_table_setn(r->headers_out, "ETag", dav_svn_getetag(resource));

  /* we accept byte-ranges */
  apr_table_setn(r->headers_out, "Accept-Ranges", "bytes");

  /* set up the Content-Length header */
#if 0
  /* ### need to get FILE */
  err = svn_fs_file_length(&length, file);
  if (err != NULL)
    {
      return dav_svn_convert_err(err, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not fetch the resource length");
    }
  ap_set_content_length(r, length);
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
  /* ### see svn_fs_dir_entries() */

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
