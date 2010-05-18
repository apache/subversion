/*
 * deprecated.c:  holding file for all deprecated APIs.
 *                "we can't lose 'em, but we can shun 'em!"
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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

/* We define this here to remove any further warnings about the usage of
   deprecated functions in this file. */
#define SVN_DEPRECATED

#include "svn_wc.h"
#include "svn_subst.h"
#include "svn_pools.h"
#include "svn_props.h"

#include "wc.h"

#include "svn_private_config.h"




/*** From adm_crawler.c ***/

/*** Compatibility wrapper: turns an svn_ra_reporter2_t into an
     svn_ra_reporter3_t.

     This code looks like it duplicates code in libsvn_ra/ra_loader.c,
     but it does not.  That code makes an new thing look like an old
     thing; this code makes an old thing look like a new thing. ***/

struct wrap_3to2_report_baton {
  const svn_ra_reporter2_t *reporter;
  void *baton;
};

static svn_error_t *wrap_3to2_set_path(void *report_baton,
                                       const char *path,
                                       svn_revnum_t revision,
                                       svn_depth_t depth,
                                       svn_boolean_t start_empty,
                                       const char *lock_token,
                                       apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->set_path(wrb->baton, path, revision, start_empty,
                                 lock_token, pool);
}

static svn_error_t *wrap_3to2_delete_path(void *report_baton,
                                          const char *path,
                                          apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->delete_path(wrb->baton, path, pool);
}

static svn_error_t *wrap_3to2_link_path(void *report_baton,
                                        const char *path,
                                        const char *url,
                                        svn_revnum_t revision,
                                        svn_depth_t depth,
                                        svn_boolean_t start_empty,
                                        const char *lock_token,
                                        apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->link_path(wrb->baton, path, url, revision,
                                  start_empty, lock_token, pool);
}

static svn_error_t *wrap_3to2_finish_report(void *report_baton,
                                            apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->finish_report(wrb->baton, pool);
}

static svn_error_t *wrap_3to2_abort_report(void *report_baton,
                                           apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->abort_report(wrb->baton, pool);
}

static const svn_ra_reporter3_t wrap_3to2_reporter = {
  wrap_3to2_set_path,
  wrap_3to2_delete_path,
  wrap_3to2_link_path,
  wrap_3to2_finish_report,
  wrap_3to2_abort_report
};

svn_error_t *
svn_wc_crawl_revisions2(const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const svn_ra_reporter2_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_boolean_t recurse,
                        svn_boolean_t use_commit_times,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        svn_wc_traversal_info_t *traversal_info,
                        apr_pool_t *pool)
{
  struct wrap_3to2_report_baton wrb;
  wrb.reporter = reporter;
  wrb.baton = report_baton;

  return svn_wc_crawl_revisions3(path,
                                 adm_access,
                                 &wrap_3to2_reporter, &wrb,
                                 restore_files,
                                 SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                 FALSE,
                                 use_commit_times,
                                 notify_func,
                                 notify_baton,
                                 traversal_info,
                                 pool);
}


/*** Compatibility wrapper: turns an svn_ra_reporter_t into an
     svn_ra_reporter2_t.

     This code looks like it duplicates code in libsvn_ra/ra_loader.c,
     but it does not.  That code makes an new thing look like an old
     thing; this code makes an old thing look like a new thing. ***/

struct wrap_2to1_report_baton {
  const svn_ra_reporter_t *reporter;
  void *baton;
};

static svn_error_t *wrap_2to1_set_path(void *report_baton,
                                       const char *path,
                                       svn_revnum_t revision,
                                       svn_boolean_t start_empty,
                                       const char *lock_token,
                                       apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->set_path(wrb->baton, path, revision, start_empty,
                                 pool);
}

static svn_error_t *wrap_2to1_delete_path(void *report_baton,
                                          const char *path,
                                          apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->delete_path(wrb->baton, path, pool);
}

static svn_error_t *wrap_2to1_link_path(void *report_baton,
                                        const char *path,
                                        const char *url,
                                        svn_revnum_t revision,
                                        svn_boolean_t start_empty,
                                        const char *lock_token,
                                        apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->link_path(wrb->baton, path, url, revision,
                                  start_empty, pool);
}

static svn_error_t *wrap_2to1_finish_report(void *report_baton,
                                            apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->finish_report(wrb->baton, pool);
}

static svn_error_t *wrap_2to1_abort_report(void *report_baton,
                                           apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->abort_report(wrb->baton, pool);
}

static const svn_ra_reporter2_t wrap_2to1_reporter = {
  wrap_2to1_set_path,
  wrap_2to1_delete_path,
  wrap_2to1_link_path,
  wrap_2to1_finish_report,
  wrap_2to1_abort_report
};

