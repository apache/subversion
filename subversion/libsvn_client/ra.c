/*
 * ra.c :  routines for interacting with the RA layer
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



#include <apr_pools.h>
#include <assert.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_sorts.h"
#include "svn_ra.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_path.h"
#include "client.h"


static svn_error_t *
open_admin_tmp_file (apr_file_t **fp,
                     void *callback_baton,
                     apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = callback_baton;
  
  SVN_ERR (svn_wc_create_tmp_file (fp, cb->base_dir, TRUE, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
open_tmp_file (apr_file_t **fp,
               void *callback_baton,
               apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = callback_baton;
  const char *truepath;
  const char *ignored_filename;

  if (cb->base_dir)
    truepath = apr_pstrdup (pool, cb->base_dir);
  else
    SVN_ERR (svn_io_temp_dir (&truepath, pool));

  /* Tack on a made-up filename. */
  truepath = svn_path_join (truepath, "tempfile", pool);

  /* Open a unique file;  use APR_DELONCLOSE. */  
  SVN_ERR (svn_io_open_unique_file (fp, &ignored_filename,
                                    truepath, ".tmp", TRUE, pool));

  return SVN_NO_ERROR;
}


/* This implements the 'svn_ra_get_wc_prop_func_t' interface. */
static svn_error_t *
get_wc_prop (void *baton,
             const char *relpath,
             const char *name,
             const svn_string_t **value,
             apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = baton;

  *value = NULL;

  /* If we have a list of commit_items, search through that for a
     match for this relative URL. */
  if (cb->commit_items)
    {
      int i;
      for (i = 0; i < cb->commit_items->nelts; i++)
        {
          svn_client_commit_item_t *item
            = ((svn_client_commit_item_t **) cb->commit_items->elts)[i];
          if (! strcmp (relpath, 
                        svn_path_uri_decode (item->url, pool)))
            return svn_wc_prop_get (value, name, item->path, cb->base_access,
                                    pool);
        }

      return SVN_NO_ERROR;
    }

  /* If we don't have a base directory, then there are no properties. */
  else if (cb->base_dir == NULL)
    return SVN_NO_ERROR;

  return svn_wc_prop_get (value, name,
                          svn_path_join (cb->base_dir, relpath, pool),
                          cb->base_access, pool);
}

/* This implements the 'svn_ra_push_wc_prop_func_t' interface. */
static svn_error_t *
push_wc_prop (void *baton,
              const char *relpath,
              const char *name,
              const svn_string_t *value,
              apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = baton;
  int i;

  /* If we're committing, search through the commit_items list for a
     match for this relative URL. */
  if (! cb->commit_items)
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       "Attempt to set wc property '%s' on '%s' in a non-commit operation",
       name, relpath);

  for (i = 0; i < cb->commit_items->nelts; i++)
    {
      svn_client_commit_item_t *item
        = ((svn_client_commit_item_t **) cb->commit_items->elts)[i];
      
      if (strcmp (relpath, svn_path_uri_decode (item->url, pool)) == 0)
        {
          apr_pool_t *cpool = item->wcprop_changes->pool;
          svn_prop_t *prop = apr_palloc (cpool, sizeof (*prop));
          
          prop->name = apr_pstrdup (cpool, name);
          if (value)
            {
              prop->value
                = svn_string_ncreate (value->data, value->len, cpool);
            }
          else
            prop->value = NULL;
          
          /* Buffer the propchange to take effect during the
             post-commit process. */
          *((svn_prop_t **) apr_array_push (item->wcprop_changes)) = prop;
          return SVN_NO_ERROR;
        }
    }

  return SVN_NO_ERROR;
}


/* This implements the 'svn_ra_set_wc_prop_func_t' interface. */
static svn_error_t *
set_wc_prop (void *baton,
             const char *path,
             const char *name,
             const svn_string_t *value,
             apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = baton;
  svn_wc_adm_access_t *adm_access;
  const char *full_path = svn_path_join (cb->base_dir, path, pool);

  SVN_ERR (svn_wc_adm_probe_retrieve (&adm_access, cb->base_access,
                                      full_path, pool));
  return svn_wc_prop_set (name, value, full_path, adm_access, pool);
}


