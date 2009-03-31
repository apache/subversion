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

/* ==================================================================== */



/*** Includes. ***/

/* We define this here to remove any further warnings about the usage of
   deprecated functions in this file. */
#define SVN_DEPRECATED

#include <string.h>
#include "svn_client.h"
#include "svn_path.h"
#include "svn_compat.h"
#include "svn_utf.h"

#include "client.h"
#include "mergeinfo.h"

#include "svn_private_config.h"




/*** Code. ***/

/*** From add.c ***/
svn_error_t *
svn_client_add3(const char *path,
                svn_boolean_t recursive,
                svn_boolean_t force,
                svn_boolean_t no_ignore,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_add4(path, SVN_DEPTH_INFINITY_OR_EMPTY(recursive),
                         force, no_ignore, FALSE, ctx,
                         pool);
}

svn_error_t *
svn_client_add2(const char *path,
                svn_boolean_t recursive,
                svn_boolean_t force,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_add3(path, recursive, force, FALSE, ctx, pool);
}

svn_error_t *
svn_client_add(const char *path,
               svn_boolean_t recursive,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  return svn_client_add3(path, recursive, FALSE, FALSE, ctx, pool);
}



svn_error_t *
svn_client_mkdir2(svn_commit_info_t **commit_info_p,
                  const apr_array_header_t *paths,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client_mkdir3(commit_info_p, paths, FALSE, NULL, ctx, pool);
}


