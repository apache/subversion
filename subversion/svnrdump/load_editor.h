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

  svn_revnum_t revision; /* The revision on which the operation is being performed */
  void *baton;           /* as returned by the commit editor */
};


/**
 * Build up a @a parser for parsing a dumpfile stream from @a stream
 * set to fire the appropriate callbacks in load editor along with a
 * @a parser_baton, using @a pool for all memory allocations.
 */
svn_error_t *
build_dumpfile_parser(const svn_repos_parse_fns2_t **parser,
                      void **parse_baton,
                      svn_stream_t *stream,
                      apr_pool_t *pool);

/**
 * Drive the load editor @a editor to perform the @a operation on
 * @a revison using @a pool for all memory allocations.
 */
svn_error_t *
drive_load_editor(struct operation *operation,
                  const svn_delta_editor_t *editor,
                  apr_pool_t *pool);

/**
 * Get a load editor @a editor along with an @a edit_baton and an
 * operation @a root_operation corresponding to open_root, all
 * allocated in @a pool. The editor will read a dumpstream from @a
 * stream and load it into @a session when driven using
 * drive_load_editor().
 */
svn_error_t *
get_load_editor(const svn_delta_editor_t **editor,
                void **edit_baton,
                struct operation **root_operation,
                svn_stream_t *stream,
                svn_ra_session_t *session,
                apr_pool_t *pool);

#endif
