/*
 * commit.c :  routines for committing changes to the server
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



#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_uuid.h>
#include <apr_portable.h>

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#if APR_HAVE_STDLIB
#include <stdlib.h>     /* for free() */
#endif

#include <ne_request.h>
#include <ne_props.h>
#include <ne_basic.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_io.h"
#include "svn_ra.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_dav.h"

#include "ra_dav.h"


/*
** resource_t: identify the relevant pieces of a resource on the server
**
** REVISION is the resource's revision, or SVN_INVALID_REVNUM if it is
** new or is the HEAD.
**
** URL refers to the public/viewable/original resource.
** VSN_URL refers to the version resource that we stored locally
** WR_URL refers to a working resource for this resource
**
** Note that VSN_URL is NULL if this resource has just been added, and
** WR_URL can be NULL if the resource has not (yet) been checked out.
**
** LOCAL_PATH is relative to the root of the commit. It will be used
** for the get_func, set_func, and close_func callbacks.
*/
typedef struct
{
  svn_revnum_t revision;
  const char *url;
  const char *vsn_url;
  const char *wr_url;
  const char *local_path;

} resource_t;

typedef struct
{
  svn_ra_session_t *ras;
  const char *activity_url;

  /* ### resources may not be needed */
  apr_hash_t *resources;        /* URL (const char *) -> RESOURCE_T */

  apr_hash_t *valid_targets;

  svn_ra_get_wc_prop_func_t get_func;
  svn_ra_set_wc_prop_func_t set_func;
  void *cb_baton;

  /* The (potential) author of this commit. */
  const char *user;

  /* Log message for the commit. */
  const char *log_msg;

  /* The new revision created by this commit. */
  svn_revnum_t *new_rev;

  /* The date (according to the repository) of this commit. */
  const char **committed_date;

  /* The author (also according to the repository) of this commit. */
  const char **committed_author;

} commit_ctx_t;

typedef struct
{
  commit_ctx_t *cc;
  resource_t *rsrc;
  apr_table_t *prop_changes; /* name/values pairs of changed (or new) properties. */
  apr_array_header_t *prop_deletes; /* names of properties to delete. */
  svn_boolean_t created; /* set if this is an add rather than an update */
} resource_baton_t;

typedef struct
{
  apr_pool_t *pool;
  apr_file_t *tmpfile;
  svn_stringbuf_t *fname;
  resource_baton_t *file;
} put_baton_t;

/*
** singleton_delete_prop:
**
** The address of this integer is used as a "singleton" value to mark
** properties which must be deleted. Properties which are changed/added
** will use their new values.
*/
static const int singleton_delete_prop = 0;
#define DELETE_THIS_PROP (&singleton_delete_prop)

/* this property will be fetched from the server when we don't find it
   cached in the WC property store. */
static const ne_propname fetch_props[] =
{
  { "DAV:", "checked-in" },
  { NULL }
};

static const ne_propname log_message_prop = { SVN_PROP_PREFIX, "log" };


static svn_error_t * simple_request(svn_ra_session_t *ras, const char *method,
                                    const char *url, int *code,
                                    int okay_1, int okay_2)
{
  ne_request *req;
  const char *url_str = svn_path_uri_encode(url, ras->pool);

  /* create/prep the request */
  req = ne_request_create(ras->sess, method, url_str);
  if (req == NULL)
    {
      return svn_error_createf(SVN_ERR_RA_CREATING_REQUEST, 0, NULL,
                               ras->pool,
                               "Could not create a request (%s %s)",
                               method, url_str);
    }

  /* run the request and get the resulting status code (and svn_error_t) */
  SVN_ERR( svn_ra_dav__request_dispatch(code, req, ras->sess,
                                        method, url, okay_1, okay_2,
                                        ras->pool) );

  return SVN_NO_ERROR;
}

static svn_error_t * get_version_url(commit_ctx_t *cc,
                                     resource_t *rsrc,
                                     svn_boolean_t force,
                                     apr_pool_t *pool)
{
  svn_ra_dav_resource_t *propres;
  const char *url;

  if (!force && cc->get_func != NULL)
    {
      const svn_string_t *vsn_url_value;

      SVN_ERR( (*cc->get_func)(cc->cb_baton,
                               rsrc->local_path,
                               SVN_RA_DAV__LP_VSN_URL,
                               &vsn_url_value,
                               pool) );
      if (vsn_url_value != NULL)
        {
          rsrc->vsn_url = vsn_url_value->data;
          return NULL;
        }

      /* whoops. it wasn't there. go grab it from the server. */
    }

  if (rsrc->revision == SVN_INVALID_REVNUM)
    {
      /* We aren't trying to get a specific version -- use the HEAD. We
         fetch the version URL from the public URL. */
      url = rsrc->url;
    }
  else
    {
      svn_string_t bc_url;
      svn_string_t bc_relative;

      /* The version URL comes from a resource in the Baseline Collection. */
      SVN_ERR( svn_ra_dav__get_baseline_info(NULL,
                                             &bc_url, &bc_relative, NULL,
                                             cc->ras->sess,
                                             rsrc->url,
                                             rsrc->revision,
                                             pool));

      url = svn_path_join(bc_url.data, bc_relative.data, pool);
    }

  /* Get the DAV:checked-in property, which contains the URL of the
     Version Resource */
  SVN_ERR( svn_ra_dav__get_props_resource(&propres, cc->ras->sess, url,
                                          NULL, fetch_props, pool) );
  url = apr_hash_get(propres->propset,
                     SVN_RA_DAV__PROP_CHECKED_IN,
                     APR_HASH_KEY_STRING);
  if (url == NULL)
    {
      /* ### need a proper SVN_ERR here */
      return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                              "Could not fetch the Version Resource URL "
                              "(needed during an import or when it is "
                              "missing from the local, cached props).");
    }

  /* ensure we get the proper lifetime for this URL since it is going into
     a resource object. */
  rsrc->vsn_url = apr_pstrdup(cc->ras->pool, url);

  if (cc->set_func != NULL)
    {
      /* save the (new) version resource URL into the item */

      svn_string_t value;

      value.data = url;
      value.len = strlen(url);
      SVN_ERR( (*cc->set_func)(cc->cb_baton,
                               rsrc->local_path,
                               SVN_RA_DAV__LP_VSN_URL,
                               &value,
                               pool) );
    }

  return SVN_NO_ERROR;
}

