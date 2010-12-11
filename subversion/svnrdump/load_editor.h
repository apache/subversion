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
  const char *root_url;
};

/**
 * Use to wrap the dir_context_t in commit.c so we can keep track of
 * depth, relpath and parent for open_directory and close_directory.
 */
struct directory_baton
{
  void *baton;
  const char *relpath;
  int depth;
  struct directory_baton *parent;
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
  const char *base_checksum;

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
  struct directory_baton *db;
  apr_pool_t *pool;
};

/**
 * Build up a dumpstream parser @a parser (and corresponding baton @a
 * parse_baton) to fire the appropriate callbacks in a commit editor
 * set to commit to session @a session. Use @a pool for all memory
 * allocations.
 */
svn_error_t *
get_dumpstream_loader(const svn_repos_parse_fns2_t **parser,
                      void **parse_baton,
                      svn_ra_session_t *session,
                      apr_pool_t *pool);

/**
 * Drive the dumpstream loader described by @a parser and @a
 * parse_baton to parse and commit the stream @a stream to the
 * location described by @a session. Use @a pool for all memory
 * allocations.  Use @a cancel_func and @a cancel_baton to check for
 * user cancellation of the operation (for timely-but-safe
 * termination).
 */
svn_error_t *
drive_dumpstream_loader(svn_stream_t *stream,
                        const svn_repos_parse_fns2_t *parser,
                        void *parse_baton,
                        svn_ra_session_t *session,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *pool);

#endif
