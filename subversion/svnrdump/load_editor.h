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
 * Baton used by the txdelta applier.
 */
struct apply_baton
{
  const char *source;
  char *target;
  apr_pool_t *pool;
};

/**
 * General baton used by the parser functions. Contains a link to
 * apply baton.
 */
struct parse_baton
{
  struct apply_baton *ab;
};

/**
 * Baton used to represent a node; to be used by the parser
 * functions. Contains a link to the revision baton.
 */
struct node_baton
{
  const char *path;
  svn_node_kind_t kind;

  svn_revnum_t copyfrom_rev;
  const char *copyfrom_path;

  struct revision_baton *rb;
  apr_pool_t *pool;
};

/**
 * Baton used to represet a revision; used by the parser
 * functions. Contains a link to the parser baton.
 */
struct revision_baton
{
  svn_revnum_t rev;

  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  const svn_string_t *datestamp;

  apr_int32_t rev_offset;

  struct parse_baton *pb;
  apr_pool_t *pool;
};

/**
 * Used to represent an operation to perform while driving the load
 * editor.
 */
struct operation {
  enum {
    OP_OPEN,
    OP_DELETE,
    OP_ADD,
    OP_REPLACE,
    OP_PROPSET
  } operation;

  svn_revnum_t revision;        /* The revision on which the operation is being performed */
  void *baton;                  /* Represents a commit_dir_baton object */
};

/**
 * A directory baton from commit.c.
 */
struct commit_dir_baton
{
  struct commit_baton *edit_baton;
  struct commit_dir_baton *parent;
  const char *path; /* the -absolute- path to this dir in the fs */
  svn_revnum_t base_rev;        /* the revision I'm based on  */
  svn_boolean_t was_copied; /* was this directory added with history? */
  apr_pool_t *pool; /* my personal pool, in which I am allocated. */
};

/**
 * Build up a @a parser for parsing a dumpfile stream from @a stream
 * set to fire the appropriate callbacks in load editor along with a
 * @a parser_baton, using @a pool for all memory allocations.
 */
svn_error_t *
build_dumpfile_parser(const svn_repos_parse_fns2_t **parser,
                      void **parse_baton,
                      apr_pool_t *pool);

/**
 * Drive the load editor @a editor using the data in @a stream using
 * @a pool for all memory allocations.
 */
svn_error_t *
drive_load_editor(const svn_delta_editor_t *editor,
                  void *edit_baton,
                  struct operation *operation,
                  svn_stream_t *stream,
                  apr_pool_t *pool);

/**
 * Get a load editor @a editor along with an @a edit_baton and an
 * operation @a root_operation corresponding to open_root, all
 * allocated in @a pool. The load editor will commit revisions to @a
 * session when driven using drive_load_editor().
 */
svn_error_t *
get_load_editor(const svn_delta_editor_t **editor,
                void **edit_baton,
                struct operation **root_operation,
                svn_ra_session_t *session,
                apr_pool_t *pool);

#endif