svn_error_t *
svn_wc_crawl_revisions(const char *path,
                       svn_wc_adm_access_t *adm_access,
                       const svn_ra_reporter_t *reporter,
                       void *report_baton,
                       svn_boolean_t restore_files,
                       svn_boolean_t recurse,
                       svn_boolean_t use_commit_times,
                       svn_wc_notify_func_t notify_func,
                       void *notify_baton,
                       svn_wc_traversal_info_t *traversal_info,
                       apr_pool_t *pool)
{
  struct wrap_2to1_report_baton wrb;
  svn_wc__compat_notify_baton_t nb;

  wrb.reporter = reporter;
  wrb.baton = report_baton;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_crawl_revisions2(path, adm_access, &wrap_2to1_reporter, &wrb,
                                 restore_files, recurse, use_commit_times,
                                 svn_wc__compat_call_notify_func, &nb,
                                 traversal_info,
                                 pool);
}

/*** From adm_files.c ***/
svn_error_t *
svn_wc_ensure_adm2(const char *path,
                   const char *uuid,
                   const char *url,
                   const char *repos,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  return svn_wc_ensure_adm3(path, uuid, url, repos, revision,
                            svn_depth_infinity, pool);
}


svn_error_t *
svn_wc_ensure_adm(const char *path,
                  const char *uuid,
                  const char *url,
                  svn_revnum_t revision,
                  apr_pool_t *pool)
{
  return svn_wc_ensure_adm2(path, uuid, url, NULL, revision, pool);
}

svn_error_t *
svn_wc_create_tmp_file(apr_file_t **fp,
                       const char *path,
                       svn_boolean_t delete_on_close,
                       apr_pool_t *pool)
{
  return svn_wc_create_tmp_file2(fp, NULL, path,
                                 delete_on_close
                                 ? svn_io_file_del_on_close
                                 : svn_io_file_del_none,
                                 pool);
}


/*** From adm_ops.c ***/
svn_error_t *
svn_wc_process_committed3(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t recurse,
                          svn_revnum_t new_revnum,
                          const char *rev_date,
                          const char *rev_author,
                          apr_array_header_t *wcprop_changes,
                          svn_boolean_t remove_lock,
                          const unsigned char *digest,
                          apr_pool_t *pool)
{
  return svn_wc_process_committed4(path, adm_access, recurse, new_revnum,
                                   rev_date, rev_author, wcprop_changes,
                                   remove_lock, FALSE, digest, pool);
}

svn_error_t *
svn_wc_process_committed2(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t recurse,
                          svn_revnum_t new_revnum,
                          const char *rev_date,
                          const char *rev_author,
                          apr_array_header_t *wcprop_changes,
                          svn_boolean_t remove_lock,
                          apr_pool_t *pool)
{
  return svn_wc_process_committed3(path, adm_access, recurse, new_revnum,
                                   rev_date, rev_author, wcprop_changes,
                                   remove_lock, NULL, pool);
}

svn_error_t *
svn_wc_process_committed(const char *path,
                         svn_wc_adm_access_t *adm_access,
                         svn_boolean_t recurse,
                         svn_revnum_t new_revnum,
                         const char *rev_date,
                         const char *rev_author,
                         apr_array_header_t *wcprop_changes,
                         apr_pool_t *pool)
{
  return svn_wc_process_committed2(path, adm_access, recurse, new_revnum,
                                   rev_date, rev_author, wcprop_changes,
                                   FALSE, pool);
}

svn_error_t *
svn_wc_delete2(const char *path,
               svn_wc_adm_access_t *adm_access,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  return svn_wc_delete3(path, adm_access, cancel_func, cancel_baton,
                        notify_func, notify_baton, FALSE, pool);
}

svn_error_t *
svn_wc_delete(const char *path,
              svn_wc_adm_access_t *adm_access,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              svn_wc_notify_func_t notify_func,
              void *notify_baton,
              apr_pool_t *pool)
{
  svn_wc__compat_notify_baton_t nb;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_delete2(path, adm_access, cancel_func, cancel_baton,
                        svn_wc__compat_call_notify_func, &nb, pool);
}

svn_error_t *
svn_wc_add2(const char *path,
            svn_wc_adm_access_t *parent_access,
            const char *copyfrom_url,
            svn_revnum_t copyfrom_rev,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_notify_func2_t notify_func,
            void *notify_baton,
            apr_pool_t *pool)
{
  return svn_wc_add3(path, parent_access, svn_depth_infinity,
                     copyfrom_url, copyfrom_rev,
                     cancel_func, cancel_baton,
                     notify_func, notify_baton, pool);
}

svn_error_t *
svn_wc_add(const char *path,
           svn_wc_adm_access_t *parent_access,
           const char *copyfrom_url,
           svn_revnum_t copyfrom_rev,
           svn_cancel_func_t cancel_func,
           void *cancel_baton,
           svn_wc_notify_func_t notify_func,
           void *notify_baton,
           apr_pool_t *pool)
{
  svn_wc__compat_notify_baton_t nb;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_add2(path, parent_access, copyfrom_url, copyfrom_rev,
                     cancel_func, cancel_baton,
                     svn_wc__compat_call_notify_func, &nb, pool);
}

