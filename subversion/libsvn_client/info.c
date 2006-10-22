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


/* Helper: build an svn_info_t *INFO struct from svn_dirent_t DIRENT
   and (possibly NULL) svn_lock_t LOCK, all allocated in POOL.
   Pointer fields are copied by reference, not dup'd. */
static svn_error_t *
build_info_from_dirent(svn_info_t **info,
                       const svn_dirent_t *dirent,
                       svn_lock_t *lock,
                       const char *URL,
                       svn_revnum_t revision,
                       const char *repos_UUID,
                       const char *repos_root,
                       apr_pool_t *pool)
{
  svn_info_t *tmpinfo = apr_pcalloc(pool, sizeof(*tmpinfo));

  tmpinfo->URL                  = URL;
  tmpinfo->rev                  = revision;
  tmpinfo->kind                 = dirent->kind;
  tmpinfo->repos_UUID           = repos_UUID;
  tmpinfo->repos_root_URL       = repos_root;
  tmpinfo->last_changed_rev     = dirent->created_rev;
  tmpinfo->last_changed_date    = dirent->time;
  tmpinfo->last_changed_author  = dirent->last_author;;
  tmpinfo->lock                 = lock;

  *info = tmpinfo;
  return SVN_NO_ERROR;
}


/* Helper: build an svn_info_t *INFO struct from svn_wc_entry_t ENTRY,
   allocated in POOL.  Pointer fields are copied by reference, not dup'd. */
static svn_error_t *
build_info_from_entry(svn_info_t **info,
                      const svn_wc_entry_t *entry,
                      apr_pool_t *pool)
{
  svn_info_t *tmpinfo = apr_pcalloc(pool, sizeof(*tmpinfo));

  tmpinfo->URL                  = entry->url;
  tmpinfo->rev                  = entry->revision;
  tmpinfo->kind                 = entry->kind;
  tmpinfo->repos_UUID           = entry->uuid;
  tmpinfo->repos_root_URL       = entry->repos;
  tmpinfo->last_changed_rev     = entry->cmt_rev;
  tmpinfo->last_changed_date    = entry->cmt_date;
  tmpinfo->last_changed_author  = entry->cmt_author;

  /* entry-specific stuff */
  tmpinfo->has_wc_info          = TRUE;
  tmpinfo->schedule             = entry->schedule;
  tmpinfo->copyfrom_url         = entry->copyfrom_url;
  tmpinfo->copyfrom_rev         = entry->copyfrom_rev;
  tmpinfo->text_time            = entry->text_time;
  tmpinfo->prop_time            = entry->prop_time;
  tmpinfo->checksum             = entry->checksum;
  tmpinfo->conflict_old         = entry->conflict_old;
  tmpinfo->conflict_new         = entry->conflict_new;
  tmpinfo->conflict_wrk         = entry->conflict_wrk;
  tmpinfo->prejfile             = entry->prejfile;

  /* lock stuff */
  if (entry->lock_token)  /* the token is the critical bit. */
    {
      tmpinfo->lock = apr_pcalloc(pool, sizeof(*(tmpinfo->lock)));

      tmpinfo->lock->token      = entry->lock_token;
      tmpinfo->lock->owner      = entry->lock_owner;
      tmpinfo->lock->comment    = entry->lock_comment;
      tmpinfo->lock->creation_date = entry->lock_creation_date;
    }

  *info = tmpinfo;
  return SVN_NO_ERROR;
}


/* The dirent fields we care about for our calls to svn_ra_get_dir2. */
#define DIRENT_FIELDS (SVN_DIRENT_KIND        | \
                       SVN_DIRENT_CREATED_REV | \
                       SVN_DIRENT_TIME        | \
                       SVN_DIRENT_LAST_AUTHOR)


/* Helper func for recursively fetching svn_dirent_t's from a remote
   directory and pushing them at an info-receiver callback. */