/* When FORCE is true, then we force a query to the server, ignoring any
   cached property. */
static svn_error_t * get_activity_collection(
  commit_ctx_t *cc,
  const svn_string_t **activity_coll,
  svn_boolean_t force)
{
  if (!force && cc->get_func != NULL)
    {
      /* with a get_func, we can just ask for the activity URL from the
         property store. */

      /* get the URL where we should create activities */
      SVN_ERR( (*cc->get_func)(cc->cb_baton,
                               ".",
                               SVN_RA_DAV__LP_ACTIVITY_COLL,
                               activity_coll,
                               cc->ras->pool) );

      if (*activity_coll != NULL)
        {
          /* the property was there. return it. */
          return SVN_NO_ERROR;
        }

      /* property not found for some reason. get it from the server. */
    }

  /* use our utility function to fetch the activity URL */
  SVN_ERR( svn_ra_dav__get_activity_collection(activity_coll,
                                               cc->ras,
                                               cc->ras->root.path,
                                               cc->ras->pool) );

  if (cc->set_func != NULL)
    {
      /* save the (new) activity collection URL into the directory */
      SVN_ERR( (*cc->set_func)(cc->cb_baton,
                               ".",
                               SVN_RA_DAV__LP_ACTIVITY_COLL,
                               *activity_coll,
                               cc->ras->pool) );
    }

  return SVN_NO_ERROR;
}

static svn_error_t * create_activity(commit_ctx_t *cc)
{
  const svn_string_t * activity_collection;
  apr_uuid_t uuid;
  char uuid_buf[APR_UUID_FORMATTED_LENGTH + 1];
  int code;
  const char *url;

  /* the URL for our activity will be ACTIVITY_COLL/UUID */
  apr_uuid_get(&uuid);
  apr_uuid_format(uuid_buf, &uuid);

  /* get the URL where we'll create activities, construct the URL
     for the activity, and create the activity. */
  SVN_ERR( get_activity_collection(cc, &activity_collection, FALSE) );
  url = svn_path_join(activity_collection->data, uuid_buf, cc->ras->pool);
  SVN_ERR( simple_request(cc->ras, "MKACTIVITY", url, &code,
                          201 /* Created */,
                          404 /* Not Found */) );

  /* if we get a 404, then it generally means that the cached activity
     collection no longer exists. Retry the sequence, but force a query
     to the server for the activity collection. */
  if (code == 404)
    {
      SVN_ERR( get_activity_collection(cc, &activity_collection, TRUE) );
      url = svn_path_join(activity_collection->data, uuid_buf, cc->ras->pool);
      SVN_ERR( simple_request(cc->ras, "MKACTIVITY", url, &code, 201, 0) );
    }

  cc->activity_url = url;

  return SVN_NO_ERROR;
}

/* add a child resource. TEMP_POOL should be as "temporary" as possible,
   but probably not as far as requiring a new temp pool. */
static svn_error_t * add_child(resource_t **child,
                               commit_ctx_t *cc,
                               const resource_t *parent,
                               const char *name,
                               int created,
                               svn_revnum_t revision,
                               apr_pool_t *temp_pool)
{
  /* use ras->pool for the proper lifetime */
  apr_pool_t *pool = cc->ras->pool;
  resource_t *rsrc;

  /* ### todo:  This from Yoshiki Hayashi <yoshiki@xemacs.org>:

     Probably created flag in add_child can be removed because
        revision is valid => created is false 
        revision is invalid => created is true
  */

  rsrc = apr_pcalloc(pool, sizeof(*rsrc));
  rsrc->revision = revision;
  rsrc->url = svn_path_join(parent->url, name, pool);
  rsrc->local_path = svn_path_join(parent->local_path, name, pool);

  /* Case 1:  the resource is truly "new".  Either it was added as a
     completely new object, or implicitly created via a COPY.  Either
     way, it has no VR URL anywhere.  However, we *can* derive its WR
     URL by the rules of deltaV:  "copy structure is preserved below
     the WR you COPY to."  */
  if (created || (parent->vsn_url == NULL))
    {
      rsrc->wr_url = svn_path_join(parent->wr_url, name, pool);
    }

  /* Case 2: the resource is already under version-control somewhere.
     This means it has a VR URL already, and the WR URL won't exist
     until it's "checked out". */
  else
    SVN_ERR( get_version_url(cc, rsrc, FALSE, temp_pool) );

  apr_hash_set(cc->resources, rsrc->url, APR_HASH_KEY_STRING, rsrc);

  *child = rsrc;
  return SVN_NO_ERROR;
}

