/*
 * commit.c :  routines for committing changes to the server
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



#include <http_request.h>

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_uuid.h>

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_wc.h"
#include "svn_path.h"

#include "ra_dav.h"


/*
** resource_t: identify the relevant pieces of a resource on the server
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
  const char *url;
  const char *vsn_url;
  const char *wr_url;

  svn_string_t *local_path;
} resource_t;

typedef struct
{
  svn_ra_session_t *ras;
  const char *activity_url;

  /* ### resources may not be needed */
  apr_hash_t *resources;        /* URL (const char *) -> RESOURCE_T */

  /* name of local prop to hold the version resource's URL */
  svn_string_t *vsn_url_name;

  svn_ra_get_wc_prop_func_t get_func;
  svn_ra_set_wc_prop_func_t set_func;
  svn_ra_close_commit_func_t close_func;
  void *close_baton;

  svn_string_t *log_msg;

} commit_ctx_t;

typedef struct
{
  commit_ctx_t *cc;
  resource_t *rsrc;
  apr_hash_t *prop_changes;
} resource_baton_t;

/*
** singleton_delete_prop:
**
** The address of this integer is used as a "singleton" value to mark
** properties which must be deleted. Properties which are changed/added
** will use their new values.
*/
static const int singleton_delete_prop;
#define DELETE_THIS_PROP (&singleton_delete_prop)


static svn_error_t * simple_request(svn_ra_session_t *ras, const char *method,
                                    const char *url, int *code)
{
  http_req *req;
  int rv;

  /* create/prep the request */
  req = http_request_create(ras->sess, method, url);
  if (req == NULL)
    {
      return svn_error_createf(SVN_ERR_RA_CREATING_REQUEST, 0, NULL,
                               ras->pool,
                               "Could not create a request (%s %s)",
                               method, url);
    }

  /* run the request and get the resulting status code. */
  rv = http_request_dispatch(req);
  if (rv != HTTP_OK)
    {
      /* ### need to be more sophisticated with reporting the failure */
      return svn_error_createf(SVN_ERR_RA_REQUEST_FAILED, 0, NULL,
                               ras->pool,
                               "The server request failed (#%d) (%s %s)",
                               rv, method, url);
    }

  *code = http_get_status(req)->code;

  http_request_destroy(req);

  return NULL;
}

static svn_error_t * create_activity(commit_ctx_t *cc)
{
  svn_string_t * activity_url;
  apr_uuid_t uuid;
  char uuid_buf[APR_UUID_FORMATTED_LENGTH];
  svn_string_t uuid_str = { uuid_buf, sizeof(uuid_buf), 0, NULL };
  int code;

  {
    /* ### damn, this is annoying to have to create strings */
    svn_string_t * propname = svn_string_create(SVN_RA_DAV__LP_ACTIVITY_URL,
                                                cc->ras->pool);
    svn_string_t * path = svn_string_create(".", cc->ras->pool);

    /* get the URL where we should create activities */
    SVN_ERR( (*cc->get_func)(cc->close_baton, path, propname, &activity_url) );
  }

  /* the URL for our activity will be ACTIVITY_URL/UUID */
  apr_uuid_get(&uuid);
  apr_uuid_format(uuid_buf, &uuid);

  svn_path_add_component(activity_url, &uuid_str, svn_path_url_style);

  cc->activity_url = activity_url->data;

  /* do a MKACTIVITY request and get the resulting status code. */
  SVN_ERR( simple_request(cc->ras, "MKACTIVITY", cc->activity_url, &code) );
  if (code != 201)
    {
      /* ### need to be more sophisticated with reporting the failure */
      return svn_error_create(SVN_ERR_RA_MKACTIVITY_FAILED, 0, NULL,
                              cc->ras->pool,
                              "The MKACTIVITY request failed.");
    }

  return NULL;
}