static svn_error_t *
push_dir_info(svn_ra_session_t *ra_session,
              const char *session_URL,
              const char *dir,
              svn_revnum_t rev,
              const char *repos_UUID,
              const char *repos_root,
              svn_info_receiver_t receiver,
              void *receiver_baton,
              svn_client_ctx_t *ctx,
              apr_hash_t *locks,
              apr_pool_t *pool)
{
  apr_hash_t *tmpdirents;
  svn_dirent_t *the_ent;
  svn_info_t *info;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_ra_get_dir2(ra_session, &tmpdirents, NULL, NULL,
                          dir, rev, DIRENT_FIELDS, pool));

  for (hi = apr_hash_first(pool, tmpdirents); hi; hi = apr_hash_next(hi))
    {
      const char *path, *URL, *fs_path;
      const void *key;
      svn_lock_t *lock;
      void *val;

      svn_pool_clear(subpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      apr_hash_this(hi, &key, NULL, &val);
      the_ent = val;

      path = svn_path_join(dir, key, subpool);
      URL  = svn_path_url_add_component(session_URL, key, subpool);
     
      fs_path = svn_path_is_child(repos_root, URL, subpool);
      fs_path = apr_pstrcat(subpool, "/", fs_path, NULL);
      fs_path = svn_path_uri_decode(fs_path, subpool);

      lock = apr_hash_get(locks, fs_path, APR_HASH_KEY_STRING);

      SVN_ERR(build_info_from_dirent(&info, the_ent, lock, URL, rev,
                                     repos_UUID, repos_root, subpool));

      SVN_ERR(receiver(receiver_baton, path, info, subpool));

      if (the_ent->kind == svn_node_dir)
        SVN_ERR(push_dir_info(ra_session, URL, path,
                              rev, repos_UUID, repos_root,
                              receiver, receiver_baton,
                              ctx, locks, subpool));
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}



/* Callback and baton for crawl_entries() walk over entries files. */
struct found_entry_baton
{
  svn_info_receiver_t receiver;
  void *receiver_baton;         
};

static svn_error_t *
info_found_entry_callback(const char *path,
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
      && (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR)))
    return SVN_NO_ERROR;

  SVN_ERR(build_info_from_entry(&info, entry, pool));
 
  return fe_baton->receiver(fe_baton->receiver_baton, path, info, pool);
}



static const svn_wc_entry_callbacks_t 
entry_walk_callbacks =
  {
    info_found_entry_callback
  };


/* Helper function:  push the svn_wc_entry_t for WCPATH at
   RECEIVER/BATON, and possibly recurse over more entries. */
static svn_error_t *
crawl_entries(const char *wcpath,
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
  
  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, wcpath, FALSE,
                                 recurse ? -1 : 0,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 pool));
  SVN_ERR(svn_wc_entry(&entry, wcpath, adm_access, FALSE, pool));
  if (! entry)
    {
      return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                               _("Cannot read entry for '%s'"), wcpath);
    }

  SVN_ERR(build_info_from_entry(&info, entry, pool));
  fe_baton.receiver = receiver;
  fe_baton.receiver_baton = receiver_baton;

  if (entry->kind == svn_node_file)
    return receiver(receiver_baton, wcpath, info, pool);

  else if (entry->kind == svn_node_dir)
    {
      if (recurse)
        SVN_ERR(svn_wc_walk_entries2(wcpath, adm_access,
                                     &entry_walk_callbacks, &fe_baton,
                                     FALSE, ctx->cancel_func,
                                     ctx->cancel_baton, pool));
      else
        return receiver(receiver_baton, wcpath, info, pool);
    }
      
  return SVN_NO_ERROR;
}

/* Set *SAME_P to TRUE if URL exists in the head of the repository and
   refers to the same resource as it does in REV, using POOL for
   temporary allocations.  RA_SESSION is an open RA session for URL.  */
