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

#include "svn_repos.h"
#include "svn_compat.h"
#include "svn_props.h"

#include "svn_private_config.h"




/*** From commit.c ***/

svn_error_t *
svn_repos_get_commit_editor4(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             svn_repos_t *repos,
                             svn_fs_txn_t *txn,
                             const char *repos_url,
                             const char *base_path,
                             const char *user,
                             const char *log_msg,
                             svn_commit_callback2_t callback,
                             void *callback_baton,
                             svn_repos_authz_callback_t authz_callback,
                             void *authz_baton,
                             apr_pool_t *pool)
{
  apr_hash_t *revprop_table = apr_hash_make(pool);
  if (user)
    apr_hash_set(revprop_table, SVN_PROP_REVISION_AUTHOR,
                 APR_HASH_KEY_STRING,
                 svn_string_create(user, pool));
  if (log_msg)
    apr_hash_set(revprop_table, SVN_PROP_REVISION_LOG,
                 APR_HASH_KEY_STRING,
                 svn_string_create(log_msg, pool));
  return svn_repos_get_commit_editor5(editor, edit_baton, repos, txn,
                                      repos_url, base_path, revprop_table,
                                      callback, callback_baton,
                                      authz_callback, authz_baton, pool);
}


svn_error_t *
svn_repos_get_commit_editor3(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             svn_repos_t *repos,
                             svn_fs_txn_t *txn,
                             const char *repos_url,
                             const char *base_path,
                             const char *user,
                             const char *log_msg,
                             svn_commit_callback_t callback,
                             void *callback_baton,
                             svn_repos_authz_callback_t authz_callback,
                             void *authz_baton,
                             apr_pool_t *pool)
{
  svn_commit_callback2_t callback2;
  void *callback2_baton;

  svn_compat_wrap_commit_callback(&callback2, &callback2_baton,
                                  callback, callback_baton,
                                  pool);

  return svn_repos_get_commit_editor4(editor, edit_baton, repos, txn,
                                      repos_url, base_path, user,
                                      log_msg, callback2,
                                      callback2_baton, authz_callback,
                                      authz_baton, pool);
}


svn_error_t *
svn_repos_get_commit_editor2(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             svn_repos_t *repos,
                             svn_fs_txn_t *txn,
                             const char *repos_url,
                             const char *base_path,
                             const char *user,
                             const char *log_msg,
                             svn_commit_callback_t callback,
                             void *callback_baton,
                             apr_pool_t *pool)
{
  return svn_repos_get_commit_editor3(editor, edit_baton, repos, txn,
                                      repos_url, base_path, user,
                                      log_msg, callback, callback_baton,
                                      NULL, NULL, pool);
}


svn_error_t *
svn_repos_get_commit_editor(const svn_delta_editor_t **editor,
                            void **edit_baton,
                            svn_repos_t *repos,
                            const char *repos_url,
                            const char *base_path,
                            const char *user,
                            const char *log_msg,
                            svn_commit_callback_t callback,
                            void *callback_baton,
                            apr_pool_t *pool)
{
  return svn_repos_get_commit_editor2(editor, edit_baton, repos, NULL,
                                      repos_url, base_path, user,
                                      log_msg, callback,
                                      callback_baton, pool);
}

/*** From repos.c ***/
svn_error_t *
svn_repos_recover2(const char *path,
                   svn_boolean_t nonblocking,
                   svn_error_t *(*start_callback)(void *baton),
                   void *start_callback_baton,
                   apr_pool_t *pool)
{
  return svn_repos_recover3(path, nonblocking,
                            start_callback, start_callback_baton,
                            NULL, NULL,
                            pool);
}

svn_error_t *
svn_repos_recover(const char *path,
                  apr_pool_t *pool)
{
  return svn_repos_recover2(path, FALSE, NULL, NULL, pool);
}