struct invalidate_wcprop_walk_baton
{
  /* The wcprop to invalidate. */
  const char *prop_name;

  /* Access baton for the top of the walk. */
  svn_wc_adm_access_t *base_access;
};


/* This implements the `found_entry' prototype in
   `svn_wc_entry_callbacks_t'. */
static svn_error_t *
invalidate_wcprop_for_entry (const char *path,
                             const svn_wc_entry_t *entry,
                             void *walk_baton,
                             apr_pool_t *pool)
{
  struct invalidate_wcprop_walk_baton *wb = walk_baton;
  svn_wc_adm_access_t *entry_access;

  SVN_ERR (svn_wc_adm_retrieve (&entry_access, wb->base_access,
                                ((entry->kind == svn_node_dir)
                                 ? path
                                 : svn_path_dirname (path, pool)),
                                pool));
  return svn_wc_prop_set (wb->prop_name, NULL, path, entry_access, pool);
}


/* This implements the `svn_ra_invalidate_wc_props_func_t' interface. */
static svn_error_t *
invalidate_wc_props (void *baton,
                    const char *path,
                    const char *prop_name,
                    apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = baton;
  svn_wc_entry_callbacks_t walk_callbacks;
  struct invalidate_wcprop_walk_baton wb;

  wb.base_access = cb->base_access;
  wb.prop_name = prop_name;
  walk_callbacks.found_entry = invalidate_wcprop_for_entry;

  SVN_ERR (svn_wc_walk_entries (svn_path_join (cb->base_dir, path, pool),
                                cb->base_access,
                                &walk_callbacks, &wb, FALSE, pool));

  return SVN_NO_ERROR;
}


svn_error_t * 
svn_client__open_ra_session (void **session_baton,
                             const svn_ra_plugin_t *ra_lib,
                             const char *base_url,
                             const char *base_dir,
                             svn_wc_adm_access_t *base_access,
                             apr_array_header_t *commit_items,
                             svn_boolean_t use_admin,
                             svn_boolean_t read_only_wc,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  svn_ra_callbacks_t *cbtable = apr_pcalloc (pool, sizeof(*cbtable));
  svn_client__callback_baton_t *cb = apr_pcalloc (pool, sizeof(*cb));
  
  cbtable->open_tmp_file = use_admin ? open_admin_tmp_file : open_tmp_file;
  cbtable->get_wc_prop = use_admin ? get_wc_prop : NULL;
  cbtable->set_wc_prop = read_only_wc ? NULL : set_wc_prop;
  cbtable->push_wc_prop = commit_items ? push_wc_prop : NULL;
  cbtable->invalidate_wc_props = read_only_wc ? NULL : invalidate_wc_props;
  cbtable->auth_baton = ctx->auth_baton; /* new-style */

  cb->base_dir = base_dir;
  cb->base_access = base_access;
  cb->pool = pool;
  cb->commit_items = commit_items;
  cb->config = ctx->config;

  SVN_ERR (ra_lib->open (session_baton, base_url, cbtable, cb, ctx->config,
                         pool));

  return SVN_NO_ERROR;
}



svn_error_t *
svn_client_uuid_from_url (const char **uuid,
                          const char *url,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool)
{
  svn_ra_plugin_t *ra_lib;  
  void *ra_baton, *session;
  apr_pool_t *subpool = svn_pool_create (pool);

  /* use subpool to create a temporary RA session */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, subpool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, url, subpool));
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, url,
                                        NULL, /* no base dir */
                                        NULL, NULL, FALSE, TRUE, 
                                        ctx, subpool));

  ra_lib->get_uuid (session, uuid, subpool);

  /* Copy the uuid in to the passed-in pool. */
  *uuid = apr_pstrdup (pool, *uuid);

  /* destroy the RA session */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_uuid_from_path (const char **uuid,
                           const char *path,
                           svn_wc_adm_access_t *adm_access,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;

  SVN_ERR (svn_wc_entry (&entry, path, adm_access,
                         TRUE,  /* show deleted */ pool));

  if (! entry)
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, NULL,
                              "Can't find entry for '%s'", path);

  if (entry->uuid)
    {
      *uuid = entry->uuid;
    }
  else
    {
      /* fallback to using the network. */
      SVN_ERR (svn_client_uuid_from_url (uuid, entry->url, ctx, pool));
    }

  return SVN_NO_ERROR;
}


