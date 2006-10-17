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
#include "svn_props.h"
#include "client.h"

#include "svn_private_config.h"


static svn_error_t *
open_admin_tmp_file(apr_file_t **fp,
                    void *callback_baton,
                    apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = callback_baton;

  SVN_ERR(svn_wc_create_tmp_file2(fp, NULL, cb->base_dir,
                                  svn_io_file_del_on_close, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
open_tmp_file(apr_file_t **fp,
              void *callback_baton,
              apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = callback_baton;
  const char *truepath;

  if (cb->base_dir && ! cb->read_only_wc)
    truepath = apr_pstrdup(pool, cb->base_dir);
  else
    SVN_ERR(svn_io_temp_dir(&truepath, pool));

  /* Tack on a made-up filename. */
  truepath = svn_path_join(truepath, "tempfile", pool);

  /* Open a unique file;  use APR_DELONCLOSE. */
  SVN_ERR(svn_io_open_unique_file2(fp, NULL, truepath, ".tmp",
                                   svn_io_file_del_on_close, pool));

  return SVN_NO_ERROR;
}


/* This implements the 'svn_ra_get_wc_prop_func_t' interface. */
static svn_error_t *
get_wc_prop(void *baton,
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
          svn_client_commit_item2_t *item
            = APR_ARRAY_IDX(cb->commit_items, i,
                            svn_client_commit_item2_t *);
          if (! strcmp(relpath, 
                       svn_path_uri_decode(item->url, pool)))
            return svn_wc_prop_get(value, name, item->path, cb->base_access,
                                   pool);
        }

      return SVN_NO_ERROR;
    }

  /* If we don't have a base directory, then there are no properties. */
  else if (cb->base_dir == NULL)
    return SVN_NO_ERROR;

  return svn_wc_prop_get(value, name,
                         svn_path_join(cb->base_dir, relpath, pool),
                         cb->base_access, pool);
}

/* This implements the 'svn_ra_push_wc_prop_func_t' interface. */
static svn_error_t *
push_wc_prop(void *baton,
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
       _("Attempt to set wc property '%s' on '%s' in a non-commit operation"),
       name, svn_path_local_style(relpath, pool));

  for (i = 0; i < cb->commit_items->nelts; i++)
    {
      svn_client_commit_item2_t *item
        = APR_ARRAY_IDX(cb->commit_items, i, svn_client_commit_item2_t *);
      
      if (strcmp(relpath, svn_path_uri_decode(item->url, pool)) == 0)
        {
          apr_pool_t *cpool = item->wcprop_changes->pool;
          svn_prop_t *prop = apr_palloc(cpool, sizeof(*prop));
          
          prop->name = apr_pstrdup(cpool, name);
          if (value)
            {
              prop->value
                = svn_string_ncreate(value->data, value->len, cpool);
            }
          else
            prop->value = NULL;
          
          /* Buffer the propchange to take effect during the
             post-commit process. */
          *((svn_prop_t **) apr_array_push(item->wcprop_changes)) = prop;
          return SVN_NO_ERROR;
        }
    }

  return SVN_NO_ERROR;
}


/* This implements the 'svn_ra_set_wc_prop_func_t' interface. */
static svn_error_t *
set_wc_prop(void *baton,
            const char *path,
            const char *name,
            const svn_string_t *value,
            apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = baton;
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  const char *full_path = svn_path_join(cb->base_dir, path, pool);

  SVN_ERR(svn_wc_entry(&entry, full_path, cb->base_access, FALSE, pool));
  if (! entry)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("'%s' is not under version control"),
                             svn_path_local_style(full_path, pool));

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, cb->base_access,
                              (entry->kind == svn_node_dir
                               ? full_path
                               : svn_path_dirname(full_path, pool)),
                              pool));
    
  /* We pass 1 for the 'force' parameter here.  Since the property is
     coming from the repository, we definitely want to accept it.
     Ideally, we'd raise a conflict if, say, the received property is
     svn:eol-style yet the file has a locally added svn:mime-type
     claiming that it's binary.  Probably the repository is still
     right, but the conflict would remind the user to make sure.
     Unfortunately, we don't have a clean mechanism for doing that
     here, so we just set the property and hope for the best. */
  return svn_wc_prop_set2(name, value, full_path, adm_access, TRUE, pool);
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
invalidate_wcprop_for_entry(const char *path,
                            const svn_wc_entry_t *entry,
                            void *walk_baton,
                            apr_pool_t *pool)
{
  struct invalidate_wcprop_walk_baton *wb = walk_baton;
  svn_wc_adm_access_t *entry_access;

  SVN_ERR(svn_wc_adm_retrieve(&entry_access, wb->base_access,
                              ((entry->kind == svn_node_dir)
                               ? path
                               : svn_path_dirname(path, pool)),
                              pool));
  /* It doesn't matter if we pass 0 or 1 for force here, since
     property deletion is always permitted. */
  return svn_wc_prop_set2(wb->prop_name, NULL, path, entry_access,
                          FALSE, pool);
}


