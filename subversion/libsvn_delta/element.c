/*
 * editor.c :  editing trees of versioned resources
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

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_string.h"
#include "svn_props.h"

#include "private/svn_element.h"
#include "svn_private_config.h"


/*
 * ===================================================================
 * Minor data types
 * ===================================================================
 */

svn_pathrev_t
svn_pathrev_dup(svn_pathrev_t p,
                apr_pool_t *result_pool)
{
  /* The object P is passed by value so we can modify it in place */
  p.relpath = apr_pstrdup(result_pool, p.relpath);
  return p;
}

svn_boolean_t
svn_pathrev_equal(svn_pathrev_t *peg_path1,
                  svn_pathrev_t *peg_path2)
{
  if (peg_path1->rev != peg_path2->rev)
    return FALSE;
  if (strcmp(peg_path1->relpath, peg_path2->relpath) != 0)
    return FALSE;

  return TRUE;
}


/*
 * ===================================================================
 * Element content
 * ===================================================================
 */

svn_element_content_t *
svn_element_content_dup(const svn_element_content_t *old,
                        apr_pool_t *result_pool)
{
  svn_element_content_t *new_content;

  if (old == NULL)
    return NULL;

  new_content = apr_pmemdup(result_pool, old, sizeof(*new_content));
  if (old->ref.relpath)
    new_content->ref = svn_pathrev_dup(old->ref, result_pool);
  if (old->props)
    new_content->props = svn_prop_hash_dup(old->props, result_pool);
  if (old->kind == svn_node_file && old->text)
    new_content->text = svn_stringbuf_dup(old->text, result_pool);
  if (old->kind == svn_node_symlink && old->target)
    new_content->target = apr_pstrdup(result_pool, old->target);
  return new_content;
}

svn_boolean_t
svn_element_content_equal(const svn_element_content_t *left,
                          const svn_element_content_t *right,
                          apr_pool_t *scratch_pool)
{
  apr_array_header_t *prop_diffs;

  /* references are not supported */
  SVN_ERR_ASSERT_NO_RETURN(! left->ref.relpath && ! right->ref.relpath);
  SVN_ERR_ASSERT_NO_RETURN(left->kind != svn_node_unknown
                           && right->kind != svn_node_unknown);

  if (left->kind != right->kind)
    {
      return FALSE;
    }

  svn_error_clear(svn_prop_diffs(&prop_diffs,
                                 left->props, right->props,
                                 scratch_pool));

  if (prop_diffs->nelts != 0)
    {
      return FALSE;
    }
  switch (left->kind)
    {
    case svn_node_dir:
      break;
    case svn_node_file:
      if (! svn_stringbuf_compare(left->text, right->text))
        {
          return FALSE;
        }
      break;
    case svn_node_symlink:
      if (strcmp(left->target, right->target) != 0)
        {
          return FALSE;
        }
      break;
    default:
      break;
    }

  return TRUE;
}

svn_element_content_t *
svn_element_content_create_ref(svn_pathrev_t ref,
                               apr_pool_t *result_pool)
{
  svn_element_content_t *new_content
    = apr_pcalloc(result_pool, sizeof(*new_content));

  new_content->kind = svn_node_unknown;
  new_content->ref = svn_pathrev_dup(ref, result_pool);
  return new_content;
}

svn_element_content_t *
svn_element_content_create_dir(apr_hash_t *props,
                               apr_pool_t *result_pool)
{
  svn_element_content_t *new_content
    = apr_pcalloc(result_pool, sizeof(*new_content));

  new_content->kind = svn_node_dir;
  new_content->props = props ? svn_prop_hash_dup(props, result_pool) : NULL;
  return new_content;
}

svn_element_content_t *
svn_element_content_create_file(apr_hash_t *props,
                                svn_stringbuf_t *text,
                                apr_pool_t *result_pool)
{
  svn_element_content_t *new_content
    = apr_pcalloc(result_pool, sizeof(*new_content));

  SVN_ERR_ASSERT_NO_RETURN(text);

  new_content->kind = svn_node_file;
  new_content->props = props ? svn_prop_hash_dup(props, result_pool) : NULL;
  new_content->text = svn_stringbuf_dup(text, result_pool);
  return new_content;
}

svn_element_content_t *
svn_element_content_create_symlink(apr_hash_t *props,
                                   const char *target,
                                   apr_pool_t *result_pool)
{
  svn_element_content_t *new_content
    = apr_pcalloc(result_pool, sizeof(*new_content));

  SVN_ERR_ASSERT_NO_RETURN(target);

  new_content->kind = svn_node_symlink;
  new_content->props = props ? svn_prop_hash_dup(props, result_pool) : NULL;
  new_content->target = apr_pstrdup(result_pool, target);
  return new_content;
}