struct log_message_baton
{
  /* The kind of the path we're tracing. */
  svn_node_kind_t kind;

  /* The path at which we are trying to find our versioned resource in
     the log output. */
  const char *last_path;

  /* Input revisions and output paths; the whole point of this little game. */
  svn_revnum_t start_revision;
  const char *start_path;
  svn_revnum_t end_revision;
  const char *end_path;
  svn_revnum_t peg_revision;
  const char *peg_path;
  
  /* Client context baton. */
  svn_client_ctx_t *ctx;

  /* A pool from which to allocate stuff stored in this baton. */
  apr_pool_t *pool;
};


svn_error_t *
svn_client__prev_log_path (const char **prev_path_p,
                           apr_hash_t *changed_paths,
                           const char *path,
                           svn_node_kind_t kind,
                           svn_revnum_t revision,
                           apr_pool_t *pool)
{
  svn_log_changed_path_t *change;
  const char *prev_path = NULL;

  /* If PATH was explicitly changed in this revision, that makes
     things easy -- we keep the path (but check to see).  If so,
     we'll either use the path, or, if was copied, use its
     copyfrom_path. */
  change = apr_hash_get (changed_paths, path, APR_HASH_KEY_STRING);
  if (change)
    {
      if (change->copyfrom_path)
        prev_path = apr_pstrdup (pool, change->copyfrom_path);
      else
        prev_path = path;
    }
  else if (apr_hash_count (changed_paths))
    {
      /* The path was not explicitly changed in this revision.  The
         fact that we're hearing about this revision implies, then,
         that the path was a child of some copied directory.  We need
         to find that directory, and effectively "re-base" our path on
         that directory's copyfrom_path. */
      int i;
      apr_array_header_t *paths;

      /* Build a sorted list of the changed paths. */
      paths = svn_sort__hash (changed_paths,
                              svn_sort_compare_items_as_paths, pool);

      /* Now, walk the list of paths backwards, looking a parent of
         our path that has copyfrom information. */
      for (i = paths->nelts; i > 0; i--)
        {
          svn_sort__item_t item = APR_ARRAY_IDX (paths,
                                                 i - 1, svn_sort__item_t);
          const char *ch_path = item.key;
          int len = strlen (ch_path);

          /* See if our path is the child of this change path.  If
             not, keep looking.  */
          if (! ((strncmp (ch_path, path, len) == 0) && (path[len] == '/')))
            continue;

          /* Okay, our path *is* a child of this change path.  If
             this change was copied, we just need to apply the
             portion of our path that is relative to this change's
             path, to the change's copyfrom path.  Otherwise, this
             change isn't really interesting to us, and our search
             continues. */
          change = apr_hash_get (changed_paths, ch_path, len);
          if (change->copyfrom_path)
            {
              prev_path = svn_path_join (change->copyfrom_path, 
                                         path + len + 1, pool);
              break;
            }
        }
    }

  /* If we didn't find what we expected to find, return an error.
     (Because directories bubble-up, we get a bunch of logs we might
     not want.  Be forgiving in that case.)  */
  if (! prev_path)
    {
      if (kind == svn_node_dir)
        prev_path = apr_pstrdup (pool, path);
      else
        return svn_error_createf (APR_EGENERAL, NULL,
                                  "Missing changed-path information for "
                                  "'%s' in revision %" SVN_REVNUM_T_FMT,
                                  path, revision);
    }
  
  *prev_path_p = prev_path;
  return SVN_NO_ERROR;
}


/* Implements svn_log_message_receiver_t; helper for 
   svn_client_repos_locations. */