svn_error_t *
svn_client_mkdir(svn_client_commit_info_t **commit_info_p,
                 const apr_array_header_t *paths,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  err = svn_client_mkdir2(&commit_info, paths, ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return err;
}

/*** From blame.c ***/

/* Baton for use with wrap_blame_receiver */
struct blame_receiver_wrapper_baton {
  void *baton;
  svn_client_blame_receiver_t receiver;
};

/* This implements svn_client_blame_receiver2_t */
static svn_error_t *
blame_wrapper_receiver(void *baton,
                       apr_int64_t line_no,
                       svn_revnum_t revision,
                       const char *author,
                       const char *date,
                       svn_revnum_t merged_revision,
                       const char *merged_author,
                       const char *merged_date,
                       const char *merged_path,
                       const char *line,
                       apr_pool_t *pool)
{
  struct blame_receiver_wrapper_baton *brwb = baton;

  if (brwb->receiver)
    return brwb->receiver(brwb->baton,
                          line_no, revision, author, date, line, pool);

  return SVN_NO_ERROR;
}

static void
wrap_blame_receiver(svn_client_blame_receiver2_t *receiver2,
                    void **receiver2_baton,
                    svn_client_blame_receiver_t receiver,
                    void *receiver_baton,
                    apr_pool_t *pool)
{
  struct blame_receiver_wrapper_baton *brwb = apr_palloc(pool, sizeof(*brwb));

  /* Set the user provided old format callback in the baton. */
  brwb->baton = receiver_baton;
  brwb->receiver = receiver;

  *receiver2_baton = brwb;
  *receiver2 = blame_wrapper_receiver;
}

svn_error_t *
svn_client_blame3(const char *target,
                  const svn_opt_revision_t *peg_revision,
                  const svn_opt_revision_t *start,
                  const svn_opt_revision_t *end,
                  const svn_diff_file_options_t *diff_options,
                  svn_boolean_t ignore_mime_type,
                  svn_client_blame_receiver_t receiver,
                  void *receiver_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  svn_client_blame_receiver2_t receiver2;
  void *receiver2_baton;

  wrap_blame_receiver(&receiver2, &receiver2_baton, receiver, receiver_baton,
                      pool);

  return svn_client_blame4(target, peg_revision, start, end, diff_options,
                           ignore_mime_type, FALSE, receiver2, receiver2_baton,
                           ctx, pool);
}

/* svn_client_blame3 guarantees 'no EOL chars' as part of the receiver
   LINE argument.  Older versions depend on the fact that if a CR is
   required, that CR is already part of the LINE data.

   Because of this difference, we need to trap old receivers and append
   a CR to LINE before passing it on to the actual receiver on platforms
   which want CRLF line termination.

*/

struct wrapped_receiver_baton_s
{
  svn_client_blame_receiver_t orig_receiver;
  void *orig_baton;
};

static svn_error_t *
wrapped_receiver(void *baton,
                 apr_int64_t line_no,
                 svn_revnum_t revision,
                 const char *author,
                 const char *date,
                 const char *line,
                 apr_pool_t *pool)
{
  struct wrapped_receiver_baton_s *b = baton;
  svn_stringbuf_t *expanded_line = svn_stringbuf_create(line, pool);

  svn_stringbuf_appendbytes(expanded_line, "\r", 1);

  return b->orig_receiver(b->orig_baton, line_no, revision, author,
                          date, expanded_line->data, pool);
}

static void
wrap_pre_blame3_receiver(svn_client_blame_receiver_t *receiver,
                   void **receiver_baton,
                   apr_pool_t *pool)
{
  if (strlen(APR_EOL_STR) > 1)
    {
      struct wrapped_receiver_baton_s *b = apr_palloc(pool,sizeof(*b));

      b->orig_receiver = *receiver;
      b->orig_baton = *receiver_baton;

      *receiver_baton = b;
      *receiver = wrapped_receiver;
    }
}

svn_error_t *
svn_client_blame2(const char *target,
                  const svn_opt_revision_t *peg_revision,
                  const svn_opt_revision_t *start,
                  const svn_opt_revision_t *end,
                  svn_client_blame_receiver_t receiver,
                  void *receiver_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  wrap_pre_blame3_receiver(&receiver, &receiver_baton, pool);
  return svn_client_blame3(target, peg_revision, start, end,
                           svn_diff_file_options_create(pool), FALSE,
                           receiver, receiver_baton, ctx, pool);
}
svn_error_t *
svn_client_blame(const char *target,
                 const svn_opt_revision_t *start,
                 const svn_opt_revision_t *end,
                 svn_client_blame_receiver_t receiver,
                 void *receiver_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  wrap_pre_blame3_receiver(&receiver, &receiver_baton, pool);
  return svn_client_blame2(target, end, start, end,
                           receiver, receiver_baton, ctx, pool);
}

/*** From commit.c ***/
svn_error_t *
svn_client_import2(svn_commit_info_t **commit_info_p,
                   const char *path,
                   const char *url,
                   svn_boolean_t nonrecursive,
                   svn_boolean_t no_ignore,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_import3(commit_info_p,
                            path, url,
                            SVN_DEPTH_INFINITY_OR_FILES(! nonrecursive),
                            no_ignore, FALSE, NULL, ctx, pool);
}

svn_error_t *
svn_client_import(svn_client_commit_info_t **commit_info_p,
                  const char *path,
                  const char *url,
                  svn_boolean_t nonrecursive,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  err = svn_client_import2(&commit_info,
                           path, url, nonrecursive,
                           FALSE, ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return err;
}

svn_error_t *
svn_client_commit3(svn_commit_info_t **commit_info_p,
                   const apr_array_header_t *targets,
                   svn_boolean_t recurse,
                   svn_boolean_t keep_locks,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_depth_t depth = SVN_DEPTH_INFINITY_OR_EMPTY(recurse);

  return svn_client_commit4(commit_info_p, targets, depth, keep_locks,
                            FALSE, NULL, NULL, ctx, pool);
}

svn_error_t *
svn_client_commit2(svn_client_commit_info_t **commit_info_p,
                   const apr_array_header_t *targets,
                   svn_boolean_t recurse,
                   svn_boolean_t keep_locks,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  err = svn_client_commit3(&commit_info, targets, recurse, keep_locks,
                           ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return err;
}

svn_error_t *
svn_client_commit(svn_client_commit_info_t **commit_info_p,
                  const apr_array_header_t *targets,
                  svn_boolean_t nonrecursive,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client_commit2(commit_info_p, targets,
                            ! nonrecursive,
                            TRUE,
                            ctx, pool);
}

/*** From copy.c ***/

svn_error_t *
svn_client_copy4(svn_commit_info_t **commit_info_p,
                 apr_array_header_t *sources,
                 const char *dst_path,
                 svn_boolean_t copy_as_child,
                 svn_boolean_t make_parents,
                 const apr_hash_t *revprop_table,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_client_copy5(commit_info_p, sources, dst_path, copy_as_child,
                          make_parents, FALSE, revprop_table, ctx, pool);
}

svn_error_t *
svn_client_copy3(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const svn_opt_revision_t *src_revision,
                 const char *dst_path,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  apr_array_header_t *sources = apr_array_make(pool, 1,
                                  sizeof(const svn_client_copy_source_t *));
  svn_client_copy_source_t copy_source;

  copy_source.path = src_path;
  copy_source.revision = src_revision;
  copy_source.peg_revision = src_revision;

  APR_ARRAY_PUSH(sources, const svn_client_copy_source_t *) = &copy_source;

  return svn_client_copy4(commit_info_p, sources, dst_path, FALSE, FALSE,
                          NULL, ctx, pool);
}

svn_error_t *
svn_client_copy2(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const svn_opt_revision_t *src_revision,
                 const char *dst_path,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_client_copy3(commit_info_p, src_path, src_revision,
                         dst_path, ctx, pool);

  /* If the target exists, try to copy the source as a child of the target.
     This will obviously fail if target is not a directory, but that's exactly
     what we want. */
  if (err && (err->apr_err == SVN_ERR_ENTRY_EXISTS
              || err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
    {
      const char *src_basename = svn_path_basename(src_path, pool);

      svn_error_clear(err);

      return svn_client_copy3(commit_info_p, src_path, src_revision,
                              svn_path_join(dst_path, src_basename, pool),
                              ctx, pool);
    }

  return err;
}

svn_error_t *
svn_client_copy(svn_client_commit_info_t **commit_info_p,
                const char *src_path,
                const svn_opt_revision_t *src_revision,
                const char *dst_path,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  err = svn_client_copy2(&commit_info, src_path, src_revision, dst_path,
                         ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return err;
}

svn_error_t *
svn_client_move4(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  apr_array_header_t *src_paths =
    apr_array_make(pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(src_paths, const char *) = src_path;

  return svn_client_move5(commit_info_p, src_paths, dst_path, force, FALSE,
                          FALSE, NULL, ctx, pool);
}

svn_error_t *
svn_client_move3(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_client_move4(commit_info_p, src_path, dst_path, force, ctx, pool);

  /* If the target exists, try to move the source as a child of the target.
     This will obviously fail if target is not a directory, but that's exactly
     what we want. */
  if (err && (err->apr_err == SVN_ERR_ENTRY_EXISTS
              || err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
    {
      const char *src_basename = svn_path_basename(src_path, pool);

      svn_error_clear(err);

      return svn_client_move4(commit_info_p, src_path,
                              svn_path_join(dst_path, src_basename, pool),
                              force, ctx, pool);
    }

  return err;
}

svn_error_t *
svn_client_move2(svn_client_commit_info_t **commit_info_p,
                 const char *src_path,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  err = svn_client_move3(&commit_info, src_path, dst_path, force, ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return err;
}


svn_error_t *
svn_client_move(svn_client_commit_info_t **commit_info_p,
                const char *src_path,
                const svn_opt_revision_t *src_revision,
                const char *dst_path,
                svn_boolean_t force,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  /* It doesn't make sense to specify revisions in a move. */

  /* ### todo: this check could fail wrongly.  For example,
     someone could pass in an svn_opt_revision_number that just
     happens to be the HEAD.  It's fair enough to punt then, IMHO,
     and just demand that the user not specify a revision at all;
     beats mucking up this function with RA calls and such. */
  if (src_revision->kind != svn_opt_revision_unspecified
      && src_revision->kind != svn_opt_revision_head)
    {
      return svn_error_create
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Cannot specify revisions (except HEAD) with move operations"));
    }

  return svn_client_move2(commit_info_p, src_path, dst_path, force, ctx, pool);
}

/*** From delete.c ***/
svn_error_t *
svn_client_delete2(svn_commit_info_t **commit_info_p,
                   const apr_array_header_t *paths,
                   svn_boolean_t force,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_delete3(commit_info_p, paths, force, FALSE, NULL,
                            ctx, pool);
}

svn_error_t *
svn_client_delete(svn_client_commit_info_t **commit_info_p,
                  const apr_array_header_t *paths,
                  svn_boolean_t force,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err = NULL;

  err = svn_client_delete2(&commit_info, paths, force, ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return err;
}

/*** From diff.c ***/

svn_error_t *
svn_client_diff3(const apr_array_header_t *options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t ignore_content_type,
                 const char *header_encoding,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_client_diff4(options, path1, revision1, path2,
                          revision2, NULL,
                          SVN_DEPTH_INFINITY_OR_FILES(recurse),
                          ignore_ancestry, no_diff_deleted,
                          ignore_content_type, header_encoding,
                          outfile, errfile, NULL, ctx, pool);
}

svn_error_t *
svn_client_diff2(const apr_array_header_t *options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t ignore_content_type,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_client_diff3(options, path1, revision1, path2, revision2,
                          recurse, ignore_ancestry, no_diff_deleted,
                          ignore_content_type, SVN_APR_LOCALE_CHARSET,
                          outfile, errfile, ctx, pool);
}

svn_error_t *
svn_client_diff(const apr_array_header_t *options,
                const char *path1,
                const svn_opt_revision_t *revision1,
                const char *path2,
                const svn_opt_revision_t *revision2,
                svn_boolean_t recurse,
                svn_boolean_t ignore_ancestry,
                svn_boolean_t no_diff_deleted,
                apr_file_t *outfile,
                apr_file_t *errfile,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_diff2(options, path1, revision1, path2, revision2,
                          recurse, ignore_ancestry, no_diff_deleted, FALSE,
                          outfile, errfile, ctx, pool);
}

svn_error_t *
svn_client_diff_peg3(const apr_array_header_t *options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t ignore_content_type,
                     const char *header_encoding,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client_diff_peg4(options,
                              path,
                              peg_revision,
                              start_revision,
                              end_revision,
                              NULL,
                              SVN_DEPTH_INFINITY_OR_FILES(recurse),
                              ignore_ancestry,
                              no_diff_deleted,
                              ignore_content_type,
                              header_encoding,
                              outfile,
                              errfile,
                              NULL,
                              ctx,
                              pool);
}

svn_error_t *
svn_client_diff_peg2(const apr_array_header_t *options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t ignore_content_type,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client_diff_peg3(options, path, peg_revision, start_revision,
                              end_revision,
                              SVN_DEPTH_INFINITY_OR_FILES(recurse),
                              ignore_ancestry, no_diff_deleted,
                              ignore_content_type, SVN_APR_LOCALE_CHARSET,
                              outfile, errfile, ctx, pool);
}

svn_error_t *
svn_client_diff_peg(const apr_array_header_t *options,
                    const char *path,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *start_revision,
                    const svn_opt_revision_t *end_revision,
                    svn_boolean_t recurse,
                    svn_boolean_t ignore_ancestry,
                    svn_boolean_t no_diff_deleted,
                    apr_file_t *outfile,
                    apr_file_t *errfile,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  return svn_client_diff_peg2(options, path, peg_revision,
                              start_revision, end_revision, recurse,
                              ignore_ancestry, no_diff_deleted, FALSE,
                              outfile, errfile, ctx, pool);
}

svn_error_t *
svn_client_diff_summarize(const char *path1,
                          const svn_opt_revision_t *revision1,
                          const char *path2,
                          const svn_opt_revision_t *revision2,
                          svn_boolean_t recurse,
                          svn_boolean_t ignore_ancestry,
                          svn_client_diff_summarize_func_t summarize_func,
                          void *summarize_baton,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool)
{
  return svn_client_diff_summarize2(path1, revision1, path2,
                                    revision2,
                                    SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                    ignore_ancestry, NULL, summarize_func,
                                    summarize_baton, ctx, pool);
}

svn_error_t *
svn_client_diff_summarize_peg(const char *path,
                              const svn_opt_revision_t *peg_revision,
                              const svn_opt_revision_t *start_revision,
                              const svn_opt_revision_t *end_revision,
                              svn_boolean_t recurse,
                              svn_boolean_t ignore_ancestry,
                              svn_client_diff_summarize_func_t summarize_func,
                              void *summarize_baton,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool)
{
  return svn_client_diff_summarize_peg2(path, peg_revision,
                                        start_revision, end_revision,
                                        SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                        ignore_ancestry, NULL,
                                        summarize_func, summarize_baton,
                                        ctx, pool);
}

/*** From export.c ***/
svn_error_t *
svn_client_export3(svn_revnum_t *result_rev,
                   const char *from,
                   const char *to,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t overwrite,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t recurse,
                   const char *native_eol,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_export4(result_rev, from, to, peg_revision, revision,
                            overwrite, ignore_externals,
                            SVN_DEPTH_INFINITY_OR_FILES(recurse),
                            native_eol, ctx, pool);
}

svn_error_t *
svn_client_export2(svn_revnum_t *result_rev,
                   const char *from,
                   const char *to,
                   svn_opt_revision_t *revision,
                   svn_boolean_t force,
                   const char *native_eol,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;

  peg_revision.kind = svn_opt_revision_unspecified;

  return svn_client_export3(result_rev, from, to, &peg_revision,
                            revision, force, FALSE, TRUE,
                            native_eol, ctx, pool);
}


svn_error_t *
svn_client_export(svn_revnum_t *result_rev,
                  const char *from,
                  const char *to,
                  svn_opt_revision_t *revision,
                  svn_boolean_t force,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client_export2(result_rev, from, to, revision, force, NULL, ctx,
                            pool);
}

/*** From list.c ***/
svn_error_t *
svn_client_list(const char *path_or_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_boolean_t recurse,
                apr_uint32_t dirent_fields,
                svn_boolean_t fetch_locks,
                svn_client_list_func_t list_func,
                void *baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_list2(path_or_url,
                          peg_revision,
                          revision,
                          SVN_DEPTH_INFINITY_OR_IMMEDIATES(recurse),
                          dirent_fields,
                          fetch_locks,
                          list_func,
                          baton,
                          ctx,
                          pool);
}

/* Baton used by compatibility wrapper svn_client_ls3. */
struct ls_baton {
  apr_hash_t *dirents;
  apr_hash_t *locks;
  apr_pool_t *pool;
};

/* This implements svn_client_list_func_t. */
static svn_error_t *
store_dirent(void *baton, const char *path, const svn_dirent_t *dirent,
             const svn_lock_t *lock, const char *abs_path, apr_pool_t *pool)
{
  struct ls_baton *lb = baton;

  /* The dirent is allocated in a temporary pool, so duplicate it into the
     correct pool.  Note, however, that the locks are stored in the correct
     pool already. */
  dirent = svn_dirent_dup(dirent, lb->pool);

  /* An empty path means we are called for the target of the operation.
     For compatibility, we only store the target if it is a file, and we
     store it under the basename of the URL.  Note that this makes it
     impossible to differentiate between the target being a directory with a
     child with the same basename as the target and the target being a file,
     but that's how it was implemented. */
  if (path[0] == '\0')
    {
      if (dirent->kind == svn_node_file)
        {
          const char *base_name = svn_path_basename(abs_path, lb->pool);
          apr_hash_set(lb->dirents, base_name, APR_HASH_KEY_STRING, dirent);
          if (lock)
            apr_hash_set(lb->locks, base_name, APR_HASH_KEY_STRING, lock);
        }
    }
  else
    {
      path = apr_pstrdup(lb->pool, path);
      apr_hash_set(lb->dirents, path, APR_HASH_KEY_STRING, dirent);
      if (lock)
        apr_hash_set(lb->locks, path, APR_HASH_KEY_STRING, lock);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_ls3(apr_hash_t **dirents,
               apr_hash_t **locks,
               const char *path_or_url,
               const svn_opt_revision_t *peg_revision,
               const svn_opt_revision_t *revision,
               svn_boolean_t recurse,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  struct ls_baton lb;

  *dirents = lb.dirents = apr_hash_make(pool);
  if (locks)
    *locks = lb.locks = apr_hash_make(pool);
  lb.pool = pool;

  return svn_client_list(path_or_url, peg_revision, revision, recurse,
                         SVN_DIRENT_ALL, locks != NULL,
                         store_dirent, &lb, ctx, pool);
}

svn_error_t *
svn_client_ls2(apr_hash_t **dirents,
               const char *path_or_url,
               const svn_opt_revision_t *peg_revision,
               const svn_opt_revision_t *revision,
               svn_boolean_t recurse,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{

  return svn_client_ls3(dirents, NULL, path_or_url, peg_revision,
                        revision, recurse, ctx, pool);
}


svn_error_t *
svn_client_ls(apr_hash_t **dirents,
              const char *path_or_url,
              svn_opt_revision_t *revision,
              svn_boolean_t recurse,
              svn_client_ctx_t *ctx,
              apr_pool_t *pool)
{
  return svn_client_ls2(dirents, path_or_url, revision,
                        revision, recurse, ctx, pool);
}

/*** From log.c ***/
svn_error_t *
svn_client_log4(const apr_array_header_t *targets,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_boolean_t include_merged_revisions,
                const apr_array_header_t *revprops,
                svn_log_entry_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  apr_array_header_t *revision_ranges;
  svn_opt_revision_range_t *range;

  range = apr_palloc(pool, sizeof(svn_opt_revision_range_t));
  range->start = *start;
  range->end = *end;

  revision_ranges = apr_array_make(pool, 1,
                                   sizeof(svn_opt_revision_range_t *));

  APR_ARRAY_PUSH(revision_ranges, svn_opt_revision_range_t *) = range;

  return svn_client_log5(targets, peg_revision, revision_ranges, limit,
                         discover_changed_paths, strict_node_history,
                         include_merged_revisions, revprops, receiver,
                         receiver_baton, ctx, pool);
}


svn_error_t *
svn_client_log3(const apr_array_header_t *targets,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_log_message_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_log_entry_receiver_t receiver2;
  void *receiver2_baton;

  svn_compat_wrap_log_receiver(&receiver2, &receiver2_baton,
                               receiver, receiver_baton,
                               pool);

  return svn_client_log4(targets, peg_revision, start, end, limit,
                         discover_changed_paths, strict_node_history, FALSE,
                         svn_compat_log_revprops_in(pool),
                         receiver2, receiver2_baton, ctx, pool);
}

svn_error_t *
svn_client_log2(const apr_array_header_t *targets,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_log_message_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;
  peg_revision.kind = svn_opt_revision_unspecified;
  return svn_client_log3(targets, &peg_revision, start, end, limit,
                         discover_changed_paths, strict_node_history,
                         receiver, receiver_baton, ctx, pool);
}

svn_error_t *
svn_client_log(const apr_array_header_t *targets,
               const svn_opt_revision_t *start,
               const svn_opt_revision_t *end,
               svn_boolean_t discover_changed_paths,
               svn_boolean_t strict_node_history,
               svn_log_message_receiver_t receiver,
               void *receiver_baton,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;

  err = svn_client_log2(targets, start, end, 0, discover_changed_paths,
                        strict_node_history, receiver, receiver_baton, ctx,
                        pool);

  /* Special case: If there have been no commits, we'll get an error
   * for requesting log of a revision higher than 0.  But the
   * default behavior of "svn log" is to give revisions HEAD through
   * 1, on the assumption that HEAD >= 1.
   *
   * So if we got that error for that reason, and it looks like the
   * user was just depending on the defaults (rather than explicitly
   * requesting the log for revision 1), then we don't error.  Instead
   * we just invoke the receiver manually on a hand-constructed log
   * message for revision 0.
   *
   * See also http://subversion.tigris.org/issues/show_bug.cgi?id=692.
   */
  if (err && (err->apr_err == SVN_ERR_FS_NO_SUCH_REVISION)
      && (start->kind == svn_opt_revision_head)
      && ((end->kind == svn_opt_revision_number)
          && (end->value.number == 1)))
    {

      /* We don't need to check if HEAD is 0, because that must be the case,
       * by logical deduction: The revision range specified is HEAD:1.
       * HEAD cannot not exist, so the revision to which "no such revision"
       * applies is 1. If revision 1 does not exist, then HEAD is 0.
       * Hence, we deduce the repository is empty without needing access
       * to further information. */

      svn_error_clear(err);
      err = SVN_NO_ERROR;

      /* Log receivers are free to handle revision 0 specially... But
         just in case some don't, we make up a message here. */
      SVN_ERR(receiver(receiver_baton,
                       NULL, 0, "", "", _("No commits in repository"),
                       pool));
    }

  return err;
}

/*** From merge.c ***/

svn_error_t *
svn_client_merge2(const char *source1,
                  const svn_opt_revision_t *revision1,
                  const char *source2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_boolean_t recurse,
                  svn_boolean_t ignore_ancestry,
                  svn_boolean_t force,
                  svn_boolean_t dry_run,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client_merge3(source1, revision1, source2, revision2,
                           target_wcpath,
                           SVN_DEPTH_INFINITY_OR_FILES(recurse),
                           ignore_ancestry, force, FALSE, dry_run,
                           merge_options, ctx, pool);
}

svn_error_t *
svn_client_merge(const char *source1,
                 const svn_opt_revision_t *revision1,
                 const char *source2,
                 const svn_opt_revision_t *revision2,
                 const char *target_wcpath,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t force,
                 svn_boolean_t dry_run,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_client_merge2(source1, revision1, source2, revision2,
                           target_wcpath, recurse, ignore_ancestry, force,
                           dry_run, NULL, ctx, pool);
}



svn_error_t *
svn_client_merge_peg2(const char *source,
                      const svn_opt_revision_t *revision1,
                      const svn_opt_revision_t *revision2,
                      const svn_opt_revision_t *peg_revision,
                      const char *target_wcpath,
                      svn_boolean_t recurse,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t force,
                      svn_boolean_t dry_run,
                      const apr_array_header_t *merge_options,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  svn_opt_revision_range_t range;
  apr_array_header_t *ranges_to_merge =
    apr_array_make(pool, 1, sizeof(svn_opt_revision_range_t *));

  range.start = *revision1;
  range.end = *revision2;
  APR_ARRAY_PUSH(ranges_to_merge, svn_opt_revision_range_t *) = &range;
  return svn_client_merge_peg3(source, ranges_to_merge,
                               peg_revision,
                               target_wcpath,
                               SVN_DEPTH_INFINITY_OR_FILES(recurse),
                               ignore_ancestry, force, FALSE, dry_run,
                               merge_options, ctx, pool);
}

svn_error_t *
svn_client_merge_peg(const char *source,
                     const svn_opt_revision_t *revision1,
                     const svn_opt_revision_t *revision2,
                     const svn_opt_revision_t *peg_revision,
                     const char *target_wcpath,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t force,
                     svn_boolean_t dry_run,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client_merge_peg2(source, revision1, revision2, peg_revision,
                               target_wcpath, recurse, ignore_ancestry, force,
                               dry_run, NULL, ctx, pool);
}

/*** From prop_commands.c ***/
svn_error_t *
svn_client_propset2(const char *propname,
                    const svn_string_t *propval,
                    const char *target,
                    svn_boolean_t recurse,
                    svn_boolean_t skip_checks,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  return svn_client_propset3(NULL, propname, propval, target,
                             SVN_DEPTH_INFINITY_OR_EMPTY(recurse),
                             skip_checks, SVN_INVALID_REVNUM,
                             NULL, NULL, ctx, pool);
}


svn_error_t *
svn_client_propset(const char *propname,
                   const svn_string_t *propval,
                   const char *target,
                   svn_boolean_t recurse,
                   apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;

  SVN_ERR(svn_client_create_context(&ctx, pool));

  return svn_client_propset2(propname, propval, target, recurse, FALSE,
                             ctx, pool);
}

svn_error_t *
svn_client_revprop_set(const char *propname,
                       const svn_string_t *propval,
                       const char *URL,
                       const svn_opt_revision_t *revision,
                       svn_revnum_t *set_rev,
                       svn_boolean_t force,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{
  return svn_client_revprop_set2(propname, propval, NULL, URL,
                                 revision, set_rev, force, ctx, pool);

}

svn_error_t *
svn_client_propget2(apr_hash_t **props,
                    const char *propname,
                    const char *target,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *revision,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  return svn_client_propget3(props,
                             propname,
                             target,
                             peg_revision,
                             revision,
                             NULL,
                             SVN_DEPTH_INFINITY_OR_EMPTY(recurse),
                             NULL,
                             ctx,
                             pool);
}

svn_error_t *
svn_client_propget(apr_hash_t **props,
                   const char *propname,
                   const char *target,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t recurse,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_propget2(props, propname, target, revision, revision,
                             recurse, ctx, pool);
}


/* Receiver baton used by proplist2() */
struct proplist_receiver_baton {
  apr_array_header_t *props;
  apr_pool_t *pool;
};

/* Receiver function used by proplist2(). */
static svn_error_t *
proplist_receiver_cb(void *baton,
                     const char *path,
                     apr_hash_t *prop_hash,
                     apr_pool_t *pool)
{
  struct proplist_receiver_baton *pl_baton =
    (struct proplist_receiver_baton *) baton;
  svn_client_proplist_item_t *tmp_item = apr_palloc(pool, sizeof(*tmp_item));
  svn_client_proplist_item_t *item;

  /* Because the pool passed to the receiver function is likely to be a
     temporary pool of some kind, we need to make copies of *path and
     *prop_hash in the pool provided by the baton. */
  tmp_item->node_name = svn_stringbuf_create(path, pl_baton->pool);
  tmp_item->prop_hash = prop_hash;

  item = svn_client_proplist_item_dup(tmp_item, pl_baton->pool);

  APR_ARRAY_PUSH(pl_baton->props, const svn_client_proplist_item_t *) = item;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_proplist2(apr_array_header_t **props,
                     const char *target,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_boolean_t recurse,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  struct proplist_receiver_baton pl_baton;

  *props = apr_array_make(pool, 5, sizeof(svn_client_proplist_item_t *));
  pl_baton.props = *props;
  pl_baton.pool = pool;

  return svn_client_proplist3(target, peg_revision, revision,
                              SVN_DEPTH_INFINITY_OR_EMPTY(recurse), NULL,
                              proplist_receiver_cb, &pl_baton, ctx, pool);
}


svn_error_t *
svn_client_proplist(apr_array_header_t **props,
                    const char *target,
                    const svn_opt_revision_t *revision,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  return svn_client_proplist2(props, target, revision, revision,
                              recurse, ctx, pool);
}

/*** From status.c ***/
struct status3_wrapper_baton
{
  svn_wc_status_func2_t old_func;
  void *old_baton;
};

static svn_error_t *
status3_wrapper_func(void *baton,
                     const char *path,
                     svn_wc_status2_t *status,
                     apr_pool_t *pool)
{
  struct status3_wrapper_baton *swb = baton;

  swb->old_func(swb->old_baton, path, status);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_status3(svn_revnum_t *result_rev,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_wc_status_func2_t status_func,
                   void *status_baton,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   const apr_array_header_t *changelists,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  struct status3_wrapper_baton swb = { 0 };
  swb.old_func = status_func;
  swb.old_baton = status_baton;
  return svn_client_status4(result_rev, path, revision, status3_wrapper_func,
                            &swb, depth, get_all, update, no_ignore,
                            ignore_externals, changelists, ctx, pool);

}

svn_error_t *
svn_client_status2(svn_revnum_t *result_rev,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_wc_status_func2_t status_func,
                   void *status_baton,
                   svn_boolean_t recurse,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_status3(result_rev, path, revision,
                            status_func, status_baton,
                            SVN_DEPTH_INFINITY_OR_IMMEDIATES(recurse),
                            get_all, update, no_ignore, ignore_externals, NULL,
                            ctx, pool);
}


/* Baton for old_status_func_cb; does what you think it does. */
struct old_status_func_cb_baton
{
  svn_wc_status_func_t original_func;
  void *original_baton;
};

/* Help svn_client_status() accept an old-style status func and baton,
   by wrapping them before passing along to svn_client_status2().

   This implements the 'svn_wc_status_func2_t' function type. */
static void old_status_func_cb(void *baton,
                               const char *path,
                               svn_wc_status2_t *status)
{
  struct old_status_func_cb_baton *b = baton;
  svn_wc_status_t *stat = (svn_wc_status_t *) status;

  b->original_func(b->original_baton, path, stat);
}


svn_error_t *
svn_client_status(svn_revnum_t *result_rev,
                  const char *path,
                  svn_opt_revision_t *revision,
                  svn_wc_status_func_t status_func,
                  void *status_baton,
                  svn_boolean_t recurse,
                  svn_boolean_t get_all,
                  svn_boolean_t update,
                  svn_boolean_t no_ignore,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  struct old_status_func_cb_baton *b = apr_pcalloc(pool, sizeof(*b));
  b->original_func = status_func;
  b->original_baton = status_baton;

  return svn_client_status2(result_rev, path, revision,
                            old_status_func_cb, b,
                            recurse, get_all, update, no_ignore, FALSE,
                            ctx, pool);
}

/*** From update.c ***/
svn_error_t *
svn_client_update2(apr_array_header_t **result_revs,
                   const apr_array_header_t *paths,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t recurse,
                   svn_boolean_t ignore_externals,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_update3(result_revs, paths, revision,
                            SVN_DEPTH_INFINITY_OR_FILES(recurse), FALSE,
                            ignore_externals, FALSE, ctx, pool);
}

svn_error_t *
svn_client_update(svn_revnum_t *result_rev,
                  const char *path,
                  const svn_opt_revision_t *revision,
                  svn_boolean_t recurse,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client__update_internal(result_rev, path, revision,
                                     SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                     FALSE, FALSE, FALSE, NULL,
                                     TRUE, ctx, pool);
}

/*** From switch.c ***/
svn_error_t *
svn_client_switch(svn_revnum_t *result_rev,
                  const char *path,
                  const char *switch_url,
                  const svn_opt_revision_t *revision,
                  svn_boolean_t recurse,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;
  peg_revision.kind = svn_opt_revision_unspecified;
  return svn_client__switch_internal(result_rev, path, switch_url,
                                     &peg_revision, revision, NULL,
                                     SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                     FALSE, NULL, FALSE, FALSE, ctx, pool);
}

/*** From cat.c ***/
svn_error_t *
svn_client_cat(svn_stream_t *out,
               const char *path_or_url,
               const svn_opt_revision_t *revision,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  return svn_client_cat2(out, path_or_url, revision, revision,
                         ctx, pool);
}

/*** From checkout.c ***/
svn_error_t *
svn_client_checkout3(svn_revnum_t *result_rev,
                     const char *URL,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_depth_t depth,
                     svn_boolean_t ignore_externals,
                     svn_boolean_t allow_unver_obstructions,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client__checkout_internal(result_rev, URL, path, peg_revision,
                                       revision, NULL, depth, ignore_externals,
                                       allow_unver_obstructions, NULL, ctx,
                                       pool);
}

svn_error_t *
svn_client_checkout2(svn_revnum_t *result_rev,
                     const char *URL,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_externals,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client__checkout_internal(result_rev, URL, path, peg_revision,
                                       revision, NULL,
                                       SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                       ignore_externals, FALSE, NULL, ctx,
                                       pool);
}

svn_error_t *
svn_client_checkout(svn_revnum_t *result_rev,
                    const char *URL,
                    const char *path,
                    const svn_opt_revision_t *revision,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;

  peg_revision.kind = svn_opt_revision_unspecified;

  return svn_client__checkout_internal(result_rev, URL, path, &peg_revision,
                                       revision, NULL,
                                       SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                       FALSE, FALSE, NULL, ctx, pool);
}

/*** From info.c ***/
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
  return svn_client_info2(path_or_url, peg_revision, revision,
                          receiver, receiver_baton,
                          SVN_DEPTH_INFINITY_OR_EMPTY(recurse),
                          NULL, ctx, pool);
}

/*** From resolved.c ***/
svn_error_t *
svn_client_resolved(const char *path,
                    svn_boolean_t recursive,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  svn_depth_t depth = SVN_DEPTH_INFINITY_OR_EMPTY(recursive);
  return svn_client_resolve(path, depth,
                            svn_wc_conflict_choose_merged, ctx, pool);
}
/*** From revert.c ***/
svn_error_t *
svn_client_revert(const apr_array_header_t *paths,
                  svn_boolean_t recursive,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client_revert2(paths, SVN_DEPTH_INFINITY_OR_EMPTY(recursive),
                            NULL, ctx, pool);
}
