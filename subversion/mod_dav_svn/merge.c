/*
 * merge.c: handle the MERGE response processing
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
#include <apr_buckets.h>
#include <apr_xml.h>

#include <httpd.h>
#include <util_filter.h>

#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_repos.h"

#include "dav_svn.h"


/* #################################################################

   These functions are currently *VERY* SVN specific.

   * we don't check prop_elem for what the client requested
   * we presume a baseline was checked out into the activity, and is
     part of the MERGE
   * we presume that all "changed" files/dirs were checked out into
     the activity and are part of the MERGE
     (not sure if this is SVN specific; I can't see how a file/dir
      would be part of the new revision if a working resource had
      not been created for it)
   * we return some props for some resources, and a different set for
     other resources (to keep the wire smaller for now)

   At some point in the future, we'll want to make this "real". Especially
   for proper interoperability.

   #################################################################
*/

typedef struct {
  apr_pool_t *pool;
  ap_filter_t *output;
  apr_bucket_brigade *bb;
  svn_fs_root_t *root;
  const dav_svn_repos *repos;

} merge_response_ctx;

typedef struct mr_baton {
  merge_response_ctx *mrc;

  /* for directories, this is a subpool. otherwise, the pool to use. */
  apr_pool_t *pool;

  /* path for this baton's corresponding FS object */
  const char *path;

  /* for a directory, have we seen a change yet? */
  svn_boolean_t seen_change;

} mr_baton;



/* -------------------------------------------------------------------------

   PRIVATE HELPER FUNCTIONS
*/

static mr_baton *make_child_baton(mr_baton *parent, const char *name,
                                  svn_boolean_t is_dir)
{
  apr_pool_t *pool;
  mr_baton *subdir;

  if (is_dir)
    pool = svn_pool_create(parent->pool);
  else
    pool = parent->pool;

  subdir = apr_pcalloc(pool, sizeof(*subdir));
  subdir->mrc = parent->mrc;
  subdir->pool = pool;

  if (parent->path[1] == '\0')  /* must be "/" */
    subdir->path = apr_pstrcat(pool, "/", name, NULL);
  else
    subdir->path = apr_pstrcat(pool, parent->path, "/", name, NULL);

  return subdir;
}

/* send a response to the client for this baton */
static svn_error_t *send_response(mr_baton *baton, svn_boolean_t is_dir)
{
  merge_response_ctx *mrc = baton->mrc;
  const char *href;
  const char *rt;
  svn_fs_id_t *id;
  svn_stringbuf_t *stable_id;
  const char *vsn_url;
  apr_status_t status;

  href = dav_svn_build_uri(mrc->repos, DAV_SVN_BUILD_URI_PUBLIC,
                           SVN_IGNORED_REVNUM, baton->path,
                           0 /* add_href */, baton->pool);

  rt = is_dir
    ? "<D:resourcetype><D:collection/></D:resourcetype>" DEBUG_CR
    : "<D:resourcetype/>" DEBUG_CR;

  SVN_ERR( svn_fs_node_id(&id, mrc->root, baton->path, baton->pool) );

  stable_id = svn_fs_unparse_id(id, baton->pool);
  svn_stringbuf_appendcstr(stable_id, baton->path);

  vsn_url = dav_svn_build_uri(mrc->repos, DAV_SVN_BUILD_URI_VERSION,
                              SVN_INVALID_REVNUM, stable_id->data,
                              0 /* add_href */, baton->pool);

  status = ap_fputstrs(mrc->output, mrc->bb,
                       "<D:response>" DEBUG_CR
                       "<D:href>", 
                       apr_xml_quote_string (baton->pool, href, 1),
                       "</D:href>" DEBUG_CR
                       "<D:propstat><D:prop>" DEBUG_CR,
                       rt,
                       "<D:checked-in><D:href>",
                       apr_xml_quote_string (baton->pool, vsn_url, 1),
                       "</D:href></D:checked-in>" DEBUG_CR
                       "</D:prop>" DEBUG_CR
                       "<D:status>HTTP/1.1 200 OK</D:status>" DEBUG_CR
                       "</D:propstat>" DEBUG_CR
                       "</D:response>" DEBUG_CR,
                       NULL);

  if (status != APR_SUCCESS)
    return svn_error_create(status, 0, NULL, baton->pool,
                            "could not write response to output");

  return APR_SUCCESS;
}


/* -------------------------------------------------------------------------

   EDITOR FUNCTIONS
*/