static svn_error_t * do_checkout(commit_ctx_t *cc,
                                 const char *vsn_url,
                                 svn_boolean_t allow_404,
                                 int *code,
                                 const char **locn)
{
  ne_request *req;
  const char *body;
  const char *url_str;

  /* assert: vsn_url != NULL */
  url_str = svn_path_uri_encode(vsn_url, cc->ras->pool);

  /* ### send a CHECKOUT resource on vsn_url; include cc->activity_url;
     ### place result into res->wr_url and return it */

  /* create/prep the request */
  req = ne_request_create(cc->ras->sess, "CHECKOUT", url_str);
  if (req == NULL)
    {
      return svn_error_createf(SVN_ERR_RA_CREATING_REQUEST, 0, NULL,
                               cc->ras->pool,
                               "Could not create a CHECKOUT request (%s)",
                               url_str);
    }

  /* ### store this into cc to avoid pool growth */
  body = apr_psprintf(cc->ras->pool,
                      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                      "<D:checkout xmlns:D=\"DAV:\">"
                      "<D:activity-set>"
                      "<D:href>%s</D:href>"
                      "</D:activity-set></D:checkout>", cc->activity_url);
  ne_set_request_body_buffer(req, body, strlen(body));

  /* 
   * We have different const qualifiers here. locn is const char **,
   * but the prototype is void * (as opposed to const void *).
   */
  ne_add_response_header_handler(req, "location",
                                 ne_duplicate_header, (void *)locn);

  /* run the request and get the resulting status code (and svn_error_t) */
  return svn_ra_dav__request_dispatch(code, req, cc->ras->sess,
                                      "CHECKOUT", url_str,
                                      201 /* Created */,
                                      allow_404 ? 404 /* Not Found */ : 0,
                                      cc->ras->pool);
}

/* check out the specified resource (if it hasn't been checked out yet) */
static svn_error_t * checkout_resource(commit_ctx_t *cc,
                                       resource_t *res,
                                       svn_boolean_t allow_404)
{
  int code;
  const char *locn = NULL;
  struct uri parse;
  svn_error_t *err;

  if (res->wr_url != NULL)
    {
      /* already checked out! */
      return NULL;
    }

  /* check out the Version Resource */
  err = do_checkout(cc, res->vsn_url, allow_404, &code, &locn);

  /* possibly run the request again, with a re-fetched Version Resource */
  if (err == NULL && allow_404 && code == 404)
    {
      /* re-fetch, forcing a query to the server */
      SVN_ERR( get_version_url(cc, res, TRUE, cc->ras->pool) );

      /* do it again, but don't allow a 404 this time */
      err = do_checkout(cc, res->vsn_url, FALSE, &code, &locn);
    }

  /* special-case when conflicts occur */
  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_CONFLICT)
        return svn_error_createf(err->apr_err, err->src_err, err, NULL,
                                 "Your file '%s' is probably out-of-date.",
                                 res->local_path);
      return err;
    }

  /* we got the header, right? */
  if (locn == NULL)
    {
      return svn_error_create(SVN_ERR_RA_REQUEST_FAILED, 0, NULL,
                              cc->ras->pool,
                              "The CHECKOUT response did not contain a "
                              "Location: header.");
    }

  /* The location is an absolute URI. We want just the path portion. */
  /* ### what to do with the rest? what if it points somewhere other
     ### than the current session? */
  uri_parse(locn, &parse, NULL);
  res->wr_url = apr_pstrdup(cc->ras->pool, parse.path);
  uri_free(&parse);
  free((void *)locn);

  return SVN_NO_ERROR;
}

static void record_prop_change(apr_pool_t *pool,
                               resource_baton_t *r,
                               const char *name,
                               const svn_string_t *value)
{
  /* copy the name into the pool so we get the right lifetime (who knows
     what the caller will do with it) */
  /* ### this isn't strictly need for the table case, but we're going
     ### to switch that to a hash soon */
  name = apr_pstrdup(pool, name);

  if (value)
    {
      svn_stringbuf_t *escaped = NULL;

      /* changed/new property */
      if (r->prop_changes == NULL)
        r->prop_changes = apr_table_make(pool, 5);

      svn_xml_escape_string(&escaped, value, pool);
      apr_table_set(r->prop_changes, name, escaped->data);
    }
  else
    {
      /* deleted property. */

      if (r->prop_deletes == NULL)
        r->prop_deletes = apr_array_make(pool, 5, sizeof(char *));
  
      *(const char **)apr_array_push(r->prop_deletes) = name;
    }
}