static svn_error_t * add_child(resource_t **child,
                               commit_ctx_t *cc,
                               const resource_t *parent,
                               const char *name,
                               int created)
{
  apr_pool_t *pool = cc->ras->pool;
  resource_t *rsrc;

  rsrc = apr_pcalloc(pool, sizeof(*rsrc));
  rsrc->url = apr_pstrcat(pool, parent->url, "/", name, NULL);

  rsrc->local_path = svn_string_dup(parent->local_path, pool);
  svn_path_add_component_nts(rsrc->local_path, name, svn_path_local_style);

  /* If the resource was just created, then its WR appears under the
     parent and it has no VR URL. If the resource already existed, then
     it has no WR and its VR can be found in the WC properties. */
  if (created)
    {
      /* ### does the workcol have a trailing slash? do some extra work */
      rsrc->wr_url = apr_pstrcat(pool, parent->wr_url, "/", name, NULL);
    }
  else
    {
      svn_string_t *vsn_url_value;

      SVN_ERR( (*cc->get_func)(cc->close_baton,
                               rsrc->local_path,
                               cc->vsn_url_name,
                               &vsn_url_value) );
      rsrc->vsn_url = vsn_url_value->data;
    }

  apr_hash_set(cc->resources, rsrc->url, APR_HASH_KEY_STRING, rsrc);

  *child = rsrc;
  return NULL;
}

/* check out the specified resource (if it hasn't been checked out yet) */
static svn_error_t * checkout_resource(commit_ctx_t *cc, resource_t *res)
{
  http_req *req;
  int rv;
  int code;
  const char *body;
  const char *locn = NULL;

  printf("[checkout_resource] CHECKOUT: %s\n", res->url);

  if (res->wr_url != NULL)
    {
      /* already checked out! */
      return NULL;
    }

  /* assert: res->vsn_url != NULL */

  /* ### send a CHECKOUT resource on res->vsn_url; include cc->activity_url;
     ### place result into res->wr_url and return it */

  /* create/prep the request */
  req = http_request_create(cc->ras->sess, "CHECKOUT", res->vsn_url);
  if (req == NULL)
    {
      return svn_error_createf(SVN_ERR_RA_CREATING_REQUEST, 0, NULL,
                               cc->ras->pool,
                               "Could not create a CHECKOUT request (%s)",
                               res->vsn_url);
    }

  /* ### store this into cc to avoid pool growth */
  body = apr_psprintf(cc->ras->pool,
                      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                      "<D:checkout xmlns:D=\"DAV:\">"
                      "<D:activity-set>"
                      "<D:href>%s</D:href>"
                      "</D:activity-set></D:checkout>", cc->activity_url);
  http_set_request_body_buffer(req, body);

  http_add_response_header_handler(req, "location",
                                   http_duplicate_header, &locn);

  /* run the request and get the resulting status code. */
  rv = http_request_dispatch(req);
  if (rv != HTTP_OK)
    {
      /* ### need to be more sophisticated with reporting the failure */
      return svn_error_createf(SVN_ERR_RA_REQUEST_FAILED, 0, NULL,
                               cc->ras->pool,
                               "The CHECKOUT request failed (#%d) (%s)",
                               rv, res->vsn_url);
    }

  code = http_get_status(req)->code;

  http_request_destroy(req);

  if (code != 201)
    {
      /* ### error */
    }
  if (locn == NULL)
    {
      /* ### error */
    }

  res->wr_url = locn;

  return NULL;
}

static void record_prop_change(apr_pool_t *pool,
                               apr_hash_t **prop_changes,
                               const svn_string_t *name,
                               const svn_string_t *value)
{
  if (*prop_changes == NULL)
    *prop_changes = apr_hash_make(pool);

  /* ### need to copy name/value into POOL */

  /* ### put the name/value into dir->prop_changes hash */
  if (value == NULL)
    {
      /* ### put name/DELETE_THIS_PROP into the hash */
    }
  else
    {
      /* ### put the name/value into the hash */
    }
}

static svn_error_t * do_proppatch(svn_ra_session_t *ras,
                                  const resource_t *rsrc,
                                  apr_hash_t *changes)
{
  /* ### the hash contains the FINAL state, so the ordering of the items
     ### in the PROPPATCH is no big deal.
     ###
     ### iterate twice: once for all the SET items, once for the DELETE */

  /* ### we should have res->wr_url */
  /* ### maybe pass wr_url rather than resource_t* */

  printf("[do_proppatch] PROPPATCH: %s\n", rsrc->url);

  return NULL;
}