/*** From reporter.c ***/
svn_error_t *
svn_repos_begin_report(void **report_baton,
                       svn_revnum_t revnum,
                       const char *username,
                       svn_repos_t *repos,
                       const char *fs_base,
                       const char *s_operand,
                       const char *switch_path,
                       svn_boolean_t text_deltas,
                       svn_boolean_t recurse,
                       svn_boolean_t ignore_ancestry,
                       const svn_delta_editor_t *editor,
                       void *edit_baton,
                       svn_repos_authz_func_t authz_read_func,
                       void *authz_read_baton,
                       apr_pool_t *pool)
{
  return svn_repos_begin_report2(report_baton,
                                 revnum,
                                 repos,
                                 fs_base,
                                 s_operand,
                                 switch_path,
                                 text_deltas,
                                 SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                 ignore_ancestry,
                                 FALSE, /* don't send copyfrom args */
                                 editor,
                                 edit_baton,
                                 authz_read_func,
                                 authz_read_baton,
                                 pool);
}

svn_error_t *
svn_repos_set_path2(void *baton, const char *path, svn_revnum_t rev,
                    svn_boolean_t start_empty, const char *lock_token,
                    apr_pool_t *pool)
{
  return svn_repos_set_path3(baton, path, rev, svn_depth_infinity,
                             start_empty, lock_token, pool);
}

svn_error_t *
svn_repos_set_path(void *baton, const char *path, svn_revnum_t rev,
                   svn_boolean_t start_empty, apr_pool_t *pool)
{
  return svn_repos_set_path2(baton, path, rev, start_empty, NULL, pool);
}

svn_error_t *
svn_repos_link_path2(void *baton, const char *path, const char *link_path,
                     svn_revnum_t rev, svn_boolean_t start_empty,
                     const char *lock_token, apr_pool_t *pool)
{
  return svn_repos_link_path3(baton, path, link_path, rev, svn_depth_infinity,
                              start_empty, lock_token, pool);
}

svn_error_t *
svn_repos_link_path(void *baton, const char *path, const char *link_path,
                    svn_revnum_t rev, svn_boolean_t start_empty,
                    apr_pool_t *pool)
{
  return svn_repos_link_path2(baton, path, link_path, rev, start_empty,
                              NULL, pool);
}

/*** From dir-delta.c ***/
svn_error_t *
svn_repos_dir_delta(svn_fs_root_t *src_root,
                    const char *src_parent_dir,
                    const char *src_entry,
                    svn_fs_root_t *tgt_root,
                    const char *tgt_fullpath,
                    const svn_delta_editor_t *editor,
                    void *edit_baton,
                    svn_repos_authz_func_t authz_read_func,
                    void *authz_read_baton,
                    svn_boolean_t text_deltas,
                    svn_boolean_t recurse,
                    svn_boolean_t entry_props,
                    svn_boolean_t ignore_ancestry,
                    apr_pool_t *pool)
{
  return svn_repos_dir_delta2(src_root,
                              src_parent_dir,
                              src_entry,
                              tgt_root,
                              tgt_fullpath,
                              editor,
                              edit_baton,
                              authz_read_func,
                              authz_read_baton,
                              text_deltas,
                              SVN_DEPTH_INFINITY_OR_FILES(recurse),
                              entry_props,
                              ignore_ancestry,
                              pool);
}

/*** From replay.c ***/
svn_error_t *
svn_repos_replay(svn_fs_root_t *root,
                 const svn_delta_editor_t *editor,
                 void *edit_baton,
                 apr_pool_t *pool)
{
  return svn_repos_replay2(root,
                           "" /* the whole tree */,
                           SVN_INVALID_REVNUM, /* no low water mark */
                           FALSE /* no text deltas */,
                           editor, edit_baton,
                           NULL /* no authz func */,
                           NULL /* no authz baton */,
                           pool);
}

/*** From fs-wrap.c ***/
svn_error_t *
svn_repos_fs_change_rev_prop2(svn_repos_t *repos,
                              svn_revnum_t rev,
                              const char *author,
                              const char *name,
                              const svn_string_t *new_value,
                              svn_repos_authz_func_t authz_read_func,
                              void *authz_read_baton,
                              apr_pool_t *pool)
{
  return svn_repos_fs_change_rev_prop3(repos, rev, author, name, new_value,
                                       TRUE, TRUE, authz_read_func,
                                       authz_read_baton, pool);
}