svn_error_t *
svn_wc_revert2(const char *path,
               svn_wc_adm_access_t *parent_access,
               svn_boolean_t recursive,
               svn_boolean_t use_commit_times,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  return svn_wc_revert3(path, parent_access,
                        SVN_DEPTH_INFINITY_OR_EMPTY(recursive),
                        use_commit_times, NULL, cancel_func, cancel_baton,
                        notify_func, notify_baton, pool);
}

svn_error_t *
svn_wc_revert(const char *path,
              svn_wc_adm_access_t *parent_access,
              svn_boolean_t recursive,
              svn_boolean_t use_commit_times,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              svn_wc_notify_func_t notify_func,
              void *notify_baton,
              apr_pool_t *pool)
{
  svn_wc__compat_notify_baton_t nb;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_revert2(path, parent_access, recursive, use_commit_times,
                        cancel_func, cancel_baton,
                        svn_wc__compat_call_notify_func, &nb, pool);
}

svn_error_t *
svn_wc_resolved_conflict(const char *path,
                         svn_wc_adm_access_t *adm_access,
                         svn_boolean_t resolve_text,
                         svn_boolean_t resolve_props,
                         svn_boolean_t recurse,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,
                         apr_pool_t *pool)
{
  svn_wc__compat_notify_baton_t nb;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_resolved_conflict2(path, adm_access,
                                   resolve_text, resolve_props, recurse,
                                   svn_wc__compat_call_notify_func, &nb,
                                   NULL, NULL, pool);

}

svn_error_t *
svn_wc_resolved_conflict2(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t resolve_text,
                          svn_boolean_t resolve_props,
                          svn_boolean_t recurse,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *pool)
{
  return svn_wc_resolved_conflict3(path, adm_access, resolve_text,
                                   resolve_props,
                                   SVN_DEPTH_INFINITY_OR_EMPTY(recurse),
                                   svn_wc_conflict_choose_merged,
                                   notify_func, notify_baton, cancel_func,
                                   cancel_baton, pool);
}

svn_error_t *
svn_wc_resolved_conflict3(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t resolve_text,
                          svn_boolean_t resolve_props,
                          svn_depth_t depth,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *pool)
{
  return svn_wc_resolved_conflict4(path, adm_access, resolve_text,
                                   resolve_props, FALSE, depth,
                                   svn_wc_conflict_choose_merged,
                                   notify_func, notify_baton, cancel_func,
                                   cancel_baton, pool);
}

