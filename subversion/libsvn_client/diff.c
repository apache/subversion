/*
 * diff.c: comparing
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

/* ==================================================================== */

/* We define this here to remove any further warnings about the usage of
   experimental functions in this file. */
#define SVN_EXPERIMENTAL


/*** Includes. ***/

#include <apr_strings.h>
#include <apr_pools.h>
#include "svn_types.h"
#include "svn_wc.h"
#include "svn_diff.h"
#include "svn_mergeinfo.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_io.h"
#include "client.h"

#include "private/svn_client_shelf.h"
#include "private/svn_client_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_diff_tree.h"
#include "private/svn_ra_private.h"

#include "svn_private_config.h"


/** The logic behind 'svn diff' and 'svn merge'.  */


/* Hi!  This is a comment left behind by Karl, and Ben is too afraid
   to erase it at this time, because he's not fully confident that all
   this knowledge has been grokked yet.

   There are five cases:
      1. path is not a URL and start_revision != end_revision
      2. path is not a URL and start_revision == end_revision
      3. path is a URL and start_revision != end_revision
      4. path is a URL and start_revision == end_revision
      5. path is not a URL and no revisions given

   With only one distinct revision the working copy provides the
   other.  When path is a URL there is no working copy. Thus

     1: compare repository versions for URL corresponding to working copy
     2: compare working copy against repository version
     3: compare repository versions for URL
     4: nothing to do.
     5: compare working copy against text-base

   Case 4 is not as stupid as it looks, for example it may occur if
   the user specifies two dates that resolve to the same revision.  */


/** Check if paths PATH_OR_URL1 and PATH_OR_URL2 are urls and if the
 * revisions REVISION1 and REVISION2 are local. If PEG_REVISION is not
 * unspecified, ensure that at least one of the two revisions is not
 * BASE or WORKING.
 * If PATH_OR_URL1 can only be found in the repository, set *IS_REPOS1
 * to TRUE. If PATH_OR_URL2 can only be found in the repository, set
 * *IS_REPOS2 to TRUE. */
static svn_error_t *
check_paths(svn_boolean_t *is_repos1,
            svn_boolean_t *is_repos2,
            const char *path_or_url1,
            const char *path_or_url2,
            const svn_opt_revision_t *revision1,
            const svn_opt_revision_t *revision2,
            const svn_opt_revision_t *peg_revision)
{
  svn_boolean_t is_local_rev1, is_local_rev2;

  /* Verify our revision arguments in light of the paths. */
  if ((revision1->kind == svn_opt_revision_unspecified)
      || (revision2->kind == svn_opt_revision_unspecified))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("Not all required revisions are specified"));

  /* Revisions can be said to be local or remote.
   * BASE and WORKING are local revisions.  */
  is_local_rev1 =
    ((revision1->kind == svn_opt_revision_base)
     || (revision1->kind == svn_opt_revision_working));
  is_local_rev2 =
    ((revision2->kind == svn_opt_revision_base)
     || (revision2->kind == svn_opt_revision_working));

  if (peg_revision->kind != svn_opt_revision_unspecified &&
      is_local_rev1 && is_local_rev2)
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("At least one revision must be something other "
                              "than BASE or WORKING when diffing a URL"));

  /* Working copy paths with non-local revisions get turned into
     URLs.  We don't do that here, though.  We simply record that it
     needs to be done, which is information that helps us choose our
     diff helper function.  */
  *is_repos1 = ! is_local_rev1 || svn_path_is_url(path_or_url1);
  *is_repos2 = ! is_local_rev2 || svn_path_is_url(path_or_url2);

  return SVN_NO_ERROR;
}

/* Raise an error if the diff target URL does not exist at REVISION.
 * If REVISION does not equal OTHER_REVISION, mention both revisions in
 * the error message. Use RA_SESSION to contact the repository.
 * Use POOL for temporary allocations. */
static svn_error_t *
check_diff_target_exists(const char *url,
                         svn_revnum_t revision,
                         svn_revnum_t other_revision,
                         svn_ra_session_t *ra_session,
                         apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *session_url;

  SVN_ERR(svn_ra_get_session_url(ra_session, &session_url, pool));

  if (strcmp(url, session_url) != 0)
    SVN_ERR(svn_ra_reparent(ra_session, url, pool));

  SVN_ERR(svn_ra_check_path(ra_session, "", revision, &kind, pool));
  if (kind == svn_node_none)
    {
      if (revision == other_revision)
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Diff target '%s' was not found in the "
                                   "repository at revision '%ld'"),
                                 url, revision);
      else
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Diff target '%s' was not found in the "
                                   "repository at revision '%ld' or '%ld'"),
                                 url, revision, other_revision);
     }

  if (strcmp(url, session_url) != 0)
    SVN_ERR(svn_ra_reparent(ra_session, session_url, pool));

  return SVN_NO_ERROR;
}

/** Prepare a repos repos diff between PATH_OR_URL1 and
 * PATH_OR_URL2@PEG_REVISION, in the revision range REVISION1:REVISION2.
 *
 * Return the resolved URL and peg revision pairs in *URL1, *REV1 and in
 * *URL2, *REV2.
 *
 * Return suitable anchor URL and target pairs in *ANCHOR1, *TARGET1 and
 * in *ANCHOR2, *TARGET2, corresponding to *URL1 and *URL2.
 *
 * (The choice of anchor URLs here appears to be: start with *URL1, *URL2;
 * then take the parent dir on both sides, unless either of *URL1 or *URL2
 * is the repository root or the parent dir of *URL1 is unreadable.)
 *
 * Set *KIND1 and *KIND2 to the node kinds of *URL1 and *URL2, and verify
 * that at least one of the diff targets exists.
 *
 * Set *RA_SESSION to an RA session parented at the URL *ANCHOR1.
 *
 * Use client context CTX. Do all allocations in POOL. */