static svn_error_t *
log_receiver (void *baton,
              apr_hash_t *changed_paths,
              svn_revnum_t revision,
              const char *author,
              const char *date,
              const char *message,
              apr_pool_t *pool)
{
  struct log_message_baton *lmb = baton;
  const char *current_path = lmb->last_path;
  const char *prev_path;

  /* See if the user is fed up with this time-consuming process yet. */
  if (lmb->ctx->cancel_func)
    SVN_ERR (lmb->ctx->cancel_func (lmb->ctx->cancel_baton));

  /* If we've already determined all of our paths, then frankly, why
     are we here?  Oh well, just do nothing. */
  if (lmb->start_path && lmb->peg_path && lmb->end_path)
    return SVN_NO_ERROR;

  /* Determine the paths for any of the revisions for which we haven't
     gotten paths already. */
  if ((! lmb->start_path) && (revision <= lmb->start_revision))
    lmb->start_path = apr_pstrdup (lmb->pool, current_path);
  if ((! lmb->end_path) && (revision <= lmb->end_revision))
    lmb->end_path = apr_pstrdup (lmb->pool, current_path);
  if ((! lmb->peg_path) && (revision <= lmb->peg_revision))
    lmb->peg_path = apr_pstrdup (lmb->pool, current_path);

  /* Figure out at which repository path our object of interest lived
     in the previous revision. */
  SVN_ERR (svn_client__prev_log_path (&prev_path, changed_paths,
                                      current_path, lmb->kind, 
                                      revision, pool));

  /* Squirrel away our "next place to look" path (suffer the strcmp
     hit to save on allocations). */
  if (strcmp (prev_path, current_path) != 0)
    lmb->last_path = apr_pstrdup (lmb->pool, prev_path);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__repos_locations (const char **start_url,
                             svn_opt_revision_t **start_revision,
                             const char **end_url,
                             svn_opt_revision_t **end_revision,
                             const char *path,
                             const svn_opt_revision_t *revision,
                             const svn_opt_revision_t *start,
                             const svn_opt_revision_t *end,
                             svn_ra_plugin_t *ra_lib,
                             void *ra_session,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  const char *repos_url;
  struct log_message_baton lmb = { 0 };
  apr_array_header_t *targets;
  const char *url;
  svn_revnum_t peg_revnum, start_revnum, end_revnum, youngest;
  svn_boolean_t pegrev_is_youngest = FALSE;

  /* Ensure that we are given some real revision data to work with.
     (It's okay if the END is unspecified -- in that case, we'll just
     set it to the same thing as START.)  */
  if (revision->kind == svn_opt_revision_unspecified
      || start->kind == svn_opt_revision_unspecified)
    return svn_error_create (SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);

  /* Ensure that we have a repository URL. */
  SVN_ERR (svn_client_url_from_path (&url, path, pool));
  if (! url)
    return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, NULL,
                              "'%s' has no URL", path);

  /* Resolve the opt_revision_t's. */
  SVN_ERR (svn_client__get_revision_number (&peg_revnum, ra_lib, 
                                            ra_session, revision, path, pool));
  SVN_ERR (svn_client__get_revision_number (&start_revnum, ra_lib, 
                                            ra_session, start, path, pool));
  if (end->kind == svn_opt_revision_unspecified)
    end_revnum = start_revnum;
  else
    SVN_ERR (svn_client__get_revision_number (&end_revnum, ra_lib, 
                                              ra_session, end, path, pool));

  /* Sanity check:  verify the that the peg-object exists in repos. */
  SVN_ERR (ra_lib->check_path (ra_session, "", peg_revnum, &(lmb.kind), pool));
  if (lmb.kind == svn_node_none)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, NULL,
       "path '%s' doesn't exist in revision %" SVN_REVNUM_T_FMT, 
       path, peg_revnum);

  /* Populate most of our log message baton structure. */
  SVN_ERR (ra_lib->get_repos_root (ra_session, &repos_url, pool));
  lmb.last_path = url + strlen (repos_url);
  lmb.start_revision = start_revnum;
  lmb.end_revision = end_revnum;
  lmb.peg_revision = peg_revnum;
  lmb.ctx = ctx;
  lmb.pool = pool;

  /* Figure out the youngest rev, and start to populate our baton. */
  if ((peg_revnum >= start_revnum) && (peg_revnum >= end_revnum))
    {
      youngest = peg_revnum;
      lmb.peg_path = lmb.last_path;
      pegrev_is_youngest = TRUE;      
    }
  else if (end_revnum > peg_revnum)
    {
      if (end_revnum >= start_revnum)
        {        
          youngest = end_revnum;
          lmb.end_path = lmb.last_path;
        }
      else
        {
          youngest = start_revnum;
          lmb.start_path = lmb.last_path;
        }
    }
  else /* start_revnum > peg_revnum */
    {
      if (start_revnum >= end_revnum)
        {
          youngest = start_revnum;
          lmb.start_path = lmb.last_path;
        }
      else
        {
          youngest = end_revnum;
          lmb.end_path = lmb.last_path;
        }
    }
    
  /* If the peg revision is at least as big as our ending revision, we
     don't need to search for a path in that peg revision. */
  if (peg_revnum >= end_revnum)
    lmb.peg_path = lmb.last_path;

  /* Build a one-item TARGETS array, as input to ra->get_log() */
  targets = apr_array_make (pool, 1, sizeof (const char *));
  APR_ARRAY_PUSH (targets, const char *) = "";

  /* Let the RA layer drive our log information handler, which will do
     the work of finding the actual locations for our resource.
     Notice that we always run on the youngest rev of the 3 inputs. */
  SVN_ERR (ra_lib->get_log (ra_session, targets, youngest, 1,
                            TRUE, FALSE, log_receiver, &lmb, pool));

  /* We'd better have all the paths we were looking for! */
  if (! lmb.start_path)
    return svn_error_createf 
      (APR_EGENERAL, NULL,
       "Unable to find repository location for '%s' in revision %"
       SVN_REVNUM_T_FMT, path, start_revnum);
  if (! lmb.end_path)
    return svn_error_createf 
      (APR_EGENERAL, NULL,
       "Unable to find repository location for '%s' in revision %"
       SVN_REVNUM_T_FMT, path, end_revnum);
  if (! lmb.peg_path)
    return svn_error_createf 
      (APR_EGENERAL, NULL,
       "Unable to find repository location for '%s' in revision %"
       SVN_REVNUM_T_FMT, path, peg_revnum);
    
  /* Repository paths might be absolute, but we want to treat them as
     relative. */
  if (lmb.start_path[0] == '/')
    lmb.start_path = lmb.start_path + 1;
  if (lmb.end_path[0] == '/')
    lmb.end_path = lmb.end_path + 1;
  if (lmb.peg_path[0] == '/')
    lmb.peg_path = lmb.peg_path + 1;

  /* If our peg revision was smaller than either of our range
     revisions, we need to make sure that our calculated peg path is
     the same as what we expected it to be. */
  if (! pegrev_is_youngest)
    {
      if (strcmp (url, svn_path_join (repos_url, lmb.peg_path, pool)) != 0)
        return svn_error_createf
          (SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
           "'%s' in revision %" SVN_REVNUM_T_FMT " is an unrelated object.",
           path, youngest);
    }

  /* Set our return variables */
  *start_url = svn_path_join (repos_url, lmb.start_path, pool);
  *start_revision = apr_pcalloc (pool, sizeof (*start_revision));
  (*start_revision)->kind = svn_opt_revision_number;
  (*start_revision)->value.number = lmb.start_revision;
  if (end->kind != svn_opt_revision_unspecified)
    {
      *end_url = svn_path_join (repos_url, lmb.end_path, pool);
      *end_revision = apr_pcalloc (pool, sizeof (*end_revision));
      (*end_revision)->kind = svn_opt_revision_number;
      (*end_revision)->value.number = lmb.end_revision;
    }
    
  return SVN_NO_ERROR;
}