static svn_error_t * commit_replace_root(void *edit_baton,
                                         svn_revnum_t base_revision, 
                                         void **root_baton)
{
  commit_ctx_t *cc = edit_baton;
  resource_baton_t *root;
  resource_t *rsrc;
  svn_string_t *vsn_url_value;

  /* create the root resource. no wr_url (yet) */
  rsrc = apr_pcalloc(cc->ras->pool, sizeof(*rsrc));
  rsrc->url = cc->ras->root.path;

  /* ### or use empty string? */
  rsrc->local_path = svn_string_create(".", cc->ras->pool);

  SVN_ERR( (*cc->get_func)(cc->close_baton,
                           rsrc->local_path,
                           cc->vsn_url_name,
                           &vsn_url_value) );
  rsrc->vsn_url = vsn_url_value->data;

  apr_hash_set(cc->resources, rsrc->url, APR_HASH_KEY_STRING, rsrc);

  root = apr_pcalloc(cc->ras->pool, sizeof(*root));
  root->cc = cc;
  root->rsrc = rsrc;

  *root_baton = root;

  return NULL;
}

static svn_error_t * commit_delete_entry(svn_string_t *name,
                                         void *parent_baton)
{
  resource_baton_t *parent = parent_baton;
  apr_pool_t *pool = parent->cc->ras->pool;
  const char *child;
  int code;

  /* get the URL to the working collection */
  printf("[delete_entry] ");
  SVN_ERR( checkout_resource(parent->cc, parent->rsrc) );

  /* create the URL for the child resource */
  /* ### does the workcol have a trailing slash? do some extra work */
  child = apr_pstrcat(pool, parent->rsrc->wr_url, "/", name->data, NULL);

  /* Note: the child cannot have a resource stored in the resources table
     because of the editor traversal rules. That is: this is the first time
     we have seen anything about the child, and we're deleting it. As a
     corollary, we know the child hasn't been checked out. */

  /* delete the child resource */
  SVN_ERR( simple_request(parent->cc->ras, "DELETE", child, &code) );
  if (code != 200)
    {
      /* ### need to be more sophisticated with reporting the failure */
      return svn_error_createf(SVN_ERR_RA_DELETE_FAILED, 0, NULL, pool,
                               "Could not DELETE %s", child);
    }

  printf("[delete] DELETE: %s\n", child);

  return NULL;
}

static svn_error_t * commit_add_dir(svn_string_t *name,
                                    void *parent_baton,
                                    svn_string_t *copyfrom_path,
                                    svn_revnum_t copyfrom_revision,
                                    void **child_baton)
{
  resource_baton_t *parent = parent_baton;
  apr_pool_t *pool = parent->cc->ras->pool;
  resource_baton_t *child;
  int code;

  /* ### no support for ancestor right now. that will use COPY */

  /* check out the parent resource so that we can create the new collection
     as one of its children. */
  printf("[add_dir] ");
  SVN_ERR( checkout_resource(parent->cc, parent->rsrc) );

  child = apr_pcalloc(pool, sizeof(*child));
  child->cc = parent->cc;
  SVN_ERR( add_child(&child->rsrc, parent->cc, parent->rsrc, name->data, 1) );

  /* create the new collection */
  SVN_ERR( simple_request(parent->cc->ras, "MKCOL", child->rsrc->wr_url,
                          &code) );
  if (code != 201)
    {
      /* ### need to be more sophisticated with reporting the failure */
      /* ### need error */
    }

  printf("[add_dir] MKCOL: %s\n", child->rsrc->url);

  *child_baton = child;
  return NULL;
}