/* Callback iterator for apr_table_do below. */
static int do_setprop(void *rec, const char *name, const char *value)
{
  ne_buffer *body = rec;

  /* use custom prefix for anything that doesn't start with "svn:" */
  if (strncmp(name, "svn:", 4) == 0)
    ne_buffer_concat(body, "<S:", name + 4, ">", value, "</S:", name + 4, ">",
                     NULL);
  else
    ne_buffer_concat(body, "<C:", name, ">", value, "</C:", name, ">", NULL);

  return 1;
}

static svn_error_t * do_proppatch(svn_ra_session_t *ras,
                                  const resource_t *rsrc,
                                  resource_baton_t *rb)
{
  ne_request *req;
  int code;
  ne_buffer *body; /* ### using an ne_buffer because it can realloc */
  const char *url_str;
  svn_error_t *err;

  /* just punt if there are no changes to make. */
  if ((rb->prop_changes == NULL || apr_is_empty_table(rb->prop_changes))
      && (rb->prop_deletes == NULL || rb->prop_deletes->nelts == 0))
    return NULL;

  /* easier to roll our own PROPPATCH here than use ne_proppatch(), which 
   * doesn't really do anything clever. */
  body = ne_buffer_create();

  ne_buffer_zappend(body,
                    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>" DEBUG_CR
                    "<D:propertyupdate xmlns:D=\"DAV:\" xmlns:C=\""
                    SVN_PROP_CUSTOM_PREFIX "\" xmlns:S=\"svn:\">");

  if (rb->prop_changes != NULL)
    {
      ne_buffer_zappend(body, "<D:set><D:prop>");
      apr_table_do(do_setprop, body, rb->prop_changes, NULL);      
      ne_buffer_zappend(body, "</D:prop></D:set>");
    }
  
  if (rb->prop_deletes != NULL)
    {
      int n;

      ne_buffer_zappend(body, "<D:remove><D:prop>");
      
      for (n = 0; n < rb->prop_deletes->nelts; n++) 
        {
          const char *name = APR_ARRAY_IDX(rb->prop_deletes, n, const char *);

          /* use custom prefix for anything that doesn't start with "svn:" */
          if (strncmp(name, "svn:", 4) == 0)
            ne_buffer_concat(body, "<S:", name + 4, "/>", NULL);
          else
            ne_buffer_concat(body, "<C:", name, "/>", NULL);
        }

      ne_buffer_zappend(body, "</D:prop></D:remove>");

    }

  ne_buffer_zappend(body, "</D:propertyupdate>");

  url_str = svn_path_uri_encode(rsrc->wr_url, ras->pool);
  req = ne_request_create(ras->sess, "PROPPATCH", url_str);

  ne_set_request_body_buffer(req, body->data, ne_buffer_size(body));
  ne_add_request_header(req, "Content-Type", "text/xml; charset=UTF-8");

  /* run the request and get the resulting status code (and svn_error_t) */
  err = svn_ra_dav__request_dispatch(&code, req, ras->sess, "PROPPATCH",
                                     url_str,
                                     207 /* Multistatus */,
                                     0 /* nothing else allowed */,
                                     ras->pool);

  ne_buffer_destroy(body);

  return err;
}


static void
add_valid_target (commit_ctx_t *cc,
                  const char *path,
                  enum svn_recurse_kind kind)
{
  apr_hash_t *hash = cc->valid_targets;
  apr_hash_set (hash, path, APR_HASH_KEY_STRING, &kind);
}


static svn_error_t * commit_open_root(void *edit_baton,
                                      svn_revnum_t base_revision,
                                      apr_pool_t *dir_pool,
                                      void **root_baton)
{
  commit_ctx_t *cc = edit_baton;
  resource_baton_t *root;
  resource_t *rsrc;

  /* create the root resource. no wr_url (yet). use ras->pool for the
     proper lifetime of the resource. */
  rsrc = apr_pcalloc(cc->ras->pool, sizeof(*rsrc));

  /* ### should this be 'base_revision' here? we might not always be
     ### working against the head! (think "properties"). */
  rsrc->revision = SVN_INVALID_REVNUM;

  rsrc->url = cc->ras->root.path;
  rsrc->local_path = "";

  SVN_ERR( get_version_url(cc, rsrc, FALSE, dir_pool) );

  apr_hash_set(cc->resources, rsrc->url, APR_HASH_KEY_STRING, rsrc);

  root = apr_pcalloc(dir_pool, sizeof(*root));
  root->cc = cc;
  root->rsrc = rsrc;
  root->created = FALSE;

  *root_baton = root;

  return SVN_NO_ERROR;
}

static svn_error_t * commit_delete_entry(const char *path,
                                         svn_revnum_t revision,
                                         void *parent_baton,
                                         apr_pool_t *pool)
{
  resource_baton_t *parent = parent_baton;
  const char *name = svn_path_basename(path, pool);
  const char *child;
  int code;

  /* get the URL to the working collection */
  SVN_ERR( checkout_resource(parent->cc, parent->rsrc, TRUE) );

  /* create the URL for the child resource */
  child = svn_path_join(parent->rsrc->wr_url, name, pool);

  /* Note: the child cannot have a resource stored in the resources table
     because of the editor traversal rules. That is: this is the first time
     we have seen anything about the child, and we're deleting it. As a
     corollary, we know the child hasn't been checked out. */

  /* delete the child resource */
  /* ### 404 is ignored, because mod_dav_svn is effectively merging
     against the HEAD revision on-the-fly.  In such a universe, a
     failed deletion (because it's already missing) is OK;  deletion
     is an idempotent merge operation. */
  SVN_ERR( simple_request(parent->cc->ras, "DELETE", child, &code,
                          204 /* Created */, 404 /* Not Found */) );

  /* Add this path to the valid targets hash. */
  add_valid_target (parent->cc, path, svn_nonrecursive);
  
  return SVN_NO_ERROR;
}

