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
  apr_pool_t *pool;
  merge_response_ctx *mrc;
  const char *path; /* path for this baton's corresponding FS object */
  svn_boolean_t seen_change; /* for a directory, have we seen a change yet? */

} mr_baton;



/* -------------------------------------------------------------------------

   PRIVATE HELPER FUNCTIONS
*/

static mr_baton *make_child_baton(mr_baton *parent, 
                                  const char *path,
                                  apr_pool_t *pool)
{
  mr_baton *subdir = apr_pcalloc(pool, sizeof(*subdir));
  subdir->mrc = parent->mrc;
  if (path[0] == '/')
    subdir->path = path;
  else
    subdir->path = apr_pstrcat(pool, "/", path, NULL);
  subdir->pool = pool;

  return subdir;
}

/* send a response to the client for this baton */
static svn_error_t *send_response(mr_baton *baton, 
                                  svn_boolean_t is_dir,
                                  apr_pool_t *pool)
{
  merge_response_ctx *mrc = baton->mrc;
  const char *href;
  const char *rt;
  const char *vsn_url;
  apr_status_t status;
  svn_revnum_t rev_to_use;


  href = dav_svn_build_uri(mrc->repos, DAV_SVN_BUILD_URI_PUBLIC,
                           SVN_IGNORED_REVNUM, baton->path,
                           0 /* add_href */, pool);

  rt = is_dir
    ? "<D:resourcetype><D:collection/></D:resourcetype>" DEBUG_CR
    : "<D:resourcetype/>" DEBUG_CR;

  rev_to_use = dav_svn_get_safe_cr(mrc->root, baton->path, pool);
  vsn_url = dav_svn_build_uri(mrc->repos, DAV_SVN_BUILD_URI_VERSION,
                              rev_to_use, baton->path,
                              0 /* add_href */, pool);

  status = ap_fputstrs(mrc->output, mrc->bb,
                       "<D:response>" DEBUG_CR
                       "<D:href>", 
                       apr_xml_quote_string (pool, href, 1),
                       "</D:href>" DEBUG_CR
                       "<D:propstat><D:prop>" DEBUG_CR,
                       rt,
                       "<D:checked-in><D:href>",
                       apr_xml_quote_string (pool, vsn_url, 1),
                       "</D:href></D:checked-in>" DEBUG_CR
                       "</D:prop>" DEBUG_CR
                       "<D:status>HTTP/1.1 200 OK</D:status>" DEBUG_CR
                       "</D:propstat>" DEBUG_CR
                       "</D:response>" DEBUG_CR,
                       NULL);

  if (status != APR_SUCCESS)
    return svn_error_create(status, 0, NULL, pool,
                            "could not write response to output");

  return APR_SUCCESS;
}


/* -------------------------------------------------------------------------

   EDITOR FUNCTIONS
*/

static svn_error_t *mr_open_root(void *edit_baton,
                                 svn_revnum_t base_revision,
                                 apr_pool_t *pool,
                                 void **root_baton)
{
  merge_response_ctx *mrc = edit_baton;
  mr_baton *b;

  b = apr_pcalloc(pool, sizeof(*b));
  b->mrc = mrc;
  b->path = "/";
  b->pool = pool;

  *root_baton = b;
  return SVN_NO_ERROR;
}

static svn_error_t *mr_delete_entry(const char *path,
                                    svn_revnum_t revision,
                                    void *parent_baton,
                                    apr_pool_t *pool)
{
  mr_baton *parent = parent_baton;

  /* Removing an item is an explicit change to the parent. Mark it so the
     client will get the data on the new parent. */
  parent->seen_change = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *mr_add_directory(const char *path,
                                     void *parent_baton,
                                     const char *copyfrom_path,
                                     svn_revnum_t copyfrom_revision,
                                     apr_pool_t *pool,
                                     void **child_baton)
{
  mr_baton *parent = parent_baton;
  mr_baton *subdir = make_child_baton(parent, path, pool);

  /* pretend that we've already seen a change for this dir (so that a prop
     change won't generate a second response) */
  subdir->seen_change = TRUE;

  /* the response for this directory will occur at close_directory time */

  /* Adding a subdir is an explicit change to the parent. Mark it so the
     client will get the data on the new parent. */
  parent->seen_change = TRUE;

  *child_baton = subdir;
  return SVN_NO_ERROR;
}

static svn_error_t *mr_open_directory(const char *path,
                                      void *parent_baton,
                                      svn_revnum_t base_revision,
                                      apr_pool_t *pool,
                                      void **child_baton)
{
  mr_baton *parent = parent_baton;
  mr_baton *subdir = make_child_baton(parent, path, pool);

  /* Don't issue a response until we see a prop change, or a file/subdir
     is added/removed inside this directory. */

  *child_baton = subdir;
  return SVN_NO_ERROR;
}

static svn_error_t *mr_change_dir_prop(void *dir_baton,
                                       const char *name,
                                       const svn_string_t *value,
                                       apr_pool_t *pool)
{
  mr_baton *dir = dir_baton;

  /* okay, this qualifies as a change, and we need to tell the client
     (which happens at close_directory time). */
  dir->seen_change = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *mr_close_directory(void *dir_baton)
{
  mr_baton *dir = dir_baton;

  /* if we ever saw a change for this directory, then issue a response
     for it. */
  if (dir->seen_change)
    {
      SVN_ERR( send_response(dir, TRUE /* is_dir */, dir->pool) );
    }

  return SVN_NO_ERROR;
}

static svn_error_t *mr_add_file(const char *path,
                                void *parent_baton,
                                const char *copy_path,
                                svn_revnum_t copy_revision,
                                apr_pool_t *pool,
                                void **file_baton)
{
  mr_baton *parent = parent_baton;
  mr_baton *file = make_child_baton(parent, path, pool);

  /* We wait until close_file to issue a response for this. */

  /* Adding a file is an explicit change to the parent. Mark it so the
     client will get the data on the new parent. */
  parent->seen_change = TRUE;

  *file_baton = file;
  return SVN_NO_ERROR;
}

static svn_error_t *mr_open_file(const char *path,
                                 void *parent_baton,
                                 svn_revnum_t base_revision,
                                 apr_pool_t *pool,
                                 void **file_baton)
{
  mr_baton *parent = parent_baton;
  mr_baton *file = make_child_baton(parent, path, pool);

  /* We wait until close_file to issue a response for this. */

  *file_baton = file;
  return SVN_NO_ERROR;
}

static svn_error_t *mr_close_file(void *file_baton)
{
  mr_baton *fb = file_baton;

  /* nothing to do except for sending the response. */
  return send_response(file_baton, FALSE /* is_dir */, fb->pool);
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
  char revbuf[20];      /* long enough for SVN_REVNUM_T_FMT */
  svn_string_t *creationdate, *creator_displayname;
  svn_delta_editor_t *editor;
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
  sprintf(revbuf, "%" SVN_REVNUM_T_FMT, new_rev);

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

  /* set up the editor for the delta process */
  editor = svn_delta_default_editor(pool);
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

  return SVN_NO_ERROR;
}


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