static svn_error_t * commit_rep_dir(svn_string_t *name,
                                    void *parent_baton,
                                    svn_revnum_t base_revision,
                                    void **child_baton)
{
  resource_baton_t *parent = parent_baton;
  apr_pool_t *pool = parent->cc->ras->pool;
  resource_baton_t *child = apr_pcalloc(pool, sizeof(*child));

  child->cc = parent->cc;
  SVN_ERR( add_child(&child->rsrc, parent->cc, parent->rsrc, name->data, 0) );

  /*
  ** Note: replace_dir simply means that a change has occurred somewhere
  **       within this directory. We have nothing to do, to prepare for
  **       those changes (each will be considered independently).
  **
  ** Note: if a directory is replaced by something else, then this callback
  **       will not be used: a true replacement is modeled with a "delete"
  **       followed by an "add".
  */

  *child_baton = child;
  return NULL;
}

static svn_error_t * commit_change_dir_prop(void *dir_baton,
                                            svn_string_t *name,
                                            svn_string_t *value)
{
  resource_baton_t *dir = dir_baton;

  /* record the change. it will be applied at close_dir time. */
  record_prop_change(dir->cc->ras->pool, &dir->prop_changes, name, value);

  /* do the CHECKOUT sooner rather than later */
  printf("[change_dir_prop] ");
  SVN_ERR( checkout_resource(dir->cc, dir->rsrc) );

  printf("[change_dir_prop] %s: %s=%s\n",
         dir->rsrc->url, name->data, value->data);

  return NULL;
}

static svn_error_t * commit_close_dir(void *dir_baton)
{
  resource_baton_t *dir = dir_baton;

  /* Perform all of the property changes on the directory. Note that we
     checked out the directory when the first prop change was noted. */
  printf("[close_dir] ");
  SVN_ERR( do_proppatch(dir->cc->ras, dir->rsrc, dir->prop_changes) );

  return NULL;
}

static svn_error_t * commit_add_file(svn_string_t *name,
                                     void *parent_baton,
                                     svn_string_t *copyfrom_path,
                                     svn_revnum_t copyfrom_revision,
                                     void **file_baton)
{
  resource_baton_t *parent = parent_baton;
  apr_pool_t *pool = parent->cc->ras->pool;
  resource_baton_t *file;

  /* ### store name, parent? */

  /*
  ** To add a new file into the repository, we CHECKOUT the parent
  ** collection, then PUT the file as a member of the resuling working
  ** collection.
  **
  ** If the file was copied from elsewhere, then we will use the COPY
  ** method to copy from [### where?] into the working collection.
  */

  /* ### ancestor_path is ignored for now */

  /* do the CHECKOUT now. we'll PUT the new file later on. */
  printf("[add_file] ");
  SVN_ERR( checkout_resource(parent->cc, parent->rsrc) );

  file = apr_pcalloc(pool, sizeof(*file));
  file->cc = parent->cc;
  SVN_ERR( add_child(&file->rsrc, parent->cc, parent->rsrc, name->data, 1) );

  /* ### wait for apply_txdelta before doing a PUT. it might arrive a
     ### "long time" from now. certainly after many other operations, so
     ### we don't want to start a PUT just yet.
     ### so... anything else to do here?
  */

  *file_baton = file;
  return NULL;
}

static svn_error_t * commit_rep_file(svn_string_t *name,
                                     void *parent_baton,
                                     svn_revnum_t base_revision,
                                     void **file_baton)
{
  resource_baton_t *parent = parent_baton;
  apr_pool_t *pool = parent->cc->ras->pool;
  resource_baton_t *file;

  file = apr_pcalloc(pool, sizeof(*file));
  file->cc = parent->cc;
  SVN_ERR( add_child(&file->rsrc, parent->cc, parent->rsrc, name->data, 0) );

  /* do the CHECKOUT now. we'll PUT the new file contents later on. */
  printf("[rep_file] ");
  SVN_ERR( checkout_resource(parent->cc, file->rsrc) );

  /* ### wait for apply_txdelta before doing a PUT. it might arrive a
     ### "long time" from now. certainly after many other operations, so
     ### we don't want to start a PUT just yet.
     ### so... anything else to do here? what about the COPY case?
  */

  *file_baton = file;
  return NULL;
}