static svn_error_t *mr_open_root(void *edit_baton,
                                 svn_revnum_t base_revision,
                                 void **root_baton)
{
  merge_response_ctx *mrc = edit_baton;
  apr_pool_t *pool;
  mr_baton *b;

  /* note that we create a subpool; the root_baton is passed to the
     close_directory callback, where we will destroy the pool. */
  pool = svn_pool_create(mrc->pool);
  b = apr_pcalloc(pool, sizeof(*b));
  b->mrc = mrc;
  b->pool = pool;
  b->path = "/";

  *root_baton = b;
  return NULL;
}

static svn_error_t *mr_delete_entry(svn_stringbuf_t *name,
                                    svn_revnum_t revision,
                                    void *parent_baton)
{
  mr_baton *parent = parent_baton;

  /* Removing an item is an explicit change to the parent. Mark it so the
     client will get the data on the new parent. */
  parent->seen_change = TRUE;

  return NULL;
}

static svn_error_t *mr_add_directory(svn_stringbuf_t *name,
                                     void *parent_baton,
                                     svn_stringbuf_t *copyfrom_path,
                                     svn_revnum_t copyfrom_revision,
                                     void **child_baton)
{
  mr_baton *parent = parent_baton;
  mr_baton *subdir = make_child_baton(parent, name->data, TRUE);

  /* pretend that we've already seen a change for this dir (so that a prop
     change won't generate a second response) */
  subdir->seen_change = TRUE;

  /* the response for this directory will occur at close_directory time */

  /* Adding a subdir is an explicit change to the parent. Mark it so the
     client will get the data on the new parent. */
  parent->seen_change = TRUE;

  *child_baton = subdir;
  return NULL;
}

static svn_error_t *mr_open_directory(svn_stringbuf_t *name,
                                      void *parent_baton,
                                      svn_revnum_t base_revision,
                                      void **child_baton)
{
  mr_baton *parent = parent_baton;
  mr_baton *subdir = make_child_baton(parent, name->data, TRUE);

  /* Don't issue a response until we see a prop change, or a file/subdir
     is added/removed inside this directory. */

  *child_baton = subdir;
  return NULL;
}

static svn_error_t *mr_change_dir_prop(void *dir_baton,
                                       svn_stringbuf_t *name,
                                       svn_stringbuf_t *value)
{
  mr_baton *dir = dir_baton;

  /* okay, this qualifies as a change, and we need to tell the client
     (which happens at close_directory time). */
  dir->seen_change = TRUE;

  return NULL;
}

static svn_error_t *mr_close_directory(void *dir_baton)
{
  mr_baton *dir = dir_baton;

  /* if we ever saw a change for this directory, then issue a response
     for it. */
  if (dir->seen_change)
    {
      SVN_ERR( send_response(dir, TRUE /* is_dir */) );
    }

  svn_pool_destroy(dir->pool);

  return NULL;
}

static svn_error_t *mr_add_file(svn_stringbuf_t *name,
                                void *parent_baton,
                                svn_stringbuf_t *copy_path,
                                svn_revnum_t copy_revision,
                                void **file_baton)
{
  mr_baton *parent = parent_baton;
  mr_baton *file = make_child_baton(parent, name->data, FALSE);

  /* We wait until close_file to issue a response for this. */

  /* Adding a file is an explicit change to the parent. Mark it so the
     client will get the data on the new parent. */
  parent->seen_change = TRUE;

  *file_baton = file;
  return NULL;
}

static svn_error_t *mr_open_file(svn_stringbuf_t *name,
                                 void *parent_baton,
                                 svn_revnum_t base_revision,
                                 void **file_baton)
{
  mr_baton *parent = parent_baton;
  mr_baton *file = make_child_baton(parent, name->data, FALSE);

  /* We wait until close_file to issue a response for this. */

  *file_baton = file;
  return NULL;
}

static svn_error_t *mr_close_file(void *file_baton)
{
  /* nothing to do except for sending the response. */
  return send_response(file_baton, FALSE /* is_dir */);
}


/* -------------------------------------------------------------------------

   PUBLIC FUNCTIONS
*/

