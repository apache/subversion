/*
 * info.c:  return system-generated metadata about paths or URLs.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

/* ==================================================================== */



#include "client.h"
#include "svn_client.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_path.h"

#include "svn_private_config.h"


/* Helper: build an svn_info_t *INFO struct from svn_dirent_t DIRENT,
   allocated in POOL.  Pointer fields are copied by reference, not dup'd. */
static svn_error_t *
build_info_from_dirent (svn_info_t **info,
                        const svn_dirent_t *dirent,
                        const char *URL,
                        svn_revnum_t revision,
                        const char *repos_UUID,
                        const char *repos_root,
                        apr_pool_t *pool)
{
  svn_info_t *i = apr_pcalloc (pool, sizeof(*i));

  i->URL                  = URL;
  i->rev                  = revision;
  i->kind                 = dirent->kind;
  i->repos_UUID           = repos_UUID;
  i->repos_root_URL       = repos_root;
  i->last_changed_rev     = dirent->created_rev;
  i->last_changed_date    = dirent->time;
  i->last_changed_author  = dirent->last_author;;
  
  *info = i;
  return SVN_NO_ERROR;
}


/* Helper: build an svn_info_t *INFO struct from svn_wc_entry_t ENTRY,
   allocated in POOL.  Pointer fields are copied by reference, not dup'd. */
static svn_error_t *
build_info_from_entry (svn_info_t **info,
                       const svn_wc_entry_t *entry,
                       apr_pool_t *pool)
{
  svn_info_t *i = apr_pcalloc (pool, sizeof(*i));

  i->URL                  = entry->url;
  i->rev                  = entry->revision;
  i->kind                 = entry->kind;
  i->repos_UUID           = entry->uuid;
  i->last_changed_rev     = entry->cmt_rev;
  i->last_changed_date    = entry->cmt_date;
  i->last_changed_author  = entry->cmt_author;
  
  /* entry-specific stuff */
  i->has_wc_info          = TRUE;
  i->schedule             = entry->schedule;
  i->copyfrom_url         = entry->copyfrom_url;
  i->copyfrom_rev         = entry->copyfrom_rev;
  i->text_time            = entry->text_time;
  i->prop_time            = entry->prop_time;
  i->checksum             = entry->checksum;
  i->conflict_old         = entry->conflict_old;
  i->conflict_new         = entry->conflict_new;
  i->conflict_wrk         = entry->conflict_wrk;
  i->prejfile             = entry->prejfile;

  *info = i;
  return SVN_NO_ERROR;
}


/* Helper func for recursively fetching svn_dirent_t's from a remote
   directory and pushing them at an info-receiver callback. */
static svn_error_t *
push_dir_info (svn_ra_session_t *ra_session,
               const char *session_URL,
               const char *dir,
               svn_revnum_t rev,
               const char *repos_UUID,
               const char *repos_root,
               svn_info_receiver_t receiver,
               void *receiver_baton,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  apr_hash_t *tmpdirents;
  svn_dirent_t *the_ent;
  svn_info_t *info;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);

  SVN_ERR (svn_ra_get_dir (ra_session, dir, rev, &tmpdirents, 
                           NULL, NULL, pool));

  for (hi = apr_hash_first (pool, tmpdirents); hi; hi = apr_hash_next (hi))
    {
      const char *path, *URL;
      const void *key;
      void *val;

      svn_pool_clear (subpool);

      if (ctx->cancel_func)
        SVN_ERR (ctx->cancel_func (ctx->cancel_baton));

      apr_hash_this (hi, &key, NULL, &val);
      the_ent = val;

      path = svn_path_join (dir, key, subpool);
      URL  = svn_path_url_add_component (session_URL, key, subpool);

      SVN_ERR (build_info_from_dirent (&info, the_ent, URL, rev,
                                       repos_UUID, repos_root, subpool));

      SVN_ERR (receiver (receiver_baton, path, info, subpool));

      if (the_ent->kind == svn_node_dir)
        SVN_ERR (push_dir_info (ra_session, URL, path,
                                rev, repos_UUID, repos_root,
                                receiver, receiver_baton,
                                ctx, subpool));
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}




/* Callback and baton for crawl_entries() walk over entries files. */
struct found_entry_baton
{
  svn_info_receiver_t receiver;
  void *receiver_baton;         
};

static svn_error_t *
info_found_entry_callback (const char *path,
                           const svn_wc_entry_t *entry,
                           void *walk_baton,
                           apr_pool_t *pool)
{
  struct found_entry_baton *fe_baton = walk_baton;
  svn_info_t *info;

  /* We're going to receive dirents twice;  we want to ignore the
     first one (where it's a child of a parent dir), and only print
     the second one (where we're looking at THIS_DIR.)  */
  if ((entry->kind == svn_node_dir) 
      && (strcmp (entry->name, SVN_WC_ENTRY_THIS_DIR)))
    return SVN_NO_ERROR;

  SVN_ERR (build_info_from_entry (&info, entry, pool));
 
  return fe_baton->receiver (fe_baton->receiver_baton, path, info, pool);
}



