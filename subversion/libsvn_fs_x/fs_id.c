/* fs_id.c : FSX's implementation of svn_fs_id_t
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

#include "fs_id.h"

#include "../libsvn_fs/fs-loader.h"
#include "private/svn_string_private.h"


typedef struct fs_x__id_t
{
  /* API visible part */
  svn_fs_id_t generic_id;

  /* private members */
  svn_fs_x__id_part_t node_id;
  svn_fs_x__id_part_t noderev_id;

} fs_x__id_t;



/* Accessing ID Pieces.  */

static svn_string_t *
id_unparse(const svn_fs_id_t *fs_id,
           apr_pool_t *pool)
{
  const fs_x__id_t *id = (const fs_x__id_t *)fs_id;
  svn_string_t *node_id = svn_fs_x__id_part_unparse(&id->node_id, pool);
  svn_string_t *noderev_id = svn_fs_x__id_part_unparse(&id->noderev_id, pool);

  return svn_string_createf(pool, "%s.%s", node_id->data, noderev_id->data);
}


/*** Comparing node IDs ***/

static svn_fs_node_relation_t
id_compare(const svn_fs_id_t *a,
           const svn_fs_id_t *b)
{
  const fs_x__id_t *id_a = (const fs_x__id_t *)a;
  const fs_x__id_t *id_b = (const fs_x__id_t *)b;

  /* Quick check: same IDs? */
  if (svn_fs_x__id_part_eq(&id_a->noderev_id, &id_b->noderev_id))
    return svn_fs_node_same;

  /* Items from different txns are unrelated. */
  if (   svn_fs_x__is_txn(id_a->noderev_id.change_set)
      && svn_fs_x__is_txn(id_b->noderev_id.change_set)
      && id_a->noderev_id.change_set != id_b->noderev_id.change_set)
    return svn_fs_node_unrelated;

  return svn_fs_x__id_part_eq(&id_a->node_id, &id_b->node_id)
       ? svn_fs_node_common_ancestor
       : svn_fs_node_unrelated;
}


/* Creating ID's.  */

static id_vtable_t id_vtable = {
  id_unparse,
  id_compare
};

svn_fs_id_t *
svn_fs_x__id_create(const svn_fs_x__id_part_t *node_id,
                    const svn_fs_x__id_part_t *noderev_id,
                    apr_pool_t *pool)
{
  fs_x__id_t *id;
  if (!svn_fs_x__id_part_used(noderev_id))
    return NULL;

  id = apr_pcalloc(pool, sizeof(*id));

  id->node_id = *node_id;
  id->noderev_id = *noderev_id;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;

  return (svn_fs_id_t *)id;
}
