/*
 * deprecated.c:  holding file for all deprecated APIs.
 *                "we can't lose 'em, but we can shun 'em!"
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

/* We define this here to remove any further warnings about the usage of
   deprecated functions in this file. */
#define SVN_DEPRECATED

#include "svn_delta.h"
#include "svn_sorts.h"


struct path_driver_2_to_3_baton_t
{
  svn_delta_path_driver_cb_func_t callback_func;
  void *callback_baton;
};

/* Convert from a newer to older callback
 */
static svn_error_t *
path_driver_2_to_3_func(void **dir_baton,
                        const svn_delta_editor_t *editor,
                        void *edit_baton,
                        void *parent_baton,
                        void *callback_baton,
                        const char *path,
                        apr_pool_t *pool)
{
  struct path_driver_2_to_3_baton_t *b = callback_baton;

  /* Just drop the 'editor' parameters */
  SVN_ERR(b->callback_func(dir_baton, parent_baton,
                           b->callback_baton,
                           path, pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_delta_path_driver2(const svn_delta_editor_t *editor,
                       void *edit_baton,
                       const apr_array_header_t *paths,
                       svn_boolean_t sort_paths,
                       svn_delta_path_driver_cb_func_t callback_func,
                       void *callback_baton,
                       apr_pool_t *pool)
{
  struct path_driver_2_to_3_baton_t b;

  b.callback_func = callback_func;
  b.callback_baton = callback_baton;
  SVN_ERR(svn_delta_path_driver3(editor, edit_baton,
                                 paths, sort_paths,
                                 path_driver_2_to_3_func, &b,
                                 pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_delta_path_driver(const svn_delta_editor_t *editor,
                      void *edit_baton,
                      svn_revnum_t revision,
                      const apr_array_header_t *paths,
                      svn_delta_path_driver_cb_func_t callback_func,
                      void *callback_baton,
                      apr_pool_t *scratch_pool)
{
  /* REVISION is dropped on the floor.  */

  return svn_error_trace(svn_delta_path_driver2(editor, edit_baton, paths,
                                                TRUE,
                                                callback_func, callback_baton,
                                                scratch_pool));
}