static svn_error_t *
diff_prepare_repos_repos(const char **url1,
                         const char **url2,
                         svn_revnum_t *rev1,
                         svn_revnum_t *rev2,
                         const char **anchor1,
                         const char **anchor2,
                         const char **target1,
                         const char **target2,
                         svn_node_kind_t *kind1,
                         svn_node_kind_t *kind2,
                         svn_ra_session_t **ra_session,
                         svn_client_ctx_t *ctx,
                         const char *path_or_url1,
                         const char *path_or_url2,
                         const svn_opt_revision_t *revision1,
                         const svn_opt_revision_t *revision2,
                         const svn_opt_revision_t *peg_revision,
                         apr_pool_t *pool)
{
  const char *local_abspath1 = NULL;
  const char *local_abspath2 = NULL;
  const char *repos_root_url;
  const char *wri_abspath = NULL;
  svn_client__pathrev_t *resolved1;
  svn_client__pathrev_t *resolved2 = NULL;
  enum svn_opt_revision_kind peg_kind = peg_revision->kind;

  if (!svn_path_is_url(path_or_url2))
    {
      SVN_ERR(svn_dirent_get_absolute(&local_abspath2, path_or_url2, pool));
      SVN_ERR(svn_wc__node_get_url(url2, ctx->wc_ctx, local_abspath2,
                                   pool, pool));
      wri_abspath = local_abspath2;
    }
  else
    *url2 = apr_pstrdup(pool, path_or_url2);

  if (!svn_path_is_url(path_or_url1))
    {
      SVN_ERR(svn_dirent_get_absolute(&local_abspath1, path_or_url1, pool));
      wri_abspath = local_abspath1;
    }

  SVN_ERR(svn_client_open_ra_session2(ra_session, *url2, wri_abspath,
                                      ctx, pool, pool));

  /* If we are performing a pegged diff, we need to find out what our
     actual URLs will be. */
  if (peg_kind != svn_opt_revision_unspecified
      || path_or_url1 == path_or_url2
      || local_abspath2)
    {
      svn_error_t *err;

      err = svn_client__resolve_rev_and_url(&resolved2,
                                            *ra_session, path_or_url2,
                                            peg_revision, revision2,
                                            ctx, pool);
      if (err)
        {
          if (err->apr_err != SVN_ERR_CLIENT_UNRELATED_RESOURCES
              && err->apr_err != SVN_ERR_FS_NOT_FOUND)
            return svn_error_trace(err);

          svn_error_clear(err);
          resolved2 = NULL;
        }
    }
  else
    resolved2 = NULL;

  if (peg_kind != svn_opt_revision_unspecified
      || path_or_url1 == path_or_url2
      || local_abspath1)
    {
      svn_error_t *err;

      err = svn_client__resolve_rev_and_url(&resolved1,
                                            *ra_session, path_or_url1,
                                            peg_revision, revision1,
                                            ctx, pool);
      if (err)
        {
          if (err->apr_err != SVN_ERR_CLIENT_UNRELATED_RESOURCES
              && err->apr_err != SVN_ERR_FS_NOT_FOUND)
            return svn_error_trace(err);

          svn_error_clear(err);
          resolved1 = NULL;
        }
    }
  else
    resolved1 = NULL;

  if (resolved1)
    {
      *url1 = resolved1->url;
      *rev1 = resolved1->rev;
    }
  else
    {
      /* It would be nice if we could just return an error when resolving a
         location fails... But in many such cases we prefer diffing against
         a non-existent location to show adds or removes (see issue #4153) */

      if (resolved2
          && (peg_kind != svn_opt_revision_unspecified
              || path_or_url1 == path_or_url2))
        *url1 = resolved2->url;
      else if (! local_abspath1)
        *url1 = path_or_url1;
      else
        SVN_ERR(svn_wc__node_get_url(url1, ctx->wc_ctx, local_abspath1,
                                     pool, pool));

      SVN_ERR(svn_client__get_revision_number(rev1, NULL, ctx->wc_ctx,
                                              local_abspath1 /* may be NULL */,
                                              *ra_session, revision1, pool));
    }

  if (resolved2)
    {
      *url2 = resolved2->url;
      *rev2 = resolved2->rev;
    }
  else
    {
      /* It would be nice if we could just return an error when resolving a
         location fails... But in many such cases we prefer diffing against
         a non-existent location to show adds or removes (see issue #4153) */

      if (resolved1
          && (peg_kind != svn_opt_revision_unspecified
              || path_or_url1 == path_or_url2))
        *url2 = resolved1->url;
      /* else keep url2 */

      SVN_ERR(svn_client__get_revision_number(rev2, NULL, ctx->wc_ctx,
                                              local_abspath2 /* may be NULL */,
                                              *ra_session, revision2, pool));
    }

  /* Resolve revision and get path kind for the second target. */
  SVN_ERR(svn_ra_reparent(*ra_session, *url2, pool));
  SVN_ERR(svn_ra_check_path(*ra_session, "", *rev2, kind2, pool));

  /* Do the same for the first target. */
  SVN_ERR(svn_ra_reparent(*ra_session, *url1, pool));
  SVN_ERR(svn_ra_check_path(*ra_session, "", *rev1, kind1, pool));

  /* Either both URLs must exist at their respective revisions,
   * or one of them may be missing from one side of the diff. */
  if (*kind1 == svn_node_none && *kind2 == svn_node_none)
    {
      if (strcmp(*url1, *url2) == 0)
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Diff target '%s' was not found in the "
                                   "repository at revisions '%ld' and '%ld'"),
                                 *url1, *rev1, *rev2);
      else
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Diff targets '%s' and '%s' were not found "
                                   "in the repository at revisions '%ld' and "
                                   "'%ld'"),
                                 *url1, *url2, *rev1, *rev2);
    }
  else if (*kind1 == svn_node_none)
    SVN_ERR(check_diff_target_exists(*url1, *rev2, *rev1, *ra_session, pool));
  else if (*kind2 == svn_node_none)
    SVN_ERR(check_diff_target_exists(*url2, *rev1, *rev2, *ra_session, pool));

  SVN_ERR(svn_ra_get_repos_root2(*ra_session, &repos_root_url, pool));

  /* Choose useful anchors and targets for our two URLs. */
  *anchor1 = *url1;
  *anchor2 = *url2;
  *target1 = "";
  *target2 = "";

  /* If none of the targets is the repository root open the parent directory
     to allow describing replacement of the target itself */
  if (strcmp(*url1, repos_root_url) != 0
      && strcmp(*url2, repos_root_url) != 0)
    {
      svn_node_kind_t ignored_kind;
      svn_error_t *err;

      svn_uri_split(anchor1, target1, *url1, pool);
      svn_uri_split(anchor2, target2, *url2, pool);

      SVN_ERR(svn_ra_reparent(*ra_session, *anchor1, pool));

      /* We might not have the necessary rights to read the root now.
         (It is ok to pass a revision here where the node doesn't exist) */
      err = svn_ra_check_path(*ra_session, "", *rev1, &ignored_kind, pool);

      if (err && (err->apr_err == SVN_ERR_RA_DAV_FORBIDDEN
                  || err->apr_err == SVN_ERR_RA_NOT_AUTHORIZED))
        {
          svn_error_clear(err);

          /* Ok, lets undo the reparent...

             We can't report replacements this way, but at least we can
             report changes on the descendants */

          *anchor1 = svn_path_url_add_component2(*anchor1, *target1, pool);
          *anchor2 = svn_path_url_add_component2(*anchor2, *target2, pool);
          *target1 = "";
          *target2 = "";

          SVN_ERR(svn_ra_reparent(*ra_session, *anchor1, pool));
        }
      else
        SVN_ERR(err);
    }

  return SVN_NO_ERROR;
}