static const svn_wc_entry_callbacks_t 
entry_walk_callbacks =
  {
    info_found_entry_callback
  };


/* Helper function:  push the svn_wc_entry_t for WCPATH at
   RECEIVER/BATON, and possibly recurse over more entries. */
static svn_error_t *
crawl_entries (const char *wcpath,
               svn_info_receiver_t receiver,
               void *receiver_baton,               
               svn_boolean_t recurse,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  svn_info_t *info;
  struct found_entry_baton fe_baton;
  
  SVN_ERR (svn_wc_adm_probe_open3 (&adm_access, NULL, wcpath, FALSE,
                                   recurse ? -1 : 0,
                                   ctx->cancel_func, ctx->cancel_baton,
                                   pool));
  SVN_ERR (svn_wc_entry (&entry, wcpath, adm_access, FALSE, pool));
  if (! entry)
    {
      return svn_error_createf (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                _("Cannot read entry for '%s'"), wcpath);
    }

  SVN_ERR (build_info_from_entry (&info, entry, pool));
  fe_baton.receiver = receiver;
  fe_baton.receiver_baton = receiver_baton;

  if (entry->kind == svn_node_file)
    return receiver (receiver_baton, wcpath, info, pool);

  else if (entry->kind == svn_node_dir)
    {
      if (recurse)
        SVN_ERR (svn_wc_walk_entries2 (wcpath, adm_access,
                                       &entry_walk_callbacks, &fe_baton,
                                       FALSE, ctx->cancel_func,
                                       ctx->cancel_baton, pool));
      else
        return receiver (receiver_baton, wcpath, info, pool);
    }
      
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_info (const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_info_receiver_t receiver,
                 void *receiver_baton,
                 svn_boolean_t recurse,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_ra_session_t *ra_session, *parent_ra_session;
  svn_revnum_t rev;
  const char *url;
  svn_node_kind_t url_kind;
  const char *repos_root_URL, *repos_UUID;
  apr_hash_t *parent_ents;
  const char *parent_url, *base_name;
  svn_dirent_t *the_ent;
  svn_info_t *info;
   
  if ((revision == NULL 
       || revision->kind == svn_opt_revision_unspecified)
      && (peg_revision == NULL
          || peg_revision->kind == svn_opt_revision_unspecified))
    {
      /* Do all digging in the working copy. */
      return crawl_entries (path_or_url,
                            receiver, receiver_baton,               
                            recurse, ctx, pool);
    }

  /* Go repository digging instead. */

  /* Trace rename history (starting at path_or_url@peg_revision) and
     return RA session to the possibly-renamed URL as it exists in REVISION.
     The ra_session returned will be anchored on this "final" URL. */
  SVN_ERR (svn_client__ra_session_from_path (&ra_session, &rev,
                                             &url, path_or_url, peg_revision,
                                             revision, ctx, pool));

  SVN_ERR (svn_ra_get_repos_root (ra_session, &repos_root_URL, pool));
  SVN_ERR (svn_ra_get_uuid (ra_session, &repos_UUID, pool));
  SVN_ERR (svn_ra_check_path (ra_session, "", rev, &url_kind, pool));

  if (url_kind == svn_node_none)
    return svn_error_createf (SVN_ERR_RA_ILLEGAL_URL, NULL,
                              _("URL '%s' non-existent in revision '%ld'"),
                              url, rev);

  /* Step 1:  find the dirent of the file or directory URL. */

  /* Open a new RA session to the item's parent. */
  svn_path_split (url, &parent_url, &base_name, pool);
  
  /* 'base_name' is now the last component of an URL, but we want
     to use it as a plain file name. Therefore, we must URI-decode
     it. */
  base_name = svn_path_uri_decode(base_name, pool);
  SVN_ERR (svn_client__open_ra_session (&parent_ra_session, parent_url,
                                        NULL,
                                        NULL, NULL, FALSE, TRUE, 
                                        ctx, pool));
  
  /* Get all parent's entries, no props. */
  SVN_ERR (svn_ra_get_dir (parent_ra_session, "", rev, &parent_ents, 
                           NULL, NULL, pool));
  
  /* Find the item's own dirent and push it an the info receiver.*/
  the_ent = apr_hash_get (parent_ents, base_name, APR_HASH_KEY_STRING);
  if (the_ent == NULL)
    return svn_error_createf (SVN_ERR_RA_ILLEGAL_URL, NULL,
                              _("URL '%s' non-existent in that revision"),
                              url);
  
  SVN_ERR (build_info_from_dirent (&info, the_ent, url, rev,
                                   repos_UUID, repos_root_URL, pool));
  SVN_ERR (receiver (receiver_baton, base_name, info, pool));
  
  /* Step 2:  possibly recurse, using the original RA session. */

  if (recurse && (url_kind == svn_node_dir))
    {
      SVN_ERR (push_dir_info (ra_session, url, "", rev,
                              repos_UUID, repos_root_URL,
                              receiver, receiver_baton,
                              ctx, pool));
    }

  return SVN_NO_ERROR;
}
