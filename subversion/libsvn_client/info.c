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

#include "svn_private_config.h"


/* Helper:  build an svn_info_t struct from an svn_dirent_t. */
static svn_error_t *
build_info_from_dirent (svn_info_t **info,
                        const svn_dirent_t *dirent,
                        apr_pool_t *pool)
{
  /* ### todo */

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
  /* svn_ra_session_t *ra_session;
     svn_revnum_t rev;
     svn_node_kind_t url_kind;
     const char *url; */
     
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

  /* Trace rename history and return RA session to the "final" path_or_url. */
  /*  SVN_ERR (svn_client__ra_session_from_path (&ra_session, &rev,
                                             &url, path_or_url, peg_revision,
                                             revision, ctx, pool));*/


  return SVN_NO_ERROR;
}