svn_error_t *
svn_repos_fs_change_rev_prop(svn_repos_t *repos,
                             svn_revnum_t rev,
                             const char *author,
                             const char *name,
                             const svn_string_t *new_value,
                             apr_pool_t *pool)
{
  return svn_repos_fs_change_rev_prop2(repos, rev, author, name, new_value,
                                       NULL, NULL, pool);
}

/*** From logs.c ***/
svn_error_t *
svn_repos_get_logs3(svn_repos_t *repos,
                    const apr_array_header_t *paths,
                    svn_revnum_t start,
                    svn_revnum_t end,
                    int limit,
                    svn_boolean_t discover_changed_paths,
                    svn_boolean_t strict_node_history,
                    svn_repos_authz_func_t authz_read_func,
                    void *authz_read_baton,
                    svn_log_message_receiver_t receiver,
                    void *receiver_baton,
                    apr_pool_t *pool)
{
  svn_log_entry_receiver_t receiver2;
  void *receiver2_baton;

  svn_compat_wrap_log_receiver(&receiver2, &receiver2_baton,
                               receiver, receiver_baton,
                               pool);

  return svn_repos_get_logs4(repos, paths, start, end, limit,
                             discover_changed_paths, strict_node_history,
                             FALSE, svn_compat_log_revprops_in(pool),
                             authz_read_func, authz_read_baton,
                             receiver2, receiver2_baton,
                             pool);
}

svn_error_t *
svn_repos_get_logs2(svn_repos_t *repos,
                    const apr_array_header_t *paths,
                    svn_revnum_t start,
                    svn_revnum_t end,
                    svn_boolean_t discover_changed_paths,
                    svn_boolean_t strict_node_history,
                    svn_repos_authz_func_t authz_read_func,
                    void *authz_read_baton,
                    svn_log_message_receiver_t receiver,
                    void *receiver_baton,
                    apr_pool_t *pool)
{
  return svn_repos_get_logs3(repos, paths, start, end, 0,
                             discover_changed_paths, strict_node_history,
                             authz_read_func, authz_read_baton, receiver,
                             receiver_baton, pool);
}


svn_error_t *
svn_repos_get_logs(svn_repos_t *repos,
                   const apr_array_header_t *paths,
                   svn_revnum_t start,
                   svn_revnum_t end,
                   svn_boolean_t discover_changed_paths,
                   svn_boolean_t strict_node_history,
                   svn_log_message_receiver_t receiver,
                   void *receiver_baton,
                   apr_pool_t *pool)
{
  return svn_repos_get_logs3(repos, paths, start, end, 0,
                             discover_changed_paths, strict_node_history,
                             NULL, NULL, /* no authz stuff */
                             receiver, receiver_baton, pool);
}

/*** From rev_hunt.c ***/
svn_error_t *
svn_repos_history(svn_fs_t *fs,
                  const char *path,
                  svn_repos_history_func_t history_func,
                  void *history_baton,
                  svn_revnum_t start,
                  svn_revnum_t end,
                  svn_boolean_t cross_copies,
                  apr_pool_t *pool)
{
  return svn_repos_history2(fs, path, history_func, history_baton,
                            NULL, NULL,
                            start, end, cross_copies, pool);
}

svn_error_t *
svn_repos_get_file_revs(svn_repos_t *repos,
                        const char *path,
                        svn_revnum_t start,
                        svn_revnum_t end,
                        svn_repos_authz_func_t authz_read_func,
                        void *authz_read_baton,
                        svn_repos_file_rev_handler_t handler,
                        void *handler_baton,
                        apr_pool_t *pool)
{
  svn_file_rev_handler_t handler2;
  void *handler2_baton;

  svn_compat_wrap_file_rev_handler(&handler2, &handler2_baton, handler,
                                   handler_baton, pool);

  return svn_repos_get_file_revs2(repos, path, start, end, FALSE,
                                  authz_read_func, authz_read_baton,
                                  handler2, handler2_baton, pool);
}

