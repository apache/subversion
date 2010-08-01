/**
 * @copyright
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
 * @endcopyright
 *
 * @file load_editor.h
 * @brief The svn_delta_editor_t editor used by svnrdump to load
 * revisions.
 */

#ifndef LOAD_EDITOR_H_
#define LOAD_EDITOR_H_

/**
 * General baton used by the parser functions.
 */
struct parse_baton
{
  const svn_delta_editor_t *commit_editor;
  void *commit_edit_baton;
  svn_ra_session_t *session;
  const char *uuid;
  apr_pool_t *pool;
};

/**
 * Baton used to represent a node; to be used by the parser
 * functions. Contains a link to the revision baton.
 */
struct node_baton
{
  const char *path;
  svn_node_kind_t kind;
  enum svn_node_action action;

  svn_revnum_t copyfrom_rev;
  const char *copyfrom_path;

  void *file_baton;
  struct revision_baton *rb;
};

/**
 * Baton used to represet a revision; used by the parser
 * functions. Contains a link to the parser baton.
 */
struct revision_baton
{
  svn_revnum_t rev;
  apr_hash_t *revprop_table;

  const svn_string_t *datestamp;
  const svn_string_t *author;

  struct parse_baton *pb;
  apr_pool_t *pool;
  void *dir_baton;                    /* Keep track of dirs */
};

/**
 * Imported from commit.c
 * (see comment in new_node_record in load_editor.h)
 */
struct commit_edit_baton
{
  apr_pool_t *pool;

  /** Supplied when the editor is created: **/

  /* Revision properties to set for this commit. */
  apr_hash_t *revprop_table;

  /* Callback to run when the commit is done. */
  svn_commit_callback2_t commit_callback;
  void *commit_callback_baton;

  /* Callback to check authorizations on paths. */
  svn_repos_authz_callback_t authz_callback;
  void *authz_baton;

  /* The already-open svn repository to commit to. */
  svn_repos_t *repos;

  /* URL to the root of the open repository. */
  const char *repos_url;

  /* The name of the repository (here for convenience). */
  const char *repos_name;

  /* The filesystem associated with the REPOS above (here for
     convenience). */
  svn_fs_t *fs;

  /* Location in fs where the edit will begin. */
  const char *base_path;

  /* Does this set of interfaces 'own' the commit transaction? */
  svn_boolean_t txn_owner;

  /* svn transaction associated with this edit (created in
     open_root, or supplied by the public API caller). */
  svn_fs_txn_t *txn;

  /** Filled in during open_root: **/

  /* The name of the transaction. */
  const char *txn_name;

  /* The object representing the root directory of the svn txn. */
  svn_fs_root_t *txn_root;

  /** Filled in when the edit is closed: **/

  /* The new revision created by this commit. */
  svn_revnum_t *new_rev;

  /* The date (according to the repository) of this commit. */
  const char **committed_date;

  /* The author (also according to the repository) of this commit. */
  const char **committed_author;
};


/**
 * Build up a load editor @a parser for parsing a dumpfile stream from @a stream
 * set to fire the appropriate callbacks in load editor along with a
 * @a parser_baton, using @a pool for all memory allocations. The
 * @a editor/ @a edit_baton are central to the functionality of the
 *  parser.
 */
svn_error_t *
get_dumpstream_loader(const svn_repos_parse_fns2_t **parser,
                      void **parse_baton,
                      svn_ra_session_t *session,
                      apr_pool_t *pool);

/**
 * Drive the load editor @a editor using the data in @a stream using
 * @a pool for all memory allocations.
 */
svn_error_t *
drive_dumpstream_loader(svn_stream_t *stream,
                        const svn_repos_parse_fns2_t *parser,
                        void *parse_baton,
                        svn_ra_session_t *session,
                        apr_pool_t *pool);

#endif
