/*
 *  load_editor.c: The svn_delta_editor_t editor used by svnrdump to
 *  load revisions.
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

#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_repos.h"
#include "svn_ra.h"
#include "svn_cmdline.h"

#include "load_editor.h"

static svn_error_t *
commit_callback(const svn_commit_info_t *commit_info,
                void *baton,
                apr_pool_t *pool)
{
  SVN_ERR(svn_cmdline_printf(pool, "r%ld committed by %s at %s\n",
                             commit_info->revision,
                             (commit_info->author
                              ? commit_info->author : "(no author)"),
                             commit_info->date));
  return SVN_NO_ERROR;
}

svn_error_t *
drive_load_editor(struct operation *operation,
                  const svn_delta_editor_t *editor,
                  apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

/* The load editor is a essentially a dumpfile parser connected to a
   commit editor. It can be driven using drive_load_editor(). */
svn_error_t *
get_load_editor(const svn_delta_editor_t **editor,
                void **edit_baton,
                struct operation **root_operation,
                svn_stream_t *stream,
                svn_ra_session_t *session,
                apr_pool_t *pool)
{
  const svn_repos_parse_fns2_t *parser;
  void *pb = NULL;
  const svn_delta_editor_t *de;
  struct operation *root;

  root = apr_pcalloc(pool, sizeof(*root));

  SVN_ERR(svn_ra_get_latest_revnum(session, &(root->revision), pool));
  SVN_ERR(build_dumpfile_parser(&parser, &pb, stream, pool));
  SVN_ERR(svn_ra_get_commit_editor3(session, &de, NULL, NULL,
                                    commit_callback, NULL, NULL, FALSE, pool));
  SVN_ERR(de->open_root(NULL, root->revision, pool, root->baton));

  *editor = de;
  *edit_baton = NULL;
  *root_operation = root;

  return SVN_NO_ERROR;
}