static svn_error_t * commit_add_dir(const char *path,
                                    void *parent_baton,
                                    const char *copyfrom_path,
                                    svn_revnum_t copyfrom_revision,
                                    apr_pool_t *dir_pool,
                                    void **child_baton)
{
  resource_baton_t *parent = parent_baton;
  resource_baton_t *child;
  int code;
  const char *name = svn_path_basename(path, dir_pool);

  /* check out the parent resource so that we can create the new collection
     as one of its children. */
  SVN_ERR( checkout_resource(parent->cc, parent->rsrc, TRUE) );

  /* create a child object that contains all the resource urls */
  child = apr_pcalloc(dir_pool, sizeof(*child));
  child->cc = parent->cc;
  child->created = TRUE;
  SVN_ERR( add_child(&child->rsrc, parent->cc, parent->rsrc,
                     name, 1, SVN_INVALID_REVNUM, dir_pool) );

  if (! copyfrom_path)
    {
      /* This a new directory with no history, so just create a new,
         empty collection */
      SVN_ERR( simple_request(parent->cc->ras, "MKCOL", child->rsrc->wr_url,
                              &code, 201 /* Created */, 0) );
    }
  else
    {
      svn_string_t bc_url, bc_relative;
      const char *copy_src;
      int status;

      /* This add has history, so we need to do a COPY. */
      
      /* Convert the copyfrom_* url/rev "public" pair into a Baseline
         Collection (BC) URL that represents the revision -- and a
         relative path under that BC.  */
      SVN_ERR( svn_ra_dav__get_baseline_info(NULL,
                                             &bc_url, &bc_relative, NULL,
                                             parent->cc->ras->sess,
                                             copyfrom_path,
                                             copyfrom_revision,
                                             dir_pool));


      /* Combine the BC-URL and relative path; this is the main
         "source" argument to the COPY request.  The "Destination:"
         header given to COPY is simply the wr_url that is already
         part of the child object. */
      copy_src = svn_path_join(bc_url.data, bc_relative.data, dir_pool);

      /* Have neon do the COPY. */
      status = ne_copy(parent->cc->ras->sess,
                       1,                   /* overwrite */
                       NE_DEPTH_INFINITE,   /* always copy dirs deeply */
                       copy_src,            /* source URI */
                       child->rsrc->wr_url); /* dest URI */

      if (status != NE_OK)
        {
          const char *msg = apr_psprintf(dir_pool, "COPY of %s", path);
          return svn_ra_dav__convert_error(parent->cc->ras->sess,
                                           msg, status, dir_pool);
        }
    }

  /* Add this path to the valid targets hash. */
  add_valid_target (parent->cc, path, 
                    copyfrom_path ? svn_recursive : svn_nonrecursive);

  *child_baton = child;
  return SVN_NO_ERROR;
}

static svn_error_t * commit_open_dir(const char *path,
                                     void *parent_baton,
                                     svn_revnum_t base_revision,
                                     apr_pool_t *dir_pool,
                                     void **child_baton)
{
  resource_baton_t *parent = parent_baton;
  resource_baton_t *child = apr_pcalloc(dir_pool, sizeof(*child));
  const char *name = svn_path_basename(path, dir_pool);

  child->cc = parent->cc;
  child->created = FALSE;
  SVN_ERR( add_child(&child->rsrc, parent->cc, parent->rsrc,
                     name, 0, base_revision, dir_pool) );

  /*
  ** Note: open_dir simply means that a change has occurred somewhere
  **       within this directory. We have nothing to do, to prepare for
  **       those changes (each will be considered independently).
  **
  ** Note: if a directory is replaced by something else, then this callback
  **       will not be used: a true replacement is modeled with a "delete"
  **       followed by an "add".
  */

  *child_baton = child;
  return SVN_NO_ERROR;
}

static svn_error_t * commit_change_dir_prop(void *dir_baton,
                                            const char *name,
                                            const svn_string_t *value,
                                            apr_pool_t *pool)
{
  resource_baton_t *dir = dir_baton;

  /* record the change. it will be applied at close_dir time. */
  /* ### we should put this into the dir_baton's pool */
  record_prop_change(dir->cc->ras->pool, dir, name, value);

  /* do the CHECKOUT sooner rather than later */
  SVN_ERR( checkout_resource(dir->cc, dir->rsrc, TRUE) );

  /* Add this path to the valid targets hash. */
  add_valid_target (dir->cc, dir->rsrc->local_path, svn_nonrecursive);

  return SVN_NO_ERROR;
}

static svn_error_t * commit_close_dir(void *dir_baton)
{
  resource_baton_t *dir = dir_baton;

  /* Perform all of the property changes on the directory. Note that we
     checked out the directory when the first prop change was noted. */
  SVN_ERR( do_proppatch(dir->cc->ras, dir->rsrc, dir) );

  return SVN_NO_ERROR;
}