static svn_error_t *
same_resource_in_head(svn_boolean_t *same_p,
                      const char *url,
                      svn_revnum_t rev,
                      svn_ra_session_t *ra_session,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  svn_error_t *err;
  svn_opt_revision_t start_rev, end_rev, peg_rev;
  svn_opt_revision_t *ignored_rev;
  const char *head_url, *ignored_url;

  start_rev.kind = svn_opt_revision_head;
  peg_rev.kind = svn_opt_revision_number;
  peg_rev.value.number = rev;
  end_rev.kind = svn_opt_revision_unspecified;

  err = svn_client__repos_locations(&head_url, &ignored_rev,
                                    &ignored_url, &ignored_rev,
                                    ra_session,
                                    url, &peg_rev,
                                    &start_rev, &end_rev,
                                    ctx, pool);
  if (err && 
      ((err->apr_err == SVN_ERR_CLIENT_UNRELATED_RESOURCES) ||
       (err->apr_err == SVN_ERR_FS_NOT_FOUND)))
    {
      svn_error_clear(err);
      *same_p = FALSE;
    }
  else if (err)
    return err;
  else
    {
      /* ### Currently, the URLs should always be equal, since we can't
         ### walk forwards in history. */
      if (strcmp(url, head_url) == 0)
        *same_p = TRUE;
      else
        *same_p = FALSE;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_info(const char *path_or_url,
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
  svn_lock_t *lock;
  svn_boolean_t related;
  apr_hash_t *parent_ents;
  const char *parent_url, *base_name;
  svn_dirent_t *the_ent;
  svn_info_t *info;
  svn_error_t *err;
   
  if ((revision == NULL 
       || revision->kind == svn_opt_revision_unspecified)
      && (peg_revision == NULL
          || peg_revision->kind == svn_opt_revision_unspecified))
    {
      /* Do all digging in the working copy. */
      return crawl_entries(path_or_url,
                           receiver, receiver_baton,               
                           recurse, ctx, pool);
    }

  /* Go repository digging instead. */

  /* Trace rename history (starting at path_or_url@peg_revision) and
     return RA session to the possibly-renamed URL as it exists in REVISION.
     The ra_session returned will be anchored on this "final" URL. */
  SVN_ERR(svn_client__ra_session_from_path(&ra_session, &rev,
                                           &url, path_or_url, peg_revision,
                                           revision, ctx, pool));

  SVN_ERR(svn_ra_get_repos_root(ra_session, &repos_root_URL, pool));
  SVN_ERR(svn_ra_get_uuid(ra_session, &repos_UUID, pool));
  
  svn_path_split(url, &parent_url, &base_name, pool);
  base_name = svn_path_uri_decode(base_name, pool);
  
  /* Get the dirent for the URL itself. */
  err = svn_ra_stat(ra_session, "", rev, &the_ent, pool);

  /* svn_ra_stat() will work against old versions of mod_dav_svn, but
     not old versions of svnserve.  In the case of a pre-1.2 svnserve,
     catch the specific error it throws:*/
  if (err && err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
    {
      /* Fall back to pre-1.2 strategy for fetching dirent's URL. */
      svn_error_clear(err);

      SVN_ERR(svn_ra_check_path(ra_session, "", rev, &url_kind, pool));      
      if (url_kind == svn_node_none)
        return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                 _("URL '%s' non-existent in revision '%ld'"),
                                 url, rev);

      if (strcmp(url, repos_root_URL) == 0)
        {
          /* In this universe, there's simply no way to fetch
             information about the repository's root directory!  So
             degrade gracefully.  Rather than throw error, return no
             information about the repos root, but at least give
             recursion a chance. */     
          goto recurse;
        }        
      
      /* Open a new RA session to the item's parent. */
      SVN_ERR(svn_client__open_ra_session_internal(&parent_ra_session,
                                                   parent_url, NULL,
                                                   NULL, NULL, FALSE, TRUE, 
                                                   ctx, pool));
      
      /* Get all parent's entries, and find the item's dirent in the hash. */
      SVN_ERR(svn_ra_get_dir2(parent_ra_session, &parent_ents, NULL, NULL,
                              "", rev, DIRENT_FIELDS, pool));
      the_ent = apr_hash_get(parent_ents, base_name, APR_HASH_KEY_STRING);
      if (the_ent == NULL)
        return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                 _("URL '%s' non-existent in revision '%ld'"),
                                 url, rev);
    }
  else if (err)
    {
      return err;
    }
  
  if (! the_ent)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("URL '%s' non-existent in revision '%ld'"),
                             url, rev);

  /* Check if the URL exists in HEAD and refers to the same resource.
     In this case, we check the repository for a lock on this URL.

     ### There is a possible race here, since HEAD might have changed since
     ### we checked it.  A solution to this problem could be to do the below
     ### check in a loop which only terminates if the HEAD revision is the same
     ### before and after this check.  That could, however, lead to a
     ### starvation situation instead.  */
  SVN_ERR(same_resource_in_head(&related, url, rev, ra_session, ctx, pool));
  if (related)
    {
      err = svn_ra_get_lock(ra_session, &lock, "", pool);

      /* An old mod_dav_svn will always work; there's nothing wrong with
         doing a PROPFIND for a property named "DAV:supportedlock". But
         an old svnserve will error. */
      if (err && err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
        {
          svn_error_clear(err);
          lock = NULL;
        }
      else if (err)
        return err;
    }
  else
    lock = NULL;

  /* Push the URL's dirent (and lock) at the callback.*/  
  SVN_ERR(build_info_from_dirent(&info, the_ent, lock, url, rev,
                                 repos_UUID, repos_root_URL, pool));
  SVN_ERR(receiver(receiver_baton, base_name, info, pool));

  /* Possibly recurse, using the original RA session. */  
 recurse:
  
  if (recurse && (the_ent->kind == svn_node_dir))
    {
      apr_hash_t *locks;

      if (peg_revision->kind == svn_opt_revision_head)
        {
          err = svn_ra_get_locks(ra_session, &locks, "", pool);
          
          /* Catch specific errors thrown by old mod_dav_svn or svnserve. */
          if (err && 
              (err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED
               || err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE))
            {
              svn_error_clear(err);
              locks = apr_hash_make(pool); /* use an empty hash */
            }
          else if (err)
            return err;
        }
      else
        locks = apr_hash_make(pool); /* use an empty hash */
        
      SVN_ERR(push_dir_info(ra_session, url, "", rev,
                            repos_UUID, repos_root_URL,
                            receiver, receiver_baton,
                            ctx, locks, pool));
    }

  return SVN_NO_ERROR;
}


svn_info_t *
svn_info_dup(const svn_info_t *info, apr_pool_t *pool)
{
  svn_info_t *dupinfo = apr_palloc(pool, sizeof(*dupinfo));

  /* Perform a trivial copy ... */
  *dupinfo = *info;

  /* ...and then re-copy stuff that needs to be duped into our pool. */
  if (info->URL)
    dupinfo->URL = apr_pstrdup(pool, info->URL);
  if (info->repos_root_URL)
    dupinfo->repos_root_URL = apr_pstrdup(pool, info->repos_root_URL);
  if (info->repos_UUID)
    dupinfo->repos_UUID = apr_pstrdup(pool, info->repos_UUID);
  if (info->last_changed_author)
    dupinfo->last_changed_author = apr_pstrdup(pool,
                                               info->last_changed_author);
  if (info->lock)
    dupinfo->lock = svn_lock_dup(info->lock, pool);
  if (info->copyfrom_url)
    dupinfo->copyfrom_url = apr_pstrdup(pool, info->copyfrom_url);
  if (info->checksum)
    dupinfo->checksum = apr_pstrdup(pool, info->checksum);
  if (info->conflict_old)
    dupinfo->conflict_old = apr_pstrdup(pool, info->conflict_old);
  if (info->conflict_new)
    dupinfo->conflict_new = apr_pstrdup(pool, info->conflict_new);
  if (info->conflict_wrk)
    dupinfo->conflict_wrk = apr_pstrdup(pool, info->conflict_wrk);
  if (info->prejfile)
    dupinfo->prejfile = apr_pstrdup(pool, info->prejfile);

  return dupinfo;
}