static svn_error_t * commit_send_txdelta(svn_txdelta_window_t *window,
                                         void *baton)
{
  return NULL;
}

static svn_error_t * commit_apply_txdelta(void *file_baton, 
                                          svn_txdelta_window_handler_t *handler,
                                          void **handler_baton)
{
  resource_baton_t *file = file_baton;

  /* ### begin a PUT here? */

  printf("[apply_txdelta] PUT: %s\n", file->rsrc->url);

  *handler = commit_send_txdelta;
  *handler_baton = NULL;

  return NULL;
}

static svn_error_t * commit_change_file_prop(
  void *file_baton,
  svn_string_t *name,
  svn_string_t *value)
{
  resource_baton_t *file = file_baton;

  /* record the change. it will be applied at close_file time. */
  record_prop_change(file->cc->ras->pool, &file->prop_changes, name, value);

  /* do the CHECKOUT sooner rather than later */
  printf("[change_file_prop] ");
  SVN_ERR( checkout_resource(file->cc, file->rsrc) );

  printf("[change_file_prop] %s: %s=%s\n",
         file->rsrc->url, name->data, value->data);

  return NULL;
}

static svn_error_t * commit_close_file(void *file_baton)
{
  resource_baton_t *file = file_baton;

  /* Perform all of the property changes on the file. Note that we
     checked out the file when the first prop change was noted. */
  printf("[close_file] ");
  SVN_ERR( do_proppatch(file->cc->ras, file->rsrc, file->prop_changes) );

  return NULL;
}


static svn_error_t * commit_close_edit(void *edit_baton)
{
  commit_ctx_t *cc = edit_baton;

  /* ### MERGE the activity */
  printf("[close_edit] MERGE: %s\n",
         cc->activity_url ? cc->activity_url : "(activity)");

  /* ### set new_revision according to response from server */
  /* ### get the new version URLs for all affected resources */

  /* ### invoke close_func, set_func */

  return NULL;
}
 

svn_error_t * svn_ra_dav__get_commit_editor(
  void *session_baton,
  const svn_delta_edit_fns_t **editor,
  void **edit_baton,
  svn_string_t *log_msg,
  svn_ra_get_wc_prop_func_t get_func,
  svn_ra_set_wc_prop_func_t set_func,
  svn_ra_close_commit_func_t close_func,
  void *close_baton)
{
  svn_ra_session_t *ras = session_baton;
  commit_ctx_t *cc;
  svn_delta_edit_fns_t *commit_editor;

  cc = apr_pcalloc(ras->pool, sizeof(*cc));
  cc->ras = ras;
  cc->resources = apr_hash_make(ras->pool);
  cc->vsn_url_name = svn_string_create(SVN_RA_DAV__LP_VSN_URL, ras->pool);
  cc->get_func = get_func;
  cc->set_func = set_func;
  cc->close_func = close_func;
  cc->close_baton = close_baton;
  cc->log_msg = log_msg;

  SVN_ERR( create_activity(cc) );

  /* ### check out the baseline and attach the log_msg */

  /*
  ** Set up the editor.
  **
  ** This structure is used during the commit process. An external caller
  ** uses these callbacks to describe all the changes in the working copy
  ** that must be committed to the server.
  */
  commit_editor = svn_delta_default_editor(ras->pool);
  commit_editor->replace_root = commit_replace_root;
  commit_editor->delete_entry = commit_delete_entry;
  commit_editor->add_directory = commit_add_dir;
  commit_editor->replace_directory = commit_rep_dir;
  commit_editor->change_dir_prop = commit_change_dir_prop;
  commit_editor->close_directory = commit_close_dir;
  commit_editor->add_file = commit_add_file;
  commit_editor->replace_file = commit_rep_file;
  commit_editor->apply_textdelta = commit_apply_txdelta;
  commit_editor->change_file_prop = commit_change_file_prop;
  commit_editor->close_file = commit_close_file;
  commit_editor->close_edit = commit_close_edit;

  *edit_baton = cc;
  *editor = commit_editor;

  return NULL;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