static svn_error_t * commit_add_file(const char *path,
                                     void *parent_baton,
                                     const char *copyfrom_path,
                                     svn_revnum_t copyfrom_revision,
                                     apr_pool_t *file_pool,
                                     void **file_baton)
{
  resource_baton_t *parent = parent_baton;
  resource_baton_t *file;
  const char *name = svn_path_basename(path, file_pool);

  /*
  ** To add a new file into the repository, we CHECKOUT the parent
  ** collection, then PUT the file as a member of the resuling working
  ** collection.
  **
  ** If the file was copied from elsewhere, then we will use the COPY
  ** method to copy into the working collection.
  */

  /* Do the parent CHECKOUT first */
  SVN_ERR( checkout_resource(parent->cc, parent->rsrc, TRUE) );

  /* Construct a file_baton that contains all the resource urls. */
  file = apr_pcalloc(file_pool, sizeof(*file));
  file->cc = parent->cc;
  file->created = TRUE;
  SVN_ERR( add_child(&file->rsrc, parent->cc, parent->rsrc,
                     name, 1, SVN_INVALID_REVNUM, file_pool) );

  /* If the parent directory existed before this commit then there may be a
     file with this URL already. We need to ensure such a file does not
     exist, which we do by attempting a PROPFIND.  Of course, a
     PROPFIND *should* succeed if this "add" is actually the second
     half of a "replace".  

     ### For now, we'll assume that if this path has already been
     added to the valid targets hash, that that addition occured
     during the "delete" phase (if that's not the case, this editor is
     being driven incorrectly, as we should never visit the same path
     twice except in a delete+add situation). */
  if ((! parent->created) 
      && (! apr_hash_get(file->cc->valid_targets, path, APR_HASH_KEY_STRING)))
    {
      svn_ra_dav_resource_t *res;
      svn_error_t *err = svn_ra_dav__get_starting_props(&res,
                                                        file->cc->ras->sess,
                                                        file->rsrc->url, NULL,
                                                        file_pool);
      if (!err)
        {
          /* If the PROPFIND succeeds the file already exists */
          return svn_error_createf(SVN_ERR_RA_ALREADY_EXISTS, 0, NULL,
                                   file_pool,
                                   "file '%s' already exists", file->rsrc->url);
        }
      else if (err->apr_err == SVN_ERR_RA_REQUEST_FAILED)
        {
          /* ### TODO: This is what we get if the file doesn't exist
             but an explicit not-found error might be better */
          svn_error_clear_all(err);
        }
      else
        {
          /* A real error */
          return err;
        }
    }

  if (! copyfrom_path)
    {
      /* This a truly new file. */

      /* ### wait for apply_txdelta before doing a PUT. it might arrive a
         ### "long time" from now. certainly after many other operations, so
         ### we don't want to start a PUT just yet.
         ### so... anything else to do here?
      */
    }
  else
    {
      svn_string_t bc_url, bc_relative;
      const char *copy_src;
      int status;

      /* This add has history, so we need to do a COPY. */
      
      /* Convert the copyfrom_* url/rev "public" pair into a Baseline
         Collection (BC) URL that represents the revision -- and a
         relative path under that BC.  */
      SVN_ERR( svn_ra_dav__get_baseline_info(NULL,
                                             &bc_url, &bc_relative, NULL,
                                             parent->cc->ras->sess,
                                             copyfrom_path,
                                             copyfrom_revision,
                                             file_pool));


      /* Combine the BC-URL and relative path; this is the main
         "source" argument to the COPY request.  The "Destination:"
         header given to COPY is simply the wr_url that is already
         part of the file_baton. */
      copy_src = svn_path_join(bc_url.data, bc_relative.data, file_pool);

      /* Have neon do the COPY. */
      status = ne_copy(parent->cc->ras->sess,
                       1,                   /* overwrite */
                       NE_DEPTH_ZERO,       /* for a file, does it care? */
                       copy_src,            /* source URI */
                       file->rsrc->wr_url); /* dest URI */

      if (status != NE_OK)
        {
          const char *msg = apr_psprintf(file_pool, "COPY of %s", path);
          return svn_ra_dav__convert_error(parent->cc->ras->sess,
                                           msg, status, file_pool);
        }
    }

  /* Add this path to the valid targets hash. */
  add_valid_target (parent->cc, path, svn_nonrecursive);

  /* return the file_baton */
  *file_baton = file;
  return SVN_NO_ERROR;
}

static svn_error_t * commit_open_file(const char *path,
                                      void *parent_baton,
                                      svn_revnum_t base_revision,
                                      apr_pool_t *file_pool,
                                      void **file_baton)
{
  resource_baton_t *parent = parent_baton;
  resource_baton_t *file;
  const char *name = svn_path_basename(path, file_pool);

  file = apr_pcalloc(file_pool, sizeof(*file));
  file->cc = parent->cc;
  file->created = FALSE;
  SVN_ERR( add_child(&file->rsrc, parent->cc, parent->rsrc,
                     name, 0, base_revision, file_pool) );

  /* do the CHECKOUT now. we'll PUT the new file contents later on. */
  SVN_ERR( checkout_resource(parent->cc, file->rsrc, TRUE) );

  /* ### wait for apply_txdelta before doing a PUT. it might arrive a
     ### "long time" from now. certainly after many other operations, so
     ### we don't want to start a PUT just yet.
     ### so... anything else to do here? what about the COPY case?
  */

  *file_baton = file;
  return SVN_NO_ERROR;
}