/*** From dump.c ***/
svn_error_t *
svn_repos_dump_fs(svn_repos_t *repos,
                  svn_stream_t *stream,
                  svn_stream_t *feedback_stream,
                  svn_revnum_t start_rev,
                  svn_revnum_t end_rev,
                  svn_boolean_t incremental,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *pool)
{
  return svn_repos_dump_fs2(repos, stream, feedback_stream, start_rev,
                            end_rev, incremental, FALSE, cancel_func,
                            cancel_baton, pool);
}

/*** From load.c ***/
static svn_repos_parser_fns_t *
fns_from_fns2(const svn_repos_parse_fns2_t *fns2,
              apr_pool_t *pool)
{
  svn_repos_parser_fns_t *fns;

  fns = apr_palloc(pool, sizeof(*fns));
  fns->new_revision_record = fns2->new_revision_record;
  fns->uuid_record = fns2->uuid_record;
  fns->new_node_record = fns2->new_node_record;
  fns->set_revision_property = fns2->set_revision_property;
  fns->set_node_property = fns2->set_node_property;
  fns->remove_node_props = fns2->remove_node_props;
  fns->set_fulltext = fns2->set_fulltext;
  fns->close_node = fns2->close_node;
  fns->close_revision = fns2->close_revision;
  return fns;
}
static svn_repos_parse_fns2_t *
fns2_from_fns(const svn_repos_parser_fns_t *fns,
              apr_pool_t *pool)
{
  svn_repos_parse_fns2_t *fns2;

  fns2 = apr_palloc(pool, sizeof(*fns2));
  fns2->new_revision_record = fns->new_revision_record;
  fns2->uuid_record = fns->uuid_record;
  fns2->new_node_record = fns->new_node_record;
  fns2->set_revision_property = fns->set_revision_property;
  fns2->set_node_property = fns->set_node_property;
  fns2->remove_node_props = fns->remove_node_props;
  fns2->set_fulltext = fns->set_fulltext;
  fns2->close_node = fns->close_node;
  fns2->close_revision = fns->close_revision;
  fns2->delete_node_property = NULL;
  fns2->apply_textdelta = NULL;
  return fns2;
}

svn_error_t *
svn_repos_parse_dumpstream(svn_stream_t *stream,
                           const svn_repos_parser_fns_t *parse_fns,
                           void *parse_baton,
                           svn_cancel_func_t cancel_func,
                           void *cancel_baton,
                           apr_pool_t *pool)
{
  svn_repos_parse_fns2_t *fns2 = fns2_from_fns(parse_fns, pool);

  return svn_repos_parse_dumpstream2(stream, fns2, parse_baton,
                                     cancel_func, cancel_baton, pool);
}

svn_error_t *
svn_repos_load_fs(svn_repos_t *repos,
                  svn_stream_t *dumpstream,
                  svn_stream_t *feedback_stream,
                  enum svn_repos_load_uuid uuid_action,
                  const char *parent_dir,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *pool)
{
  return svn_repos_load_fs2(repos, dumpstream, feedback_stream,
                            uuid_action, parent_dir, FALSE, FALSE,
                            cancel_func, cancel_baton, pool);
}

svn_error_t *
svn_repos_get_fs_build_parser(const svn_repos_parser_fns_t **parser_callbacks,
                              void **parse_baton,
                              svn_repos_t *repos,
                              svn_boolean_t use_history,
                              enum svn_repos_load_uuid uuid_action,
                              svn_stream_t *outstream,
                              const char *parent_dir,
                              apr_pool_t *pool)
{
  const svn_repos_parse_fns2_t *fns2;

  SVN_ERR(svn_repos_get_fs_build_parser2(&fns2, parse_baton, repos,
                                         use_history, uuid_action, outstream,
                                         parent_dir, pool));

  *parser_callbacks = fns_from_fns2(fns2, pool);
  return SVN_NO_ERROR;
}