dav_error * dav_svn__merge_response(ap_filter_t *output,
                                    const dav_svn_repos *repos,
                                    svn_revnum_t new_rev,
                                    apr_xml_elem *prop_elem,
                                    apr_pool_t *pool)
{
  apr_bucket_brigade *bb;
  svn_fs_root_t *committed_root;
  svn_fs_root_t *previous_root;
  svn_error_t *serr;
  const char *vcc;
  char revbuf[20];      /* long enough for %ld */
  svn_string_t *creationdate, *creator_displayname;
  apr_hash_t *revs;
  svn_revnum_t *rev_ptr;
  svn_delta_edit_fns_t *editor;
  merge_response_ctx mrc = { 0 };

  serr = svn_fs_revision_root(&committed_root, repos->fs, new_rev, pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not open the FS root for the "
                                 "revision just committed.");
    }
  serr = svn_fs_revision_root(&previous_root, repos->fs, new_rev - 1, pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not open the FS root for the "
                                 "previous revision.");
    }

  bb = apr_brigade_create(pool, output->c->bucket_alloc);

  /* prep some strings */
  
  /* the HREF for the baseline is actually the VCC */
  vcc = dav_svn_build_uri(repos, DAV_SVN_BUILD_URI_VCC, SVN_IGNORED_REVNUM,
                          NULL, 0 /* add_href */, pool);

  /* the version-name of the baseline is the revision number */
  sprintf(revbuf, "%ld", new_rev);

  /* get the creationdate and creator-displayname of the new revision, too. */
  serr = svn_fs_revision_prop(&creationdate, repos->fs, new_rev,
                              SVN_PROP_REVISION_DATE, pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not get date of newest revision"); 
    }
  serr = svn_fs_revision_prop(&creator_displayname, repos->fs, new_rev,
                              SVN_PROP_REVISION_AUTHOR, pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not get author of newest revision"); 
    }


  (void) ap_fputstrs(output, bb,
                     DAV_XML_HEADER DEBUG_CR
                     "<D:merge-response xmlns:D=\"DAV:\">" DEBUG_CR
                     "<D:updated-set>" DEBUG_CR

                     /* generate a response for the new baseline */
                     "<D:response>" DEBUG_CR
                     "<D:href>", 
                     apr_xml_quote_string (pool, vcc, 1),
                     "</D:href>" DEBUG_CR
                     "<D:propstat><D:prop>" DEBUG_CR
                     /* ### this is wrong. it's a VCC, not a baseline. but
                        ### we need to tell the client to look at *this*
                        ### resource for the version-name. */
                     "<D:resourcetype><D:baseline/></D:resourcetype>" DEBUG_CR
                     "<D:version-name>", revbuf, "</D:version-name>" DEBUG_CR
                     "<D:creationdate>", creationdate->data, 
                                     "</D:creationdate>" DEBUG_CR
                     "<D:creator-displayname>", creator_displayname->data,
                                     "</D:creator-displayname>" DEBUG_CR
                     "</D:prop>" DEBUG_CR
                     "<D:status>HTTP/1.1 200 OK</D:status>" DEBUG_CR
                     "</D:propstat>" DEBUG_CR
                     "</D:response>" DEBUG_CR,

                     NULL);

  /* Now we need to generate responses for all the resources which changed.
     This is done through a delta of the two roots.

     Note that a directory is not marked when open_dir is seen (since it
     typically is used just for changing members in that directory); instead,
     we want for a property change (the only reason the client would need to
     fetch a new directory).

     ### we probably should say something about the dirs, so that we can
     ### pass back the new version URL */

  /* ### hrm. needing this hash table feels wonky. */
  revs = apr_hash_make(pool);
  rev_ptr = apr_palloc(pool, sizeof(*rev_ptr));
  *rev_ptr = new_rev - 1;
  apr_hash_set(revs, "", APR_HASH_KEY_STRING, rev_ptr);

  /* set up the editor for the delta process */
  editor = svn_delta_old_default_editor(pool);
  editor->open_root = mr_open_root;
  editor->delete_entry = mr_delete_entry;
  editor->add_directory = mr_add_directory;
  editor->open_directory = mr_open_directory;
  editor->change_dir_prop = mr_change_dir_prop;
  editor->close_directory = mr_close_directory;
  editor->add_file = mr_add_file;
  editor->open_file = mr_open_file;
  editor->close_file = mr_close_file;

  /* set up the merge response context */
  mrc.pool = pool;
  mrc.output = output;
  mrc.bb = bb;
  mrc.root = committed_root;
  mrc.repos = repos;

  serr = svn_repos_dir_delta(previous_root, "/",
                             NULL,      /* ### should fix */
                             revs,
                             committed_root, "/",
                             editor, &mrc, 
                             FALSE, /* don't bother with text-deltas */
                             TRUE, /* Do recurse into subdirectories */
                             FALSE, /* Do not allow entry props */
                             FALSE, /* Do not allow copyfrom args */
                             pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not process the merge delta.");
    }

  /* wrap up the merge response */
  (void) ap_fputs(output, bb,
                  "</D:updated-set>" DEBUG_CR
                  "</D:merge-response>" DEBUG_CR);

  /* send whatever is left in the brigade */
  (void) ap_pass_brigade(output, bb);

  return NULL;
}


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