/* A Theoretical Note From Ben, regarding do_diff().

   This function is really svn_client_diff7().  If you read the public
   API description for svn_client_diff7(), it sounds quite Grand.  It
   sounds really generalized and abstract and beautiful: that it will
   diff any two paths, be they working-copy paths or URLs, at any two
   revisions.

   Now, the *reality* is that we have exactly three 'tools' for doing
   diffing, and thus this routine is built around the use of the three
   tools.  Here they are, for clarity:

     - svn_wc_diff:  assumes both paths are the same wcpath.
                     compares wcpath@BASE vs. wcpath@WORKING

     - svn_wc_get_diff_editor:  compares some URL@REV vs. wcpath@WORKING

     - svn_client__get_diff_editor:  compares some URL1@REV1 vs. URL2@REV2

   Since Subversion 1.8 we also have a variant of svn_wc_diff called
   svn_client__arbitrary_nodes_diff, that allows handling WORKING-WORKING
   comparisons between nodes in the working copy.

   So the truth of the matter is, if the caller's arguments can't be
   pigeonholed into one of these use-cases, we currently bail with a
   friendly apology.

   Perhaps someday a brave soul will truly make svn_client_diff7()
   perfectly general.  For now, we live with the 90% case.  Certainly,
   the commandline client only calls this function in legal ways.
   When there are other users of svn_client.h, maybe this will become
   a more pressing issue.
 */

/* Return a "you can't do that" error, optionally wrapping another
   error CHILD_ERR. */
static svn_error_t *
unsupported_diff_error(svn_error_t *child_err)
{
  return svn_error_create(SVN_ERR_INCORRECT_PARAMS, child_err,
                          _("Sorry, svn_client_diff7 was called in a way "
                            "that is not yet supported"));
}

/* Perform a diff between two working-copy paths.

   PATH1 and PATH2 are both working copy paths.  REVISION1 and
   REVISION2 are their respective revisions.

   For now, require PATH1=PATH2, REVISION1='base', REVISION2='working',
   otherwise return an error.

   Anchor DIFF_PROCESSOR at the requested diff targets.

   All other options are the same as those passed to svn_client_diff7(). */