/*** From diff.c ***/
/* Used to wrap svn_wc_diff_callbacks_t. */
struct callbacks_wrapper_baton {
  const svn_wc_diff_callbacks_t *callbacks;
  void *baton;
};

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
file_changed(svn_wc_adm_access_t *adm_access,
             svn_wc_notify_state_t *contentstate,
             svn_wc_notify_state_t *propstate,
             svn_boolean_t *tree_conflicted,
             const char *path,
             const char *tmpfile1,
             const char *tmpfile2,
             svn_revnum_t rev1,
             svn_revnum_t rev2,
             const char *mimetype1,
             const char *mimetype2,
             const apr_array_header_t *propchanges,
             apr_hash_t *originalprops,
             void *diff_baton)
{
  struct callbacks_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  if (tmpfile2 != NULL)
    SVN_ERR(b->callbacks->file_changed(adm_access, contentstate, path,
                                       tmpfile1, tmpfile2,
                                       rev1, rev2, mimetype1, mimetype2,
                                       b->baton));
  if (propchanges->nelts > 0)
    SVN_ERR(b->callbacks->props_changed(adm_access, propstate, path,
                                        propchanges, originalprops,
                                        b->baton));

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
file_added(svn_wc_adm_access_t *adm_access,
           svn_wc_notify_state_t *contentstate,
           svn_wc_notify_state_t *propstate,
           svn_boolean_t *tree_conflicted,
           const char *path,
           const char *tmpfile1,
           const char *tmpfile2,
           svn_revnum_t rev1,
           svn_revnum_t rev2,
           const char *mimetype1,
           const char *mimetype2,
           const apr_array_header_t *propchanges,
           apr_hash_t *originalprops,
           void *diff_baton)
{
  struct callbacks_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  SVN_ERR(b->callbacks->file_added(adm_access, contentstate, path,
                                   tmpfile1, tmpfile2, rev1, rev2,
                                   mimetype1, mimetype2, b->baton));
  if (propchanges->nelts > 0)
    SVN_ERR(b->callbacks->props_changed(adm_access, propstate, path,
                                        propchanges, originalprops,
                                        b->baton));

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
file_deleted(svn_wc_adm_access_t *adm_access,
             svn_wc_notify_state_t *state,
             svn_boolean_t *tree_conflicted,
             const char *path,
             const char *tmpfile1,
             const char *tmpfile2,
             const char *mimetype1,
             const char *mimetype2,
             apr_hash_t *originalprops,
             void *diff_baton)
{
  struct callbacks_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  SVN_ERR_ASSERT(originalprops);

  return b->callbacks->file_deleted(adm_access, state, path,
                                    tmpfile1, tmpfile2, mimetype1, mimetype2,
                                    b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
dir_added(svn_wc_adm_access_t *adm_access,
          svn_wc_notify_state_t *state,
          svn_boolean_t *tree_conflicted,
          const char *path,
          svn_revnum_t rev,
          void *diff_baton)
{
  struct callbacks_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks->dir_added(adm_access, state, path, rev, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
dir_deleted(svn_wc_adm_access_t *adm_access,
            svn_wc_notify_state_t *state,
            svn_boolean_t *tree_conflicted,
            const char *path,
            void *diff_baton)
{
  struct callbacks_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks->dir_deleted(adm_access, state, path, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
dir_props_changed(svn_wc_adm_access_t *adm_access,
                  svn_wc_notify_state_t *state,
                  svn_boolean_t *tree_conflicted,
                  const char *path,
                  const apr_array_header_t *propchanges,
                  apr_hash_t *originalprops,
                  void *diff_baton)
{
  struct callbacks_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks->props_changed(adm_access, state, path, propchanges,
                                     originalprops, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t
   and svn_wc_diff_callbacks2_t. */
static svn_error_t *
dir_opened(svn_wc_adm_access_t *adm_access,
           svn_boolean_t *tree_conflicted,
           const char *path,
           svn_revnum_t rev,
           void *diff_baton)
{
  if (tree_conflicted)
    *tree_conflicted = FALSE;
  /* Do nothing. */
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t
   and svn_wc_diff_callbacks2_t. */
static svn_error_t *
dir_closed(svn_wc_adm_access_t *adm_access,
           svn_wc_notify_state_t *propstate,
           svn_wc_notify_state_t *contentstate,
           svn_boolean_t *tree_conflicted,
           const char *path,
           void *diff_baton)
{
  if (contentstate)
    *contentstate = svn_wc_notify_state_unknown;
  if (propstate)
    *propstate = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;
  /* Do nothing. */
  return SVN_NO_ERROR;
}

/* Used to wrap svn_diff_callbacks_t as an svn_wc_diff_callbacks3_t. */
static struct svn_wc_diff_callbacks3_t callbacks_wrapper = {
  file_changed,
  file_added,
  file_deleted,
  dir_added,
  dir_deleted,
  dir_props_changed,
  dir_opened,
  dir_closed
};



/* Used to wrap svn_wc_diff_callbacks2_t. */
struct callbacks2_wrapper_baton {
  const svn_wc_diff_callbacks2_t *callbacks2;
  void *baton;
};

/* An svn_wc_diff_callbacks3_t function for wrapping
 * svn_wc_diff_callbacks2_t. */
static svn_error_t *
file_changed2(svn_wc_adm_access_t *adm_access,
              svn_wc_notify_state_t *contentstate,
              svn_wc_notify_state_t *propstate,
              svn_boolean_t *tree_conflicted,
              const char *path,
              const char *tmpfile1,
              const char *tmpfile2,
              svn_revnum_t rev1,
              svn_revnum_t rev2,
              const char *mimetype1,
              const char *mimetype2,
              const apr_array_header_t *propchanges,
              apr_hash_t *originalprops,
              void *diff_baton)
{
  struct callbacks2_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks2->file_changed(adm_access, contentstate, propstate,
                                     path, tmpfile1, tmpfile2,
                                     rev1, rev2, mimetype1, mimetype2,
                                     propchanges, originalprops, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping
 * svn_wc_diff_callbacks2_t. */
static svn_error_t *
file_added2(svn_wc_adm_access_t *adm_access,
            svn_wc_notify_state_t *contentstate,
            svn_wc_notify_state_t *propstate,
            svn_boolean_t *tree_conflicted,
            const char *path,
            const char *tmpfile1,
            const char *tmpfile2,
            svn_revnum_t rev1,
            svn_revnum_t rev2,
            const char *mimetype1,
            const char *mimetype2,
            const apr_array_header_t *propchanges,
            apr_hash_t *originalprops,
            void *diff_baton)
{
  struct callbacks2_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks2->file_added(adm_access, contentstate, propstate, path,
                                   tmpfile1, tmpfile2, rev1, rev2,
                                   mimetype1, mimetype2, propchanges,
                                   originalprops, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping
 * svn_wc_diff_callbacks2_t. */
static svn_error_t *
file_deleted2(svn_wc_adm_access_t *adm_access,
              svn_wc_notify_state_t *state,
              svn_boolean_t *tree_conflicted,
              const char *path,
              const char *tmpfile1,
              const char *tmpfile2,
              const char *mimetype1,
              const char *mimetype2,
              apr_hash_t *originalprops,
              void *diff_baton)
{
  struct callbacks2_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks2->file_deleted(adm_access, state, path,
                                     tmpfile1, tmpfile2, mimetype1, mimetype2,
                                     originalprops, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping
 * svn_wc_diff_callbacks2_t. */
static svn_error_t *
dir_added2(svn_wc_adm_access_t *adm_access,
           svn_wc_notify_state_t *state,
           svn_boolean_t *tree_conflicted,
           const char *path,
           svn_revnum_t rev,
           void *diff_baton)
{
  struct callbacks2_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks2->dir_added(adm_access, state, path, rev, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping
 * svn_wc_diff_callbacks2_t. */
static svn_error_t *
dir_deleted2(svn_wc_adm_access_t *adm_access,
             svn_wc_notify_state_t *state,
             svn_boolean_t *tree_conflicted,
             const char *path,
             void *diff_baton)
{
  struct callbacks2_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks2->dir_deleted(adm_access, state, path, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping
 * svn_wc_diff_callbacks2_t. */
static svn_error_t *
dir_props_changed2(svn_wc_adm_access_t *adm_access,
                  svn_wc_notify_state_t *state,
                  svn_boolean_t *tree_conflicted,
                  const char *path,
                  const apr_array_header_t *propchanges,
                  apr_hash_t *originalprops,
                  void *diff_baton)
{
  struct callbacks2_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks2->dir_props_changed(adm_access, state, path, propchanges,
                                          originalprops, b->baton);
}

/* Used to wrap svn_diff_callbacks2_t as an svn_wc_diff_callbacks3_t. */
static struct svn_wc_diff_callbacks3_t callbacks2_wrapper = {
  file_changed2,
  file_added2,
  file_deleted2,
  dir_added2,
  dir_deleted2,
  dir_props_changed2,
  dir_opened,
  dir_closed
};


svn_error_t *
svn_wc_get_diff_editor4(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks2_t *callbacks,
                        void *callback_baton,
                        svn_depth_t depth,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const apr_array_header_t *changelists,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool)
{
  struct callbacks2_wrapper_baton *b = apr_palloc(pool, sizeof(*b));
  b->callbacks2 = callbacks;
  b->baton = callback_baton;
  return svn_wc_get_diff_editor5(anchor,
                                 target,
                                 &callbacks2_wrapper,
                                 b,
                                 depth,
                                 ignore_ancestry,
                                 use_text_base,
                                 reverse_order,
                                 cancel_func,
                                 cancel_baton,
                                 changelists,
                                 editor,
                                 edit_baton,
                                 pool);
}

svn_error_t *
svn_wc_get_diff_editor3(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks2_t *callbacks,
                        void *callback_baton,
                        svn_boolean_t recurse,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool)
{
  return svn_wc_get_diff_editor4(anchor,
                                 target,
                                 callbacks,
                                 callback_baton,
                                 SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                 ignore_ancestry,
                                 use_text_base,
                                 reverse_order,
                                 cancel_func,
                                 cancel_baton,
                                 NULL,
                                 editor,
                                 edit_baton,
                                 pool);
}

svn_error_t *
svn_wc_get_diff_editor2(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks_t *callbacks,
                        void *callback_baton,
                        svn_boolean_t recurse,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool)
{
  struct callbacks_wrapper_baton *b = apr_palloc(pool, sizeof(*b));
  b->callbacks = callbacks;
  b->baton = callback_baton;
  return svn_wc_get_diff_editor5(anchor, target, &callbacks_wrapper, b,
                                 SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                 ignore_ancestry, use_text_base,
                                 reverse_order, cancel_func, cancel_baton,
                                 NULL, editor, edit_baton, pool);
}

svn_error_t *
svn_wc_get_diff_editor(svn_wc_adm_access_t *anchor,
                       const char *target,
                       const svn_wc_diff_callbacks_t *callbacks,
                       void *callback_baton,
                       svn_boolean_t recurse,
                       svn_boolean_t use_text_base,
                       svn_boolean_t reverse_order,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       const svn_delta_editor_t **editor,
                       void **edit_baton,
                       apr_pool_t *pool)
{
  return svn_wc_get_diff_editor2(anchor, target, callbacks, callback_baton,
                                 recurse, FALSE, use_text_base, reverse_order,
                                 cancel_func, cancel_baton,
                                 editor, edit_baton, pool);
}

svn_error_t *
svn_wc_diff4(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks2_t *callbacks,
             void *callback_baton,
             svn_depth_t depth,
             svn_boolean_t ignore_ancestry,
             const apr_array_header_t *changelists,
             apr_pool_t *pool)
{
  struct callbacks2_wrapper_baton *b = apr_palloc(pool, sizeof(*b));
  b->callbacks2 = callbacks;
  b->baton = callback_baton;

  return svn_wc_diff5(anchor, target, &callbacks2_wrapper, b,
                      depth, ignore_ancestry, changelists, pool);
}

svn_error_t *
svn_wc_diff3(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks2_t *callbacks,
             void *callback_baton,
             svn_boolean_t recurse,
             svn_boolean_t ignore_ancestry,
             apr_pool_t *pool)
{
  return svn_wc_diff4(anchor, target, callbacks, callback_baton,
                      SVN_DEPTH_INFINITY_OR_FILES(recurse), ignore_ancestry,
                      NULL, pool);
}

svn_error_t *
svn_wc_diff2(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks_t *callbacks,
             void *callback_baton,
             svn_boolean_t recurse,
             svn_boolean_t ignore_ancestry,
             apr_pool_t *pool)
{
  struct callbacks_wrapper_baton *b = apr_pcalloc(pool, sizeof(*b));
  b->callbacks = callbacks;
  b->baton = callback_baton;
  return svn_wc_diff5(anchor, target, &callbacks_wrapper, b,
                      SVN_DEPTH_INFINITY_OR_FILES(recurse), ignore_ancestry,
                      NULL, pool);
}

svn_error_t *
svn_wc_diff(svn_wc_adm_access_t *anchor,
            const char *target,
            const svn_wc_diff_callbacks_t *callbacks,
            void *callback_baton,
            svn_boolean_t recurse,
            apr_pool_t *pool)
{
  return svn_wc_diff2(anchor, target, callbacks, callback_baton,
                      recurse, FALSE, pool);
}

/*** From entries.c ***/
svn_error_t *
svn_wc_walk_entries2(const char *path,
                     svn_wc_adm_access_t *adm_access,
                     const svn_wc_entry_callbacks_t *walk_callbacks,
                     void *walk_baton,
                     svn_boolean_t show_hidden,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *pool)
{
  svn_wc_entry_callbacks2_t walk_cb2 = { 0 };
  walk_cb2.found_entry = walk_callbacks->found_entry;
  walk_cb2.handle_error = svn_wc__walker_default_error_handler;
  return svn_wc_walk_entries3(path, adm_access,
                              &walk_cb2, walk_baton, svn_depth_infinity,
                              show_hidden, cancel_func, cancel_baton, pool);
}

svn_error_t *
svn_wc_walk_entries(const char *path,
                    svn_wc_adm_access_t *adm_access,
                    const svn_wc_entry_callbacks_t *walk_callbacks,
                    void *walk_baton,
                    svn_boolean_t show_hidden,
                    apr_pool_t *pool)
{
  return svn_wc_walk_entries2(path, adm_access, walk_callbacks,
                              walk_baton, show_hidden, NULL, NULL,
                              pool);
}

/*** From props.c ***/
svn_error_t *
svn_wc_parse_externals_description2(apr_array_header_t **externals_p,
                                    const char *parent_directory,
                                    const char *desc,
                                    apr_pool_t *pool)
{
  apr_array_header_t *list;
  apr_pool_t *subpool = svn_pool_create(pool);
  int i;

  SVN_ERR(svn_wc_parse_externals_description3(externals_p ? &list : NULL,
                                              parent_directory, desc,
                                              TRUE, subpool));

  if (externals_p)
    {
      *externals_p = apr_array_make(pool, list->nelts,
                                    sizeof(svn_wc_external_item_t *));
      for (i = 0; i < list->nelts; i++)
        {
          svn_wc_external_item2_t *item2 = APR_ARRAY_IDX(list, i,
                                             svn_wc_external_item2_t *);
          svn_wc_external_item_t *item = apr_palloc(pool, sizeof (*item));

          if (item2->target_dir)
            item->target_dir = apr_pstrdup(pool, item2->target_dir);
          if (item2->url)
            item->url = apr_pstrdup(pool, item2->url);
          item->revision = item2->revision;

          APR_ARRAY_PUSH(*externals_p, svn_wc_external_item_t *) = item;
        }
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_parse_externals_description(apr_hash_t **externals_p,
                                   const char *parent_directory,
                                   const char *desc,
                                   apr_pool_t *pool)
{
  apr_array_header_t *list;
  int i;

  SVN_ERR(svn_wc_parse_externals_description2(externals_p ? &list : NULL,
                                              parent_directory, desc, pool));

  /* Store all of the items into the hash if that was requested. */
  if (externals_p)
    {
      *externals_p = apr_hash_make(pool);
      for (i = 0; i < list->nelts; i++)
        {
          svn_wc_external_item_t *item;
          item = APR_ARRAY_IDX(list, i, svn_wc_external_item_t *);

          apr_hash_set(*externals_p, item->target_dir,
                       APR_HASH_KEY_STRING, item);
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_prop_set2(const char *name,
                 const svn_string_t *value,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 svn_boolean_t skip_checks,
                 apr_pool_t *pool)
{
  return svn_wc_prop_set3(name, value, path, adm_access, skip_checks,
                          NULL, NULL, pool);
}

svn_error_t *
svn_wc_prop_set(const char *name,
                const svn_string_t *value,
                const char *path,
                svn_wc_adm_access_t *adm_access,
                apr_pool_t *pool)
{
  return svn_wc_prop_set2(name, value, path, adm_access, FALSE, pool);
}

/*** From status.c ***/
struct status_editor3_compat_baton
{
  svn_wc_status_func2_t old_func;
  void *old_baton;
};

static svn_error_t *
status_editor3_compat_func(void *baton,
                           const char *path,
                           svn_wc_status2_t *status,
                           apr_pool_t *pool)
{
  struct status_editor3_compat_baton *secb = baton;

  secb->old_func(secb->old_baton, path, status);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_get_status_editor3(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_depth_t depth,
                          svn_boolean_t get_all,
                          svn_boolean_t no_ignore,
                          apr_array_header_t *ignore_patterns,
                          svn_wc_status_func2_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  struct status_editor3_compat_baton *secb = apr_palloc(pool, sizeof(*secb));
  secb->old_func = status_func;
  secb->old_baton = status_baton;

  return svn_wc_get_status_editor4(editor, edit_baton, set_locks_baton,
                                   edit_revision, anchor, target, depth,
                                   get_all, no_ignore, ignore_patterns,
                                   status_editor3_compat_func, secb,
                                   cancel_func, cancel_baton, traversal_info,
                                   pool);
}

svn_error_t *
svn_wc_get_status_editor2(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          apr_hash_t *config,
                          svn_boolean_t recurse,
                          svn_boolean_t get_all,
                          svn_boolean_t no_ignore,
                          svn_wc_status_func2_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  apr_array_header_t *ignores;
  SVN_ERR(svn_wc_get_default_ignores(&ignores, config, pool));
  return svn_wc_get_status_editor3(editor,
                                   edit_baton,
                                   set_locks_baton,
                                   edit_revision,
                                   anchor,
                                   target,
                                   SVN_DEPTH_INFINITY_OR_IMMEDIATES(recurse),
                                   get_all,
                                   no_ignore,
                                   ignores,
                                   status_func,
                                   status_baton,
                                   cancel_func,
                                   cancel_baton,
                                   traversal_info,
                                   pool);
}


/* Helpers for deprecated svn_wc_status_editor(), of type
   svn_wc_status_func2_t. */
struct old_status_func_cb_baton
{
  svn_wc_status_func_t original_func;
  void *original_baton;
};

static void old_status_func_cb(void *baton,
                               const char *path,
                               svn_wc_status2_t *status)
{
  struct old_status_func_cb_baton *b = baton;
  svn_wc_status_t *stat = (svn_wc_status_t *) status;

  b->original_func(b->original_baton, path, stat);
}

svn_error_t *
svn_wc_get_status_editor(const svn_delta_editor_t **editor,
                         void **edit_baton,
                         svn_revnum_t *edit_revision,
                         svn_wc_adm_access_t *anchor,
                         const char *target,
                         apr_hash_t *config,
                         svn_boolean_t recurse,
                         svn_boolean_t get_all,
                         svn_boolean_t no_ignore,
                         svn_wc_status_func_t status_func,
                         void *status_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         svn_wc_traversal_info_t *traversal_info,
                         apr_pool_t *pool)
{
  struct old_status_func_cb_baton *b = apr_pcalloc(pool, sizeof(*b));
  apr_array_header_t *ignores;
  b->original_func = status_func;
  b->original_baton = status_baton;
  SVN_ERR(svn_wc_get_default_ignores(&ignores, config, pool));
  return svn_wc_get_status_editor3(editor, edit_baton, NULL, edit_revision,
                                   anchor, target,
                                   SVN_DEPTH_INFINITY_OR_IMMEDIATES(recurse),
                                   get_all, no_ignore, ignores,
                                   old_status_func_cb, b,
                                   cancel_func, cancel_baton,
                                   traversal_info, pool);
}

/*** From update_editor.c ***/
svn_error_t *
svn_wc_add_repos_file2(const char *dst_path,
                       svn_wc_adm_access_t *adm_access,
                       const char *new_text_base_path,
                       const char *new_text_path,
                       apr_hash_t *new_base_props,
                       apr_hash_t *new_props,
                       const char *copyfrom_url,
                       svn_revnum_t copyfrom_rev,
                       apr_pool_t *pool)
{
  svn_stream_t *new_base_contents;
  svn_stream_t *new_contents = NULL;

  SVN_ERR(svn_stream_open_readonly(&new_base_contents, new_text_base_path,
                                   pool, pool));

  if (new_text_path)
    {
      /* NOTE: the specified path may *not* be under version control.
         It is most likely sitting in .svn/tmp/. Thus, we cannot use the
         typical WC functions to access "special", "keywords" or "EOL"
         information. We need to look at the properties given to us. */

      /* If the new file is special, then we can simply open the given
         contents since it is already in normal form. */
      if (apr_hash_get(new_props,
                       SVN_PROP_SPECIAL, APR_HASH_KEY_STRING) != NULL)
        {
          SVN_ERR(svn_stream_open_readonly(&new_contents, new_text_path,
                                           pool, pool));
        }
      else
        {
          /* The new text contents need to be detrans'd into normal form. */
          svn_subst_eol_style_t eol_style;
          const char *eol_str;
          apr_hash_t *keywords = NULL;
          svn_string_t *list;

          list = apr_hash_get(new_props,
                              SVN_PROP_KEYWORDS, APR_HASH_KEY_STRING);
          if (list != NULL)
            {
              /* Since we are detranslating, all of the keyword values
                 can be "". */
              SVN_ERR(svn_subst_build_keywords2(&keywords,
                                                list->data,
                                                "", "", 0, "",
                                                pool));
              if (apr_hash_count(keywords) == 0)
                keywords = NULL;
            }

          svn_subst_eol_style_from_value(&eol_style, &eol_str,
                                         apr_hash_get(new_props,
                                                      SVN_PROP_EOL_STYLE,
                                                      APR_HASH_KEY_STRING));

          if (svn_subst_translation_required(eol_style, eol_str, keywords,
                                             FALSE, FALSE))
            {
              SVN_ERR(svn_subst_stream_detranslated(&new_contents,
                                                    new_text_path,
                                                    eol_style, eol_str,
                                                    FALSE,
                                                    keywords,
                                                    FALSE,
                                                    pool));
            }
          else
            {
              SVN_ERR(svn_stream_open_readonly(&new_contents, new_text_path,
                                               pool, pool));
            }
        }
    }

  SVN_ERR(svn_wc_add_repos_file3(dst_path, adm_access,
                                 new_base_contents, new_contents,
                                 new_base_props, new_props,
                                 copyfrom_url, copyfrom_rev,
                                 NULL, NULL, NULL, NULL,
                                 pool));

  /* The API contract states that the text files will be removed upon
     successful completion. add_repos_file3() does not remove the files
     since it only has streams on them. Toss 'em now. */
  svn_error_clear(svn_io_remove_file(new_text_base_path, pool));
  if (new_text_path)
    svn_error_clear(svn_io_remove_file(new_text_path, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add_repos_file(const char *dst_path,
                      svn_wc_adm_access_t *adm_access,
                      const char *new_text_path,
                      apr_hash_t *new_props,
                      const char *copyfrom_url,
                      svn_revnum_t copyfrom_rev,
                      apr_pool_t *pool)
{
  return svn_wc_add_repos_file2(dst_path, adm_access,
                                new_text_path, NULL,
                                new_props, NULL,
                                copyfrom_url, copyfrom_rev,
                                pool);
}

/*** From lock.c ***/

/* To preserve API compatibility with Subversion 1.0.0 */
svn_error_t *
svn_wc_adm_open(svn_wc_adm_access_t **adm_access,
                svn_wc_adm_access_t *associated,
                const char *path,
                svn_boolean_t write_lock,
                svn_boolean_t tree_lock,
                apr_pool_t *pool)
{
  return svn_wc_adm_open3(adm_access, associated, path, write_lock,
                          (tree_lock ? -1 : 0), NULL, NULL, pool);
}

svn_error_t *
svn_wc_adm_open2(svn_wc_adm_access_t **adm_access,
                 svn_wc_adm_access_t *associated,
                 const char *path,
                 svn_boolean_t write_lock,
                 int levels_to_lock,
                 apr_pool_t *pool)
{
  return svn_wc_adm_open3(adm_access, associated, path, write_lock,
                          levels_to_lock, NULL, NULL, pool);
}

svn_error_t *
svn_wc_adm_probe_open(svn_wc_adm_access_t **adm_access,
                      svn_wc_adm_access_t *associated,
                      const char *path,
                      svn_boolean_t write_lock,
                      svn_boolean_t tree_lock,
                      apr_pool_t *pool)
{
  return svn_wc_adm_probe_open3(adm_access, associated, path,
                                write_lock, (tree_lock ? -1 : 0),
                                NULL, NULL, pool);
}


svn_error_t *
svn_wc_adm_probe_open2(svn_wc_adm_access_t **adm_access,
                       svn_wc_adm_access_t *associated,
                       const char *path,
                       svn_boolean_t write_lock,
                       int levels_to_lock,
                       apr_pool_t *pool)
{
  return svn_wc_adm_probe_open3(adm_access, associated, path, write_lock,
                                levels_to_lock, NULL, NULL, pool);
}

svn_error_t *
svn_wc_adm_probe_try(svn_wc_adm_access_t **adm_access,
                     svn_wc_adm_access_t *associated,
                     const char *path,
                     svn_boolean_t write_lock,
                     svn_boolean_t tree_lock,
                     apr_pool_t *pool)
{
  return svn_wc_adm_probe_try3(adm_access, associated, path, write_lock,
                               (tree_lock ? -1 : 0), NULL, NULL, pool);
}

svn_error_t *
svn_wc_adm_close(svn_wc_adm_access_t *adm_access)
{
  /* This is the only pool we have access to. */
  apr_pool_t *scratch_pool = svn_wc_adm_access_pool(adm_access);

  return svn_wc_adm_close2(adm_access, scratch_pool);
}


/*** From translate.c ***/

svn_error_t *
svn_wc_translated_file(const char **xlated_p,
                       const char *vfile,
                       svn_wc_adm_access_t *adm_access,
                       svn_boolean_t force_repair,
                       apr_pool_t *pool)
{
  return svn_wc_translated_file2(xlated_p, vfile, vfile, adm_access,
                                 SVN_WC_TRANSLATE_TO_NF
                                 | (force_repair ?
                                    SVN_WC_TRANSLATE_FORCE_EOL_REPAIR : 0),
                                 pool);
}