/* This implements the `svn_ra_invalidate_wc_props_func_t' interface. */
static svn_error_t *
invalidate_wc_props(void *baton,
                    const char *path,
                    const char *prop_name,
                    apr_pool_t *pool)
{
  svn_client__callback_baton_t *cb = baton;
  svn_wc_entry_callbacks_t walk_callbacks;
  struct invalidate_wcprop_walk_baton wb;
  svn_wc_adm_access_t *adm_access;

  wb.base_access = cb->base_access;
  wb.prop_name = prop_name;
  walk_callbacks.found_entry = invalidate_wcprop_for_entry;

  path = svn_path_join(cb->base_dir, path, pool);
  SVN_ERR(svn_wc_adm_probe_retrieve(&adm_access, cb->base_access, path,
                                    pool));
  SVN_ERR(svn_wc_walk_entries2(path, adm_access, &walk_callbacks, &wb,
                               FALSE, cb->ctx->cancel_func,
                               cb->ctx->cancel_baton, pool));

  return SVN_NO_ERROR;
}


svn_error_t * 
svn_client__open_ra_session_internal(svn_ra_session_t **ra_session,
                                     const char *base_url,
                                     const char *base_dir,
                                     svn_wc_adm_access_t *base_access,
                                     apr_array_header_t *commit_items,
                                     svn_boolean_t use_admin,
                                     svn_boolean_t read_only_wc,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool)
{
  svn_ra_callbacks2_t *cbtable = apr_pcalloc(pool, sizeof(*cbtable));
  svn_client__callback_baton_t *cb = apr_pcalloc(pool, sizeof(*cb));
  
  cbtable->open_tmp_file = use_admin ? open_admin_tmp_file : open_tmp_file;
  cbtable->get_wc_prop = use_admin ? get_wc_prop : NULL;
  cbtable->set_wc_prop = read_only_wc ? NULL : set_wc_prop;
  cbtable->push_wc_prop = commit_items ? push_wc_prop : NULL;
  cbtable->invalidate_wc_props = read_only_wc ? NULL : invalidate_wc_props;
  cbtable->auth_baton = ctx->auth_baton; /* new-style */
  cbtable->progress_func = ctx->progress_func;
  cbtable->progress_baton = ctx->progress_baton;

  cb->base_dir = base_dir;
  cb->base_access = base_access;
  cb->read_only_wc = read_only_wc;
  cb->pool = pool;
  cb->commit_items = commit_items;
  cb->ctx = ctx;

  SVN_ERR(svn_ra_open2(ra_session, base_url, cbtable, cb,
                       ctx->config, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_open_ra_session(svn_ra_session_t **session,
                           const char *url,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  return svn_client__open_ra_session_internal(session, url, NULL, NULL, NULL,
                                              FALSE, TRUE, ctx, pool);
}


svn_error_t *
svn_client_uuid_from_url(const char **uuid,
                         const char *url,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* use subpool to create a temporary RA session */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, url,
                                               NULL, /* no base dir */
                                               NULL, NULL, FALSE, TRUE, 
                                               ctx, subpool));

  SVN_ERR(svn_ra_get_uuid(ra_session, uuid, subpool));

  /* Copy the uuid in to the passed-in pool. */
  *uuid = apr_pstrdup(pool, *uuid);

  /* destroy the RA session */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_uuid_from_path(const char **uuid,
                          const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;

  SVN_ERR(svn_wc_entry(&entry, path, adm_access,
                       TRUE,  /* show deleted */ pool));

  if (! entry)
    return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                             _("Can't find entry for '%s'"),
                             svn_path_local_style(path, pool));

  if (entry->uuid)
    {
      *uuid = entry->uuid;
    }
  else if (entry->url)
    {
      /* fallback to using the network. */
      SVN_ERR(svn_client_uuid_from_url(uuid, entry->url, ctx, pool));
    }
  else
    {
      /* Try the parent if it's the same working copy.  It's not
         entirely clear how this happens (possibly an old wc?) but it
         has been triggered by TSVN, see
         http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=101831
         Message-ID: <877jgjtkus.fsf@debian2.lan> */
      svn_boolean_t is_root;
      SVN_ERR(svn_wc_is_wc_root(&is_root, path, adm_access, pool));
      if (is_root)
        return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                                 _("'%s' has no URL"),
                                 svn_path_local_style(path, pool));
      else
        return svn_client_uuid_from_path(uuid, svn_path_dirname(path, pool),
                                         adm_access, ctx, pool);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__prev_log_path(const char **prev_path_p,
                          char *action_p,
                          svn_revnum_t *copyfrom_rev_p,
                          apr_hash_t *changed_paths,
                          const char *path,
                          svn_node_kind_t kind,
                          svn_revnum_t revision,
                          apr_pool_t *pool)
{
  svn_log_changed_path_t *change;
  const char *prev_path = NULL;

  /* It's impossible to find the predecessor path of a NULL path. */
  assert(path);

  /* Initialize our return values for the action and copyfrom_rev in
     case we have an unhandled case later on. */
  if (action_p)
    *action_p = 'M';
  if (copyfrom_rev_p)
    *copyfrom_rev_p = SVN_INVALID_REVNUM;

  /* See if PATH was explicitly changed in this revision. */
  change = apr_hash_get(changed_paths, path, APR_HASH_KEY_STRING);
  if (change)
    {
      /* If PATH was not newly added in this revision, then it may or may
         not have also been part of a moved subtree.  In this case, set a
         default previous path, but still look through the parents of this
         path for a possible copy event. */
      if (change->action != 'A' && change->action != 'R')
        {
          prev_path = path;
        }
      else
        {
          /* PATH is new in this revision.  This means it cannot have been
             part of a copied subtree. */
          if (change->copyfrom_path)
            prev_path = apr_pstrdup(pool, change->copyfrom_path);
          else
            prev_path = NULL;
          
          *prev_path_p = prev_path;
          if (action_p)
            *action_p = change->action;
          if (copyfrom_rev_p)
            *copyfrom_rev_p = change->copyfrom_rev;
          return SVN_NO_ERROR;
        }
    }
  
  if (apr_hash_count(changed_paths))
    {
      /* The path was not explicitly changed in this revision.  The
         fact that we're hearing about this revision implies, then,
         that the path was a child of some copied directory.  We need
         to find that directory, and effectively "re-base" our path on
         that directory's copyfrom_path. */
      int i;
      apr_array_header_t *paths;

      /* Build a sorted list of the changed paths. */
      paths = svn_sort__hash(changed_paths,
                             svn_sort_compare_items_as_paths, pool);

      /* Now, walk the list of paths backwards, looking a parent of
         our path that has copyfrom information. */
      for (i = paths->nelts; i > 0; i--)
        {
          svn_sort__item_t item = APR_ARRAY_IDX(paths,
                                                i - 1, svn_sort__item_t);
          const char *ch_path = item.key;
          int len = strlen(ch_path);

          /* See if our path is the child of this change path.  If
             not, keep looking.  */
          if (! ((strncmp(ch_path, path, len) == 0) && (path[len] == '/')))
            continue;

          /* Okay, our path *is* a child of this change path.  If
             this change was copied, we just need to apply the
             portion of our path that is relative to this change's
             path, to the change's copyfrom path.  Otherwise, this
             change isn't really interesting to us, and our search
             continues. */
          change = apr_hash_get(changed_paths, ch_path, len);
          if (change->copyfrom_path)
            {
              if (action_p)
                *action_p = change->action;
              if (copyfrom_rev_p)
                *copyfrom_rev_p = change->copyfrom_rev;
              prev_path = svn_path_join(change->copyfrom_path, 
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
        prev_path = apr_pstrdup(pool, path);
      else
        return svn_error_createf(SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
                                 _("Missing changed-path information for "
                                   "'%s' in revision %ld"),
                                 svn_path_local_style(path, pool), revision);
    }
  
  *prev_path_p = prev_path;
  return SVN_NO_ERROR;
}

/* ### This is to support 1.0 servers. */
struct log_receiver_baton
{
  /* The kind of the path we're tracing. */
  svn_node_kind_t kind;

  /* The path at which we are trying to find our versioned resource in
     the log output. */
  const char *last_path;

  /* Input revisions and output paths; the whole point of this little game. */
  svn_revnum_t start_revision;
  const char **start_path_p;
  svn_revnum_t end_revision;
  const char **end_path_p;
  svn_revnum_t peg_revision;
  const char *peg_path;
  
  /* Client context baton. */
  svn_client_ctx_t *ctx;

  /* A pool from which to allocate stuff stored in this baton. */
  apr_pool_t *pool;
};


/* Implements svn_log_message_receiver_t; helper for
   slow_get_locations.  As input, takes log_receiver_baton
   (defined above) and attempts to "fill in" all three paths in the
   baton over the course of many iterations. */
static svn_error_t *
log_receiver(void *baton,
             apr_hash_t *changed_paths,
             svn_revnum_t revision,
             const char *author,
             const char *date,
             const char *message,
             apr_pool_t *pool)
{
  struct log_receiver_baton *lrb = baton;
  const char *current_path = lrb->last_path;
  const char *prev_path;

  /* See if the user is fed up with this time-consuming process yet. */
  if (lrb->ctx->cancel_func)
    SVN_ERR(lrb->ctx->cancel_func(lrb->ctx->cancel_baton));

  /* No paths were changed in this revision.  Nothing to do. */
  if (!changed_paths)
    return SVN_NO_ERROR;

  /* If we've run off the end of the path's history, there's nothing
     to do.  (This should never happen with a properly functioning
     server, since we'd get no more log messages after the one where
     path was created.  But a malfunctioning server shouldn't cause us
     to trigger an assertion failure.) */
  if (! current_path)
    return SVN_NO_ERROR;
  
  /* Determine the paths for any of the revisions for which we haven't
     gotten paths already. */
  if ((! *lrb->start_path_p) && (revision <= lrb->start_revision))
    *lrb->start_path_p = apr_pstrdup(lrb->pool, current_path);
  if ((! *lrb->end_path_p) && (revision <= lrb->end_revision))
    *lrb->end_path_p = apr_pstrdup(lrb->pool, current_path);
  if ((! lrb->peg_path) && (revision <= lrb->peg_revision))
    lrb->peg_path = apr_pstrdup(lrb->pool, current_path);

  /* Figure out at which repository path our object of interest lived
     in the previous revision. */
  SVN_ERR(svn_client__prev_log_path(&prev_path, NULL, NULL, changed_paths,
                                    current_path, lrb->kind, 
                                    revision, pool));

  /* Squirrel away our "next place to look" path (suffer the strcmp
     hit to save on allocations). */
  if (! prev_path)
    lrb->last_path = NULL;
  else if (strcmp(prev_path, current_path) != 0)
    lrb->last_path = apr_pstrdup(lrb->pool, prev_path);

  return SVN_NO_ERROR;
}


/* Use the RA layer get_log() function to get the locations at START_REVNUM
   and END_REVNUM.
   The locations are put in START_PATH and END_PATH, respectively.  ABS_PATH
   is the path as seen in PEG_REVISION for which to get the locations.
   ORIG_PATH is only used for error messages.
   ### This is needed for 1.0.x servers, which don't have the get_locations
   RA layer function. */
static svn_error_t *
slow_locations(const char **start_path, const char** end_path,
               const char *abs_path, svn_revnum_t peg_revnum,
               svn_revnum_t start_revnum, svn_revnum_t end_revnum,
               const char *orig_path,
               svn_ra_session_t *ra_session,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  struct log_receiver_baton lrb = { 0 };
  apr_array_header_t *targets;
  svn_revnum_t youngest, oldest;
  svn_boolean_t pegrev_is_youngest = FALSE;

  /* Sanity check:  verify that the peg-object exists in repos. */
  SVN_ERR(svn_ra_check_path(ra_session, "", peg_revnum, &(lrb.kind), pool));
  if (lrb.kind == svn_node_none)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, NULL,
       _("path '%s' doesn't exist in revision %ld"),
       orig_path, peg_revnum);

  /* Populate most of our log receiver baton structure. */
  lrb.last_path = abs_path;
  lrb.start_revision = start_revnum;
  lrb.end_revision = end_revnum;
  lrb.peg_revision = peg_revnum;
  lrb.start_path_p = start_path;
  lrb.end_path_p = end_path;
  lrb.ctx = ctx;
  lrb.pool = pool;

  /* Figure out the youngest and oldest revs. */
  youngest = peg_revnum;
  youngest = (start_revnum > youngest) ? start_revnum : youngest;
  youngest = (end_revnum > youngest) ? end_revnum : youngest;
  oldest = peg_revnum;
  oldest = (start_revnum < oldest) ? start_revnum : oldest;
  oldest = (end_revnum < oldest) ? end_revnum : oldest;

  /* Build a one-item TARGETS array, as input to ra->get_log() */
  targets = apr_array_make(pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(targets, const char *) = "";

  /* Let the RA layer drive our log information handler, which will do
     the work of finding the actual locations for our resource.
     Notice that we always run on the youngest rev of the 3 inputs. */
  SVN_ERR(svn_ra_get_log(ra_session, targets, youngest, oldest, 0,
                         TRUE, FALSE, log_receiver, &lrb, pool));

  /* If the received log information did not cover any of the
     requested revisions, use the last known path.  (This normally
     just means that ABS_PATH was not modified between the requested
     revision and OLDEST.  If the file was created at some point after
     OLDEST, then lrb.last_path should be NULL.) */
  if (! lrb.peg_path)
    lrb.peg_path = lrb.last_path;
  if (! *start_path)
    *start_path = lrb.last_path;
  if (! *end_path)
    *end_path = lrb.last_path;

  /* Check that we got the peg path. */
  if (! lrb.peg_path)
    return svn_error_createf 
      (APR_EGENERAL, NULL,
       _("Unable to find repository location for '%s' in revision %ld"),
       orig_path, peg_revnum);

  /* If our peg revision was smaller than either of our range
     revisions, we need to make sure that our calculated peg path is
     the same as what we expected it to be. */
  if (! pegrev_is_youngest)
    {
      if (strcmp(abs_path, lrb.peg_path) != 0)
        return svn_error_createf
          (SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
           _("'%s' in revision %ld is an unrelated object"),
           orig_path, youngest);
    }

  return SVN_NO_ERROR;
}
                
svn_error_t *
svn_client__repos_locations(const char **start_url,
                            svn_opt_revision_t **start_revision,
                            const char **end_url,
                            svn_opt_revision_t **end_revision,
                            svn_ra_session_t *ra_session,
                            const char *path,
                            const svn_opt_revision_t *revision,
                            const svn_opt_revision_t *start,
                            const svn_opt_revision_t *end,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  const char *repos_url;
  const char *url;
  const char *start_path = NULL;
  const char *end_path = NULL;
  svn_revnum_t peg_revnum = SVN_INVALID_REVNUM;
  svn_revnum_t start_revnum, end_revnum;
  apr_array_header_t *revs;
  apr_hash_t *rev_locs;
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_error_t *err;

  /* Ensure that we are given some real revision data to work with.
     (It's okay if the END is unspecified -- in that case, we'll just
     set it to the same thing as START.)  */
  if (revision->kind == svn_opt_revision_unspecified
      || start->kind == svn_opt_revision_unspecified)
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);

  /* Check to see if this is schedule add with history working copy
     path.  If it is, then we need to use the URL and peg revision of
     the copyfrom information. */
  if (! svn_path_is_url(path))
    {
      svn_wc_adm_access_t *adm_access;
      const svn_wc_entry_t *entry;
      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path,
                                     FALSE, 0, ctx->cancel_func,
                                     ctx->cancel_baton, pool));
      SVN_ERR(svn_wc_entry(&entry, path, adm_access, FALSE, pool));
      SVN_ERR(svn_wc_adm_close(adm_access));
      if (entry->copyfrom_url && revision->kind == svn_opt_revision_working)
        {
          url = entry->copyfrom_url;
          peg_revnum = entry->copyfrom_rev;
          if (!entry->url || strcmp(entry->url, entry->copyfrom_url) != 0)
            {
              /* We can't use the caller provided RA session in this case */
              ra_session = NULL;
            }
        }
      else if (entry->url)
        {
          url = entry->url;
        }
      else
        {
          return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                                   _("'%s' has no URL"),
                                   svn_path_local_style(path, pool));
        }
    }
  else
    {
      url = path;
    }

  /* ### We should be smarter here.  If the callers just asks for BASE and
     WORKING revisions, we should already have the correct URL:s, so we
     don't need to do anything more here in that case. */

  /* Open a RA session to this URL if we don't have one already. */
  if (! ra_session)
    SVN_ERR(svn_client__open_ra_session_internal(&ra_session, url, NULL,
                                                 NULL, NULL, FALSE, TRUE,
                                                 ctx, subpool));

  /* Resolve the opt_revision_ts. */
  if (peg_revnum == SVN_INVALID_REVNUM)
    SVN_ERR(svn_client__get_revision_number(&peg_revnum,
                                            ra_session, revision, path,
                                            pool));
  
  SVN_ERR(svn_client__get_revision_number(&start_revnum,
                                          ra_session, start, path, pool));
  if (end->kind == svn_opt_revision_unspecified)
    end_revnum = start_revnum;
  else
    SVN_ERR(svn_client__get_revision_number(&end_revnum,
                                            ra_session, end, path, pool));

  /* Set the output revision variables. */
  *start_revision = apr_pcalloc(pool, sizeof(**start_revision));
  (*start_revision)->kind = svn_opt_revision_number;
  (*start_revision)->value.number = start_revnum;
  if (end->kind != svn_opt_revision_unspecified)
    {
      *end_revision = apr_pcalloc(pool, sizeof(**end_revision));
      (*end_revision)->kind = svn_opt_revision_number;
      (*end_revision)->value.number = end_revnum;
    }

  if (start_revnum == peg_revnum && end_revnum == peg_revnum)
    {
      /* Avoid a network request in the common easy case. */
      *start_url = url;
      if (end->kind != svn_opt_revision_unspecified)
        *end_url = url;
      svn_pool_destroy(subpool);
      return SVN_NO_ERROR;
    }
  
  SVN_ERR(svn_ra_get_repos_root(ra_session, &repos_url, subpool));

  revs = apr_array_make(subpool, 2, sizeof(svn_revnum_t));
  APR_ARRAY_PUSH(revs, svn_revnum_t) = start_revnum;
  if (end_revnum != start_revnum)
    APR_ARRAY_PUSH(revs, svn_revnum_t) = end_revnum;

  if (! (err = svn_ra_get_locations(ra_session, &rev_locs, "", peg_revnum,
                                    revs, subpool)))
    {
      start_path = apr_hash_get(rev_locs, &start_revnum,
                                sizeof(svn_revnum_t));
      end_path = apr_hash_get(rev_locs, &end_revnum, sizeof(svn_revnum_t));
    }
  else if (err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED)
    {
      svn_error_clear(err);
      /* Do it the slow way for 1.0.x servers. */
      SVN_ERR(slow_locations(&start_path, &end_path,
                             svn_path_uri_decode(url + strlen(repos_url),
                                                 subpool),
                             peg_revnum, start_revnum, end_revnum,
                             path, ra_session, ctx, subpool));
    }
  else
    return err;

  /* We'd better have all the paths we were looking for! */
  if (! start_path)
    return svn_error_createf 
      (SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
       _("Unable to find repository location for '%s' in revision %ld"),
       path, start_revnum);
  if (! end_path)
    return svn_error_createf 
      (SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
       _("The location for '%s' for revision %ld does not exist in the "
         "repository or refers to an unrelated object"),
       path, end_revnum);
    
  /* Repository paths might be absolute, but we want to treat them as
     relative.
     ### Aren't they always absolute? */
  if (start_path[0] == '/')
    start_path = start_path + 1;
  if (end_path[0] == '/')
    end_path = end_path + 1;

  /* Set our return variables */
  *start_url = svn_path_join(repos_url, svn_path_uri_encode(start_path,
                                                            pool), pool);
  if (end->kind != svn_opt_revision_unspecified)
    *end_url = svn_path_join(repos_url, svn_path_uri_encode(end_path,
                                                            pool), pool);
  
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}



svn_error_t *
svn_client__ra_session_from_path(svn_ra_session_t **ra_session_p,
                                 svn_revnum_t *rev_p,
                                 const char **url_p,
                                 const char *path_or_url,
                                 const svn_opt_revision_t *peg_revision_p,
                                 const svn_opt_revision_t *revision,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const char *initial_url, *url;
  const svn_opt_revision_t *good_rev;
  svn_opt_revision_t peg_revision, start_rev;
  svn_opt_revision_t dead_end_rev;
  svn_opt_revision_t *ignored_rev, *new_rev;
  svn_revnum_t rev;
  const char *ignored_url;
  
  SVN_ERR(svn_client_url_from_path(&initial_url, path_or_url, pool));
  if (! initial_url)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("'%s' has no URL"), path_or_url);

  /* If a peg revision was specified, but a desired revision was not,
     assume it is the same as the peg revision. */
  if (revision->kind == svn_opt_revision_unspecified &&
      peg_revision_p->kind != svn_opt_revision_unspecified)
    revision = peg_revision_p;
  
  if (svn_path_is_url(path_or_url))
    {
      /* URLs get a default starting rev of HEAD. */
      if (revision->kind == svn_opt_revision_unspecified)
        start_rev.kind = svn_opt_revision_head;
      else
        start_rev = *revision;
          
      /* If an explicit URL was passed in, the default peg revision is
         HEAD. */
      if (peg_revision_p->kind == svn_opt_revision_unspecified)
        peg_revision.kind = svn_opt_revision_head;
      else
        peg_revision = *peg_revision_p;
    }
  else
    {
      /* And a default starting rev of BASE. */
      if (revision->kind == svn_opt_revision_unspecified)
        start_rev.kind = svn_opt_revision_base;
      else
        start_rev = *revision;
      
      /* WC paths have a default peg revision of WORKING. */
      if (peg_revision_p->kind == svn_opt_revision_unspecified)
        peg_revision.kind = svn_opt_revision_working;
      else
        peg_revision = *peg_revision_p;
    }
  
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, initial_url,
                                               NULL, NULL, NULL,
                                               FALSE, FALSE, ctx, pool));

  dead_end_rev.kind = svn_opt_revision_unspecified;
  
  /* Run the history function to get the object's (possibly
     different) url in REVISION. */
  SVN_ERR(svn_client__repos_locations(&url, &new_rev,
                                      &ignored_url, &ignored_rev,
                                      ra_session,
                                      path_or_url, &peg_revision,
                                      /* search range: */
                                      &start_rev, &dead_end_rev,
                                      ctx, pool));
  good_rev = new_rev;

  /* Make the session point to the real URL. */
  SVN_ERR(svn_ra_reparent(ra_session, url, pool));

  /* Resolve good_rev into a real revnum. */
  SVN_ERR(svn_client__get_revision_number(&rev, ra_session,
                                          good_rev, url, pool));
  if (! SVN_IS_VALID_REVNUM(rev))
    SVN_ERR(svn_ra_get_latest_revnum(ra_session, &rev, pool));

  *ra_session_p = ra_session;
  *rev_p = rev;
  *url_p = url;

  return SVN_NO_ERROR;
}