static svn_error_t * commit_stream_write(void *baton,
                                         const char *data, apr_size_t *len)
{
  put_baton_t *pb = baton;
  apr_status_t status;

  /* drop the data into our temp file */
  status = apr_file_write_full(pb->tmpfile, data, *len, NULL);
  if (status)
    return svn_error_create(status, 0, NULL, pb->pool,
                            "Could not write svndiff to temp file.");

  return SVN_NO_ERROR;
}

static svn_error_t * commit_stream_close(void *baton)
{
  put_baton_t *pb = baton;
  commit_ctx_t *cc = pb->file->cc;
  resource_t *rsrc = pb->file->rsrc;
  ne_request *req;
  int fdesc;
  int code;
  apr_status_t status;
  svn_error_t *err;
  apr_off_t offset = 0;
  const char *url_str = svn_path_uri_encode(rsrc->wr_url, pb->pool);

  /* create/prep the request */
  req = ne_request_create(cc->ras->sess, "PUT", url_str);
  if (req == NULL)
    {
      return svn_error_createf(SVN_ERR_RA_CREATING_REQUEST, 0, NULL,
                               pb->pool,
                               "Could not create a PUT request (%s)",
                               url_str);
    }

  /* ### use a symbolic name somewhere for this MIME type? */
  ne_add_request_header(req, "Content-Type", SVN_SVNDIFF_MIME_TYPE);

  /* Rewind the tmpfile. */
  status = apr_file_seek(pb->tmpfile, APR_SET, &offset);
  if (status)
    {
      (void) apr_file_close(pb->tmpfile);
      return svn_error_create(status, 0, NULL, pb->pool,
                              "Couldn't rewind tmpfile.");
    }
  /* Convert the (apr_file_t *)tmpfile into a file descriptor for neon. */
  status = svn_io_fd_from_file(&fdesc, pb->tmpfile);
  if (status)
    {
      (void) apr_file_close(pb->tmpfile);
      return svn_error_create(status, 0, NULL, pb->pool,
                              "Couldn't get file-descriptor of tmpfile.");
    }

  /* Give the filedescriptor to neon. */
  ne_set_request_body_fd(req, fdesc);

  /* run the request and get the resulting status code (and svn_error_t) */
  err = svn_ra_dav__request_dispatch(&code, req, cc->ras->sess,
                                     "PUT", url_str,
                                     201 /* Created */,
                                     204 /* No Content */,
                                     cc->ras->pool);

  /* we're done with the file.  this should delete it. */
  (void) apr_file_close(pb->tmpfile);

  /* toss the pool. all things pb are now history */
  svn_pool_destroy(pb->pool);

  if (err)
    return err;

  return SVN_NO_ERROR;
}

static svn_error_t * 
commit_apply_txdelta(void *file_baton, 
                     svn_txdelta_window_handler_t *handler,
                     void **handler_baton)
{
  resource_baton_t *file = file_baton;
  apr_pool_t *subpool;
  put_baton_t *baton;
  svn_stream_t *stream;

  /* ### should use the file_baton's pool */
  subpool = svn_pool_create(file->cc->ras->pool);

  baton = apr_pcalloc(subpool, sizeof(*baton));
  baton->pool = subpool;
  baton->file = file;

  /* ### oh, hell. Neon's request body support is either text (a C string),
     ### or a FILE*. since we are getting binary data, we must use a FILE*
     ### for now. isn't that special? */

  /* Use the client callback to create a tmpfile. */
  SVN_ERR(file->cc->ras->callbacks->open_tmp_file 
          (&baton->tmpfile, 
           file->cc->ras->callback_baton));

  /* ### register a cleanup on our subpool which closes the file. this
     ### will ensure that the file always gets tossed, even if we exit
     ### with an error. */

  stream = svn_stream_create(baton, subpool);
  svn_stream_set_write(stream, commit_stream_write);
  svn_stream_set_close(stream, commit_stream_close);

  svn_txdelta_to_svndiff(stream, subpool, handler, handler_baton);

  /* Add this path to the valid targets hash. */
  add_valid_target (file->cc, file->rsrc->local_path, svn_nonrecursive);

  return SVN_NO_ERROR;
}

static svn_error_t * commit_change_file_prop(void *file_baton,
                                             const char *name,
                                             const svn_string_t *value,
                                             apr_pool_t *pool)
{
  resource_baton_t *file = file_baton;

  /* record the change. it will be applied at close_file time. */
  /* ### we should put this into the file_baton's pool */
  record_prop_change(file->cc->ras->pool, file, name, value);

  /* do the CHECKOUT sooner rather than later */
  SVN_ERR( checkout_resource(file->cc, file->rsrc, TRUE) );

  /* Add this path to the valid targets hash. */
  add_valid_target (file->cc, file->rsrc->local_path, svn_nonrecursive);

  return SVN_NO_ERROR;
}