static svn_error_t *
diff_wc_wc(const char *path1,
           const svn_opt_revision_t *revision1,
           const char *path2,
           const svn_opt_revision_t *revision2,
           svn_depth_t depth,
           svn_boolean_t ignore_ancestry,
           const apr_array_header_t *changelists,
           const svn_diff_tree_processor_t *diff_processor,
           svn_client_ctx_t *ctx,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  const char *abspath1;

  SVN_ERR_ASSERT(! svn_path_is_url(path1));
  SVN_ERR_ASSERT(! svn_path_is_url(path2));

  SVN_ERR(svn_dirent_get_absolute(&abspath1, path1, scratch_pool));

  /* Currently we support only the case where path1 and path2 are the
     same path. */
  if ((strcmp(path1, path2) != 0)
      || (! ((revision1->kind == svn_opt_revision_base)
             && (revision2->kind == svn_opt_revision_working))))
    return unsupported_diff_error(
       svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                        _("A non-URL diff at this time must be either from "
                          "a path's base to the same path's working version "
                          "or between the working versions of two paths"
                          )));

  /* This will open the RA session internally if needed. */
  SVN_ERR(svn_client__textbase_sync(NULL, abspath1, TRUE, TRUE, ctx,
                                    NULL, scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__diff7(TRUE,
                        ctx->wc_ctx, abspath1, depth,
                        ignore_ancestry, changelists,
                        diff_processor,
                        ctx->cancel_func, ctx->cancel_baton,
                        result_pool, scratch_pool));

  SVN_ERR(svn_client__textbase_sync(NULL, abspath1, FALSE, TRUE, ctx,
                                    NULL, scratch_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Perform a diff between two repository paths.

   PATH_OR_URL1 and PATH_OR_URL2 may be either URLs or the working copy paths.
   REVISION1 and REVISION2 are their respective revisions.
   If PEG_REVISION is specified, PATH_OR_URL2 is the path at the peg revision,
   and the actual two paths compared are determined by following copy
   history from PATH_OR_URL2.

   If DDI is null, anchor the DIFF_PROCESSOR at the requested diff
   targets. (This case is used by diff-summarize.)

   If DDI is non-null: Set DDI->orig_path_* to the two diff target URLs as
   resolved at the given revisions; set DDI->anchor to an anchor WC path
   if either of PATH_OR_URL* is given as a WC path, else to null; set
   DDI->session_relpath to the repository-relpath of the anchor URL for
   DDI->orig_path_1. Anchor the DIFF_PROCESSOR at the anchor chosen
   for the underlying diff implementation if the target on either side
   is a file, else at the actual requested targets.

   (The choice of WC anchor implemented here for DDI->anchor appears to
   be: choose PATH_OR_URL2 (if it's a WC path) or else PATH_OR_URL1 (if
   it's a WC path); then take its parent dir unless both resolved URLs
   refer to directories.)

   (For the choice of URL anchor for DDI->session_relpath, see
   diff_prepare_repos_repos().)

   ### Bizarre anchoring. TODO: always anchor DIFF_PROCESSOR at the
       requested targets.

   All other options are the same as those passed to svn_client_diff7(). */
static svn_error_t *
diff_repos_repos(svn_client__diff_driver_info_t *ddi,
                 const char *path_or_url1,
                 const char *path_or_url2,
                 const svn_opt_revision_t *revision1,
                 const svn_opt_revision_t *revision2,
                 const svn_opt_revision_t *peg_revision,
                 svn_depth_t depth,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t text_deltas,
                 const svn_diff_tree_processor_t *diff_processor,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_ra_session_t *extra_ra_session;

  const svn_ra_reporter3_t *reporter;
  void *reporter_baton;

  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;

  const char *url1;
  const char *url2;
  svn_revnum_t rev1;
  svn_revnum_t rev2;
  svn_node_kind_t kind1;
  svn_node_kind_t kind2;
  const char *anchor1;
  const char *anchor2;
  const char *target1;
  const char *target2;
  svn_ra_session_t *ra_session;

  /* Prepare info for the repos repos diff. */
  SVN_ERR(diff_prepare_repos_repos(&url1, &url2, &rev1, &rev2,
                                   &anchor1, &anchor2, &target1, &target2,
                                   &kind1, &kind2, &ra_session,
                                   ctx, path_or_url1, path_or_url2,
                                   revision1, revision2, peg_revision,
                                   scratch_pool));

  /* Set up the repos_diff editor on BASE_PATH, if available.
     Otherwise, we just use "". */

  if (ddi)
    {
      /* Get actual URLs. */
      ddi->orig_path_1 = url1;
      ddi->orig_path_2 = url2;

      /* This should be moved to the diff writer
         - path_or_url are provided by the caller
         - target1 is available as *root_relpath
         - (kind1 != svn_node_dir || kind2 != svn_node_dir) = !*root_is_dir */

      if (!svn_path_is_url(path_or_url2))
        ddi->anchor = path_or_url2;
      else if (!svn_path_is_url(path_or_url1))
        ddi->anchor = path_or_url1;
      else
        ddi->anchor = NULL;

      if (*target1 && ddi->anchor
          && (kind1 != svn_node_dir || kind2 != svn_node_dir))
        ddi->anchor = svn_dirent_dirname(ddi->anchor, result_pool);
    }

  /* The repository can bring in a new working copy, but not delete
     everything. Luckily our new diff handler can just be reversed. */
  if (kind2 == svn_node_none)
    {
      const char *str_tmp;
      svn_revnum_t rev_tmp;

      str_tmp = url2;
      url2 = url1;
      url1 = str_tmp;

      rev_tmp = rev2;
      rev2 = rev1;
      rev1 = rev_tmp;

      str_tmp = anchor2;
      anchor2 = anchor1;
      anchor1 = str_tmp;

      str_tmp = target2;
      target2 = target1;
      target1 = str_tmp;

      diff_processor = svn_diff__tree_processor_reverse_create(diff_processor,
                                                               scratch_pool);
    }

  /* Filter the first path component using a filter processor, until we fixed
     the diff processing to handle this directly */
  if (!ddi)
    {
      diff_processor = svn_diff__tree_processor_filter_create(
                                        diff_processor, target1, scratch_pool);
    }
  else if ((kind1 != svn_node_file && kind2 != svn_node_file)
           && target1[0] != '\0')
    {
      diff_processor = svn_diff__tree_processor_filter_create(
                                        diff_processor, target1, scratch_pool);
    }

  /* Now, we open an extra RA session to the correct anchor
     location for URL1.  This is used during the editor calls to fetch file
     contents.  */
  SVN_ERR(svn_ra__dup_session(&extra_ra_session, ra_session, anchor1,
                              scratch_pool, scratch_pool));

  if (ddi)
    {
      const char *repos_root_url;
      const char *session_url;

      SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url,
                                      scratch_pool));
      SVN_ERR(svn_ra_get_session_url(ra_session, &session_url,
                                      scratch_pool));

      ddi->session_relpath = svn_uri_skip_ancestor(repos_root_url,
                                                    session_url,
                                                    result_pool);
    }

  SVN_ERR(svn_client__get_diff_editor2(
                &diff_editor, &diff_edit_baton,
                extra_ra_session, depth,
                rev1,
                text_deltas,
                diff_processor,
                ctx->cancel_func, ctx->cancel_baton,
                scratch_pool));

  /* We want to switch our txn into URL2 */
  SVN_ERR(svn_ra_do_diff3(ra_session, &reporter, &reporter_baton,
                          rev2, target1,
                          depth, ignore_ancestry, text_deltas,
                          url2, diff_editor, diff_edit_baton, scratch_pool));

  /* Drive the reporter; do the diff. */
  SVN_ERR(reporter->set_path(reporter_baton, "", rev1,
                             svn_depth_infinity,
                             FALSE, NULL,
                             scratch_pool));

  return svn_error_trace(
                  reporter->finish_report(reporter_baton, scratch_pool));
}

/* Perform a diff between a repository path and a working-copy path.

   PATH_OR_URL1 may be either a URL or a working copy path.  PATH2 is a
   working copy path.  REVISION1 is the revision of URL1. If PEG_REVISION1
   is specified, then PATH_OR_URL1 is the path in the peg revision, and the
   actual repository path to be compared is determined by following copy
   history.

   REVISION_KIND2 specifies which revision should be reported from the
   working copy (BASE or WORKING)

   If REVERSE is TRUE, the diff will be reported in reverse.

   If DDI is null, anchor the DIFF_PROCESSOR at the requested diff
   targets. (This case is used by diff-summarize.)

   If DDI is non-null: Set DDI->orig_path_* to the URLs of the two diff
   targets as resolved at the given revisions; set DDI->anchor to a WC path
   anchor for PATH2; set DDI->session_relpath to the repository-relpath of
   the URL of that same anchor WC path.

   All other options are the same as those passed to svn_client_diff7(). */
static svn_error_t *
diff_repos_wc(svn_client__diff_driver_info_t *ddi,
              const char *path_or_url1,
              const svn_opt_revision_t *revision1,
              const svn_opt_revision_t *peg_revision1,
              const char *path2,
              enum svn_opt_revision_kind revision2_kind,
              svn_boolean_t reverse,
              svn_depth_t depth,
              svn_boolean_t ignore_ancestry,
              const apr_array_header_t *changelists,
              const svn_diff_tree_processor_t *diff_processor,
              svn_client_ctx_t *ctx,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  const char *anchor, *anchor_url, *target;
  svn_ra_session_t *ra_session;
  svn_depth_t diff_depth;
  const svn_ra_reporter3_t *reporter;
  void *reporter_baton;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  svn_boolean_t rev2_is_base = (revision2_kind == svn_opt_revision_base);
  svn_boolean_t server_supports_depth;
  const char *abspath_or_url1;
  const char *abspath2;
  const char *anchor_abspath;
  svn_boolean_t is_copy;
  svn_revnum_t cf_revision;
  const char *cf_repos_relpath;
  const char *cf_repos_root_url;
  svn_depth_t cf_depth;
  const char *copy_root_abspath;
  const char *target_url;
  svn_client__pathrev_t *loc1;

  SVN_ERR_ASSERT(! svn_path_is_url(path2));

  if (!svn_path_is_url(path_or_url1))
    {
      SVN_ERR(svn_dirent_get_absolute(&abspath_or_url1, path_or_url1,
                                      scratch_pool));
    }
  else
    {
      abspath_or_url1 = path_or_url1;
    }

  SVN_ERR(svn_dirent_get_absolute(&abspath2, path2, scratch_pool));

  /* Check if our diff target is a copied node. */
  SVN_ERR(svn_wc__node_get_origin(&is_copy,
                                  &cf_revision,
                                  &cf_repos_relpath,
                                  &cf_repos_root_url,
                                  NULL, &cf_depth,
                                  &copy_root_abspath,
                                  ctx->wc_ctx, abspath2,
                                  FALSE, scratch_pool, scratch_pool));

  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &loc1,
                                            path_or_url1, abspath2,
                                            peg_revision1, revision1,
                                            ctx, scratch_pool));

  if (revision2_kind == svn_opt_revision_base || !is_copy)
    {
      /* Convert path_or_url1 to a URL to feed to do_diff. */
      SVN_ERR(svn_wc_get_actual_target2(&anchor, &target, ctx->wc_ctx, path2,
                                        scratch_pool, scratch_pool));

      /* Handle the ugly case where target is ".." */
      if (*target && !svn_path_is_single_path_component(target))
        {
          anchor = svn_dirent_join(anchor, target, scratch_pool);
          target = "";
        }

      /* Fetch the URL of the anchor directory. */
      SVN_ERR(svn_dirent_get_absolute(&anchor_abspath, anchor, scratch_pool));
      SVN_ERR(svn_wc__node_get_url(&anchor_url, ctx->wc_ctx, anchor_abspath,
                                   scratch_pool, scratch_pool));
      SVN_ERR_ASSERT(anchor_url != NULL);

      target_url = NULL;
    }
  else /* is_copy && revision2->kind != svn_opt_revision_base */
    {
#if 0
      svn_node_kind_t kind;
#endif
      /* ### Ugly hack ahead ###
       *
       * We're diffing a locally copied/moved node.
       * Describe the copy source to the reporter instead of the copy itself.
       * Doing the latter would generate a single add_directory() call to the
       * diff editor which results in an unexpected diff (the copy would
       * be shown as deleted).
       *
       * ### But if we will receive any real changes from the repositor we
       * will most likely fail to apply them as the wc diff editor assumes
       * that we have the data to which the change applies in BASE...
       */

      target_url = svn_path_url_add_component2(cf_repos_root_url,
                                               cf_repos_relpath,
                                               scratch_pool);

#if 0
      /*SVN_ERR(svn_wc_read_kind2(&kind, ctx->wc_ctx, abspath2, FALSE, FALSE,
                                scratch_pool));

      if (kind != svn_node_dir
          || strcmp(copy_root_abspath, abspath2) != 0) */
#endif
        {
          /* We are looking at a subdirectory of the repository,
             We can describe the parent directory as the anchor..

             ### This 'appears to work', but that is really dumb luck
             ### for the simple cases in the test suite */
          anchor_abspath = svn_dirent_dirname(abspath2, scratch_pool);
          anchor_url = svn_path_url_add_component2(cf_repos_root_url,
                                                   svn_relpath_dirname(
                                                            cf_repos_relpath,
                                                            scratch_pool),
                                                   scratch_pool);
          target = svn_dirent_basename(abspath2, NULL);
          anchor = svn_dirent_dirname(path2, scratch_pool);
        }
#if 0
      else
        {
          /* This code, while ok can't be enabled without causing test
           * failures. The repository will send some changes against
           * BASE for nodes that don't have BASE...
           */
          anchor_abspath = abspath2;
          anchor_url = svn_path_url_add_component2(cf_repos_root_url,
                                                   cf_repos_relpath,
                                                   scratch_pool);
          anchor = path2;
          target = "";
        }
#endif
    }

  SVN_ERR(svn_ra_reparent(ra_session, anchor_url, scratch_pool));

  if (ddi)
    {
      const char *repos_root_url;

      ddi->anchor = anchor;

      if (!reverse)
        {
          ddi->orig_path_1 = apr_pstrdup(result_pool, loc1->url);
          ddi->orig_path_2 =
            svn_path_url_add_component2(anchor_url, target, result_pool);
        }
      else
        {
          ddi->orig_path_1 =
            svn_path_url_add_component2(anchor_url, target, result_pool);
          ddi->orig_path_2 = apr_pstrdup(result_pool, loc1->url);
        }

      SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url,
                                      scratch_pool));

      ddi->session_relpath = svn_uri_skip_ancestor(repos_root_url,
                                                   anchor_url,
                                                   result_pool);
    }
  else
    {
      diff_processor = svn_diff__tree_processor_filter_create(
                         diff_processor, target, scratch_pool);
    }

  if (reverse)
    diff_processor = svn_diff__tree_processor_reverse_create(diff_processor, scratch_pool);

  SVN_ERR(svn_client__textbase_sync(NULL, abspath2, TRUE, TRUE, ctx,
                                    ra_session, scratch_pool, scratch_pool));

  /* Use the diff editor to generate the diff. */
  SVN_ERR(svn_ra_has_capability(ra_session, &server_supports_depth,
                                SVN_RA_CAPABILITY_DEPTH, scratch_pool));
  SVN_ERR(svn_wc__get_diff_editor(&diff_editor, &diff_edit_baton,
                                  ctx->wc_ctx,
                                  anchor_abspath,
                                  target,
                                  depth,
                                  ignore_ancestry,
                                  rev2_is_base,
                                  reverse,
                                  server_supports_depth,
                                  changelists,
                                  diff_processor,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  scratch_pool, scratch_pool));

  if (depth != svn_depth_infinity)
    diff_depth = depth;
  else
    diff_depth = svn_depth_unknown;

  /* Tell the RA layer we want a delta to change our txn to URL1 */
  SVN_ERR(svn_ra_do_diff3(ra_session,
                          &reporter, &reporter_baton,
                          loc1->rev,
                          target,
                          diff_depth,
                          ignore_ancestry,
                          TRUE, /* text_deltas */
                          loc1->url,
                          diff_editor, diff_edit_baton,
                          scratch_pool));

  if (is_copy && revision2_kind != svn_opt_revision_base)
    {
      /* Report the copy source. */
      if (cf_depth == svn_depth_unknown)
        cf_depth = svn_depth_infinity;

      /* Reporting the in-wc revision as r0, makes the repository send
         everything as added, which avoids using BASE for pristine information,
         which is not there (or unrelated) for a copy */

      SVN_ERR(reporter->set_path(reporter_baton, "",
                                 ignore_ancestry ? 0 : cf_revision,
                                 cf_depth, FALSE, NULL, scratch_pool));

      if (*target)
        SVN_ERR(reporter->link_path(reporter_baton, target,
                                    target_url,
                                    ignore_ancestry ? 0 : cf_revision,
                                    cf_depth, FALSE, NULL, scratch_pool));

      /* Finish the report to generate the diff. */
      SVN_ERR(reporter->finish_report(reporter_baton, scratch_pool));
    }
  else
    {
      /* Create a txn mirror of path2;  the diff editor will print
         diffs in reverse.  :-)  */
      SVN_ERR(svn_wc_crawl_revisions6(ctx->wc_ctx, abspath2,
                                      reporter, reporter_baton,
                                      FALSE, depth, TRUE,
                                      (! server_supports_depth),
                                      FALSE,
                                      ctx->cancel_func, ctx->cancel_baton,
                                      NULL, NULL, /* notification is N/A */
                                      scratch_pool));
    }

  SVN_ERR(svn_client__textbase_sync(NULL, abspath2, FALSE, TRUE, ctx,
                                    NULL, scratch_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Run diff on shelf SHELF_NAME, if it exists.
 */
static svn_error_t *
diff_shelf(const char *shelf_name,
           const char *target_abspath,
           svn_depth_t depth,
           svn_boolean_t ignore_ancestry,
           const svn_diff_tree_processor_t *diff_processor,
           svn_client_ctx_t *ctx,
           apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_client__shelf_t *shelf;
  svn_client__shelf_version_t *shelf_version;
  const char *wc_relpath;

  err = svn_client__shelf_open_existing(&shelf,
                                       shelf_name, target_abspath,
                                       ctx, scratch_pool);
  if (err && err->apr_err == SVN_ERR_ILLEGAL_TARGET)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  SVN_ERR(svn_client__shelf_version_open(&shelf_version,
                                        shelf, shelf->max_version,
                                        scratch_pool, scratch_pool));
  wc_relpath = svn_dirent_skip_ancestor(shelf->wc_root_abspath, target_abspath);
  SVN_ERR(svn_client__shelf_diff(shelf_version, wc_relpath,
                                 depth, ignore_ancestry,
                                 diff_processor, scratch_pool));
  SVN_ERR(svn_client__shelf_close(shelf, scratch_pool));

  return SVN_NO_ERROR;
}

/* Run diff on all shelves named in CHANGELISTS by a changelist name
 * of the form "svn:shelf:SHELF_NAME", if they exist.
 */
static svn_error_t *
diff_shelves(const apr_array_header_t *changelists,
             const char *target_abspath,
             svn_depth_t depth,
             svn_boolean_t ignore_ancestry,
             const svn_diff_tree_processor_t *diff_processor,
             svn_client_ctx_t *ctx,
             apr_pool_t *scratch_pool)
{
  static const char PREFIX[] = "svn:shelf:";
  static const int PREFIX_LEN = 10;
  int i;

  if (! changelists)
    return SVN_NO_ERROR;
  for (i = 0; i < changelists->nelts; i++)
    {
      const char *cl = APR_ARRAY_IDX(changelists, i, const char *);

      if (strncmp(cl, PREFIX, PREFIX_LEN) == 0)
        {
          const char *shelf_name = cl + PREFIX_LEN;

          SVN_ERR(diff_shelf(shelf_name, target_abspath,
                             depth, ignore_ancestry,
                             diff_processor, ctx, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}


/* This is basically just the guts of svn_client_diff[_summarize][_peg]6(). */
static svn_error_t *
do_diff(svn_client__diff_driver_info_t *ddi,
        const char *path_or_url1,
        const char *path_or_url2,
        const svn_opt_revision_t *revision1,
        const svn_opt_revision_t *revision2,
        const svn_opt_revision_t *peg_revision,
        svn_boolean_t no_peg_revision,
        svn_depth_t depth,
        svn_boolean_t ignore_ancestry,
        const apr_array_header_t *changelists,
        svn_boolean_t text_deltas,
        const svn_diff_tree_processor_t *diff_processor,
        svn_client_ctx_t *ctx,
        apr_pool_t *result_pool,
        apr_pool_t *scratch_pool)
{
  svn_boolean_t is_repos1;
  svn_boolean_t is_repos2;

  /* Check if paths/revisions are urls/local. */
  SVN_ERR(check_paths(&is_repos1, &is_repos2, path_or_url1, path_or_url2,
                      revision1, revision2, peg_revision));

  if (is_repos1)
    {
      if (is_repos2)
        {
          /* Ignores changelists. */
          SVN_ERR(diff_repos_repos(ddi,
                                   path_or_url1, path_or_url2,
                                   revision1, revision2,
                                   peg_revision, depth, ignore_ancestry,
                                   text_deltas,
                                   diff_processor, ctx,
                                   result_pool, scratch_pool));
        }
      else /* path_or_url2 is a working copy path */
        {
          SVN_ERR(diff_repos_wc(ddi,
                                path_or_url1, revision1,
                                no_peg_revision ? revision1
                                                : peg_revision,
                                path_or_url2, revision2->kind,
                                FALSE, depth,
                                ignore_ancestry, changelists,
                                diff_processor, ctx,
                                result_pool, scratch_pool));
        }
    }
  else /* path_or_url1 is a working copy path */
    {
      if (is_repos2)
        {
          SVN_ERR(diff_repos_wc(ddi,
                                path_or_url2, revision2,
                                no_peg_revision ? revision2
                                                : peg_revision,
                                path_or_url1,
                                revision1->kind,
                                TRUE, depth,
                                ignore_ancestry, changelists,
                                diff_processor, ctx,
                                result_pool, scratch_pool));
        }
      else /* path_or_url2 is a working copy path */
        {
          if (revision1->kind == svn_opt_revision_working
              && revision2->kind == svn_opt_revision_working)
            {
              const char *abspath1;
              const char *abspath2;

              SVN_ERR(svn_dirent_get_absolute(&abspath1, path_or_url1,
                                              scratch_pool));
              SVN_ERR(svn_dirent_get_absolute(&abspath2, path_or_url2,
                                              scratch_pool));

              if (ddi)
                {
                  svn_node_kind_t kind1, kind2;

                  SVN_ERR(svn_io_check_resolved_path(abspath1, &kind1,
                                                     scratch_pool));
                  SVN_ERR(svn_io_check_resolved_path(abspath2, &kind2,
                                                     scratch_pool));
                  if (kind1 == svn_node_dir && kind2 == svn_node_dir)
                    {
                      ddi->anchor = "";
                    }
                  else
                    {
                      ddi->anchor = svn_dirent_basename(abspath1, NULL);
                    }
                  ddi->orig_path_1 = path_or_url1;
                  ddi->orig_path_2 = path_or_url2;
                }

              /* Ignores changelists, ignore_ancestry */
              SVN_ERR(svn_client__arbitrary_nodes_diff(abspath1, abspath2,
                                                       depth,
                                                       diff_processor,
                                                       ctx, scratch_pool));
            }
          else
            {
              if (ddi)
                {
                  ddi->anchor = path_or_url1;
                  ddi->orig_path_1 = path_or_url1;
                  ddi->orig_path_2 = path_or_url2;
                }

              {
                const char *abspath1;

                SVN_ERR(svn_dirent_get_absolute(&abspath1, path_or_url1,
                                                scratch_pool));
                SVN_ERR(diff_shelves(changelists, abspath1,
                                     depth, ignore_ancestry,
                                     diff_processor, ctx, scratch_pool));
              }
              SVN_ERR(diff_wc_wc(path_or_url1, revision1,
                                 path_or_url2, revision2,
                                 depth, ignore_ancestry, changelists,
                                 diff_processor, ctx,
                                 result_pool, scratch_pool));
            }
        }
    }

  return SVN_NO_ERROR;
}

/*----------------------------------------------------------------------- */

/*** Public Interfaces. ***/

/* Display context diffs between two PATH/REVISION pairs.  Each of
   these inputs will be one of the following:

   - a repository URL at a given revision.
   - a working copy path, ignoring local mods.
   - a working copy path, including local mods.

   We can establish a matrix that shows the nine possible types of
   diffs we expect to support.


      ` .     DST ||  URL:rev   | WC:base    | WC:working |
          ` .     ||            |            |            |
      SRC     ` . ||            |            |            |
      ============++============+============+============+
       URL:rev    || (*)        | (*)        | (*)        |
                  ||            |            |            |
                  ||            |            |            |
                  ||            |            |            |
      ------------++------------+------------+------------+
       WC:base    || (*)        |                         |
                  ||            | New svn_wc_diff which   |
                  ||            | is smart enough to      |
                  ||            | handle two WC paths     |
      ------------++------------+ and their related       +
       WC:working || (*)        | text-bases and working  |
                  ||            | files.  This operation  |
                  ||            | is entirely local.      |
                  ||            |                         |
      ------------++------------+------------+------------+
      * These cases require server communication.
*/
svn_error_t *
svn_client_diff7(const apr_array_header_t *options,
                 const char *path_or_url1,
                 const svn_opt_revision_t *revision1,
                 const char *path_or_url2,
                 const svn_opt_revision_t *revision2,
                 const char *relative_to_dir,
                 svn_depth_t depth,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_added,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t show_copies_as_adds,
                 svn_boolean_t ignore_content_type,
                 svn_boolean_t ignore_properties,
                 svn_boolean_t properties_only,
                 svn_boolean_t use_git_diff_format,
                 svn_boolean_t pretty_print_mergeinfo,
                 const char *header_encoding,
                 svn_stream_t *outstream,
                 svn_stream_t *errstream,
                 const apr_array_header_t *changelists,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;
  svn_diff_tree_processor_t *diff_processor;
  svn_client__diff_driver_info_t *ddi;

  if (ignore_properties && properties_only)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Cannot ignore properties and show only "
                              "properties at the same time"));

  /* We will never do a pegged diff from here. */
  peg_revision.kind = svn_opt_revision_unspecified;

  /* --show-copies-as-adds and --git imply --notice-ancestry */
  if (show_copies_as_adds || use_git_diff_format)
    ignore_ancestry = FALSE;

  SVN_ERR(svn_client__get_diff_writer_svn(&diff_processor, &ddi,
                                          NULL /*anchor*/,
                                          NULL /*orig_path_1*/,
                                          NULL /*orig_path_2*/,
                                          options,
                                          relative_to_dir,
                                          no_diff_added,
                                          no_diff_deleted,
                                          show_copies_as_adds,
                                          ignore_content_type,
                                          ignore_properties,
                                          properties_only,
                                          use_git_diff_format,
                                          pretty_print_mergeinfo,
                                          header_encoding,
                                          outstream, errstream,
                                          ctx, pool));

  return svn_error_trace(do_diff(ddi,
                                 path_or_url1, path_or_url2,
                                 revision1, revision2,
                                 &peg_revision, TRUE /* no_peg_revision */,
                                 depth, ignore_ancestry, changelists,
                                 TRUE /* text_deltas */,
                                 diff_processor, ctx, pool, pool));
}

svn_error_t *
svn_client_diff_peg7(const apr_array_header_t *options,
                     const char *path_or_url,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     const char *relative_to_dir,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_added,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t show_copies_as_adds,
                     svn_boolean_t ignore_content_type,
                     svn_boolean_t ignore_properties,
                     svn_boolean_t properties_only,
                     svn_boolean_t use_git_diff_format,
                     svn_boolean_t pretty_print_mergeinfo,
                     const char *header_encoding,
                     svn_stream_t *outstream,
                     svn_stream_t *errstream,
                     const apr_array_header_t *changelists,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  svn_diff_tree_processor_t *diff_processor;
  svn_client__diff_driver_info_t *ddi;

  if (ignore_properties && properties_only)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Cannot ignore properties and show only "
                              "properties at the same time"));

  /* --show-copies-as-adds and --git imply --notice-ancestry */
  if (show_copies_as_adds || use_git_diff_format)
    ignore_ancestry = FALSE;

  SVN_ERR(svn_client__get_diff_writer_svn(&diff_processor, &ddi,
                                          NULL /*anchor*/,
                                          NULL /*orig_path_1*/,
                                          NULL /*orig_path_2*/,
                                          options,
                                          relative_to_dir,
                                          no_diff_added,
                                          no_diff_deleted,
                                          show_copies_as_adds,
                                          ignore_content_type,
                                          ignore_properties,
                                          properties_only,
                                          use_git_diff_format,
                                          pretty_print_mergeinfo,
                                          header_encoding,
                                          outstream, errstream,
                                          ctx, pool));

  return svn_error_trace(do_diff(ddi,
                                 path_or_url, path_or_url,
                                 start_revision, end_revision,
                                 peg_revision, FALSE /* no_peg_revision */,
                                 depth, ignore_ancestry, changelists,
                                 TRUE /* text_deltas */,
                                 diff_processor, ctx, pool, pool));
}

svn_error_t *
svn_client_diff_summarize2(const char *path_or_url1,
                           const svn_opt_revision_t *revision1,
                           const char *path_or_url2,
                           const svn_opt_revision_t *revision2,
                           svn_depth_t depth,
                           svn_boolean_t ignore_ancestry,
                           const apr_array_header_t *changelists,
                           svn_client_diff_summarize_func_t summarize_func,
                           void *summarize_baton,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  svn_diff_tree_processor_t *diff_processor;
  svn_opt_revision_t peg_revision;

  /* We will never do a pegged diff from here. */
  peg_revision.kind = svn_opt_revision_unspecified;

  SVN_ERR(svn_client__get_diff_summarize_callbacks(&diff_processor,
                     summarize_func, summarize_baton,
                     pool, pool));

  return svn_error_trace(do_diff(NULL,
                                 path_or_url1, path_or_url2,
                                 revision1, revision2,
                                 &peg_revision, TRUE /* no_peg_revision */,
                                 depth, ignore_ancestry, changelists,
                                 FALSE /* text_deltas */,
                                 diff_processor, ctx, pool, pool));
}

svn_error_t *
svn_client_diff_summarize_peg2(const char *path_or_url,
                               const svn_opt_revision_t *peg_revision,
                               const svn_opt_revision_t *start_revision,
                               const svn_opt_revision_t *end_revision,
                               svn_depth_t depth,
                               svn_boolean_t ignore_ancestry,
                               const apr_array_header_t *changelists,
                               svn_client_diff_summarize_func_t summarize_func,
                               void *summarize_baton,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool)
{
  svn_diff_tree_processor_t *diff_processor;

  SVN_ERR(svn_client__get_diff_summarize_callbacks(&diff_processor,
                     summarize_func, summarize_baton,
                     pool, pool));

  return svn_error_trace(do_diff(NULL,
                                 path_or_url, path_or_url,
                                 start_revision, end_revision,
                                 peg_revision, FALSE /* no_peg_revision */,
                                 depth, ignore_ancestry, changelists,
                                 FALSE /* text_deltas */,
                                 diff_processor, ctx, pool, pool));
}