static svn_error_t * commit_close_file(void *file_baton)
{
  resource_baton_t *file = file_baton;

  /* Perform all of the property changes on the file. Note that we
     checked out the file when the first prop change was noted. */
  SVN_ERR( do_proppatch(file->cc->ras, file->rsrc, file) );

  return SVN_NO_ERROR;
}


static svn_error_t * commit_close_edit(void *edit_baton)
{
  commit_ctx_t *cc = edit_baton;

  /* ### different pool? */
  SVN_ERR( svn_ra_dav__merge_activity(cc->new_rev,
                                      cc->committed_date,
                                      cc->committed_author,
                                      cc->ras,
                                      cc->ras->root.path,
                                      cc->activity_url,
                                      cc->valid_targets,
                                      cc->ras->pool) );

  SVN_ERR( svn_ra_dav__maybe_store_auth_info(cc->ras) );

  return SVN_NO_ERROR;
}

static svn_error_t * apply_log_message(commit_ctx_t *cc,
                                       const char *log_msg)
{
  apr_pool_t *pool = cc->ras->pool;
  const svn_string_t *vcc;
  const svn_string_t *baseline_url;
  resource_t baseline_rsrc = { SVN_INVALID_REVNUM };
  ne_proppatch_operation po[2] = { { 0 } };
  int rv;
  svn_stringbuf_t *xml_data;

  /* ### this whole sequence can/should be replaced with an expand-property
     ### REPORT when that is available on the server. */

  /* fetch the DAV:version-controlled-configuration from the session's URL */
  SVN_ERR( svn_ra_dav__get_one_prop(&vcc, cc->ras->sess, cc->ras->root.path, 
                                    NULL, &svn_ra_dav__vcc_prop, pool) );

  /* ### we should use DAV:apply-to-version on the CHECKOUT so we can skip
     ### retrieval of the baseline */

  /* Get the Baseline from the DAV:checked-in value */
  SVN_ERR( svn_ra_dav__get_one_prop(&baseline_url, cc->ras->sess, vcc->data, 
                                    NULL, &svn_ra_dav__checked_in_prop, pool));

  baseline_rsrc.vsn_url = baseline_url->data;
  SVN_ERR( checkout_resource(cc, &baseline_rsrc, FALSE) );

  /* XML-Escape the log message. */
  xml_data = NULL;           /* Required by svn_xml_escape_*. */
  svn_xml_escape_nts(&xml_data, log_msg, cc->ras->pool);

  po[0].name = &log_message_prop;
  po[0].type = ne_propset;
  po[0].value = xml_data->data;

  rv = ne_proppatch(cc->ras->sess, baseline_rsrc.wr_url, po);
  if (rv != NE_OK)
    {
      const char *msg = apr_psprintf(cc->ras->pool,
                                     "applying log message to %s",
                                     baseline_rsrc.wr_url);
      return svn_ra_dav__convert_error(cc->ras->sess, msg, rv, cc->ras->pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t * svn_ra_dav__get_commit_editor(
  void *session_baton,
  const svn_delta_editor_t **editor,
  void **edit_baton,
  svn_revnum_t *new_rev,
  const char **committed_date,
  const char **committed_author,
  const char *log_msg)
{
  svn_ra_session_t *ras = session_baton;
  svn_delta_editor_t *commit_editor;
  commit_ctx_t *cc;

  /* Build the main commit editor's baton. */
  cc = apr_pcalloc(ras->pool, sizeof(*cc));
  cc->ras = ras;
  cc->resources = apr_hash_make(ras->pool);
  cc->valid_targets = apr_hash_make(ras->pool);
  cc->get_func = ras->callbacks->get_wc_prop;
  cc->set_func = ras->callbacks->set_wc_prop;
  cc->cb_baton = ras->callback_baton;
  cc->log_msg = log_msg;
  cc->new_rev = new_rev;
  cc->committed_date = committed_date;
  cc->committed_author = committed_author;

  /* ### should we perform an OPTIONS to validate the server we're about
     ### to talk to? */

  /*
  ** Create an Activity. This corresponds directly to an FS transaction.
  ** We will check out all further resources within the context of this
  ** activity.
  */
  SVN_ERR( create_activity(cc) );

  /*
  ** Find the latest baseline resource, check it out, and then apply the
  ** log message onto the thing.
  */
  SVN_ERR( apply_log_message(cc, log_msg) );

  /*
  ** Set up the editor.
  **
  ** This structure is used during the commit process. An external caller
  ** uses these callbacks to describe all the changes in the working copy
  ** that must be committed to the server.
  */
  commit_editor = svn_delta_default_editor(ras->pool);
  commit_editor->open_root = commit_open_root;
  commit_editor->delete_entry = commit_delete_entry;
  commit_editor->add_directory = commit_add_dir;
  commit_editor->open_directory = commit_open_dir;
  commit_editor->change_dir_prop = commit_change_dir_prop;
  commit_editor->close_directory = commit_close_dir;
  commit_editor->add_file = commit_add_file;
  commit_editor->open_file = commit_open_file;
  commit_editor->apply_textdelta = commit_apply_txdelta;
  commit_editor->change_file_prop = commit_change_file_prop;
  commit_editor->close_file = commit_close_file;
  commit_editor->close_edit = commit_close_edit;

  *editor = commit_editor;
  *edit_baton = cc;
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
