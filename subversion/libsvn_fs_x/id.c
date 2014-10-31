/* id.c : operations on node-revision IDs
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

#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "id.h"
#include "index.h"
#include "util.h"

#include "../libsvn_fs/fs-loader.h"
#include "private/svn_temp_serializer.h"
#include "private/svn_string_private.h"


typedef struct fs_x__id_t
{
  /* API visible part */
  svn_fs_id_t generic_id;

  /* private members */
  svn_fs_x__id_part_t node_id;
  svn_fs_x__id_part_t copy_id;
  svn_fs_x__id_part_t noderev_id;

  apr_pool_t *pool; /* pool that was used to allocate this struct */
} fs_x__id_t;



svn_boolean_t
svn_fs_x__is_txn(svn_fs_x__change_set_t change_set)
{
  return change_set < SVN_FS_X__INVALID_CHANGE_SET;
}

svn_boolean_t
svn_fs_x__is_revision(svn_fs_x__change_set_t change_set)
{
  return change_set > SVN_FS_X__INVALID_CHANGE_SET;
}

svn_revnum_t
svn_fs_x__get_revnum(svn_fs_x__change_set_t change_set)
{
  return svn_fs_x__is_revision(change_set)
       ? (svn_revnum_t)change_set
       : SVN_INVALID_REVNUM;
}

apr_int64_t
svn_fs_x__get_txn_id(svn_fs_x__change_set_t change_set)
{
  return svn_fs_x__is_txn(change_set)
       ? -change_set + SVN_FS_X__INVALID_CHANGE_SET -1
       : SVN_FS_X__INVALID_TXN_ID;
}


svn_fs_x__change_set_t
svn_fs_x__change_set_by_rev(svn_revnum_t revnum)
{
  assert(revnum >= SVN_FS_X__INVALID_CHANGE_SET);
  return revnum;
}

svn_fs_x__change_set_t
svn_fs_x__change_set_by_txn(apr_int64_t txn_id)
{
  assert(txn_id >= SVN_FS_X__INVALID_CHANGE_SET);
  return -txn_id + SVN_FS_X__INVALID_CHANGE_SET -1;
}


/* Parse the NUL-terminated ID part at DATA and write the result into *PART.
 * Return TRUE if no errors were detected. */
static svn_boolean_t
part_parse(svn_fs_x__id_part_t *part,
           const char *data)
{
  part->number = svn__base36toui64(&data, data);
  switch (data[0])
    {
      /* txn number? */
      case '-': part->change_set = -svn__base36toui64(&data, data + 1);
                return TRUE;

      /* revision number? */
      case '+': part->change_set = svn__base36toui64(&data, data + 1);
                return TRUE;

      /* everything else is forbidden */
      default:  return FALSE;
    }
}

/* Write the textual representation of *PART into P and return a pointer
 * to the first position behind that string.
 */
static char *
part_unparse(char *p,
             const svn_fs_x__id_part_t *part)
{
  p += svn__ui64tobase36(p, part->number);
  if (part->change_set >= 0)
    {
      *(p++) = '+';
      p += svn__ui64tobase36(p, part->change_set);
    }
  else
    {
      *(p++) = '-';
      p += svn__ui64tobase36(p, -part->change_set);
    }

  return p;
}



/* Operations on ID parts */

svn_boolean_t
svn_fs_x__id_part_is_root(const svn_fs_x__id_part_t* part)
{
  return part->change_set == 0 && part->number == 0;
}

svn_boolean_t
svn_fs_x__id_part_eq(const svn_fs_x__id_part_t *lhs,
                     const svn_fs_x__id_part_t *rhs)
{
  return lhs->change_set == rhs->change_set && lhs->number == rhs->number;
}



/* Accessing ID Pieces.  */

const svn_fs_x__id_part_t *
svn_fs_x__id_node_id(const svn_fs_id_t *fs_id)
{
  const fs_x__id_t *id = (const fs_x__id_t *)fs_id;

  return &id->node_id;
}


const svn_fs_x__id_part_t *
svn_fs_x__id_copy_id(const svn_fs_id_t *fs_id)
{
  const fs_x__id_t *id = (const fs_x__id_t *)fs_id;

  return &id->copy_id;
}


svn_fs_x__txn_id_t
svn_fs_x__id_txn_id(const svn_fs_id_t *fs_id)
{
  const fs_x__id_t *id = (const fs_x__id_t *)fs_id;

  return svn_fs_x__get_txn_id(id->noderev_id.change_set);
}


const svn_fs_x__id_part_t *
svn_fs_x__id_noderev_id(const svn_fs_id_t *fs_id)
{
  const fs_x__id_t *id = (const fs_x__id_t *)fs_id;

  return &id->noderev_id;
}

svn_revnum_t
svn_fs_x__id_rev(const svn_fs_id_t *fs_id)
{
  const fs_x__id_t *id = (const fs_x__id_t *)fs_id;

  return svn_fs_x__get_revnum(id->noderev_id.change_set);
}


apr_uint64_t
svn_fs_x__id_item(const svn_fs_id_t *fs_id)
{
  const fs_x__id_t *id = (const fs_x__id_t *)fs_id;

  return id->noderev_id.number;
}

svn_boolean_t
svn_fs_x__id_is_txn(const svn_fs_id_t *fs_id)
{
  const fs_x__id_t *id = (const fs_x__id_t *)fs_id;

  return svn_fs_x__is_txn(id->noderev_id.change_set);
}

svn_string_t *
svn_fs_x__id_unparse(const svn_fs_id_t *fs_id,
                     apr_pool_t *pool)
{
  char string[6 * SVN_INT64_BUFFER_SIZE + 10];
  const fs_x__id_t *id = (const fs_x__id_t *)fs_id;

  char *p = part_unparse(string, &id->node_id);
  *(p++) = '.';
  p = part_unparse(p, &id->copy_id);
  *(p++) = '.';
  p = part_unparse(p, &id->noderev_id);

  return svn_string_ncreate(string, p - string, pool);
}


/*** Comparing node IDs ***/

svn_boolean_t
svn_fs_x__id_eq(const svn_fs_id_t *a,
                const svn_fs_id_t *b)
{
  const fs_x__id_t *id_a = (const fs_x__id_t *)a;
  const fs_x__id_t *id_b = (const fs_x__id_t *)b;

  if (a == b)
    return TRUE;

  return svn_fs_x__id_part_eq(&id_a->node_id, &id_b->node_id)
      && svn_fs_x__id_part_eq(&id_a->copy_id, &id_b->copy_id)
      && svn_fs_x__id_part_eq(&id_a->noderev_id, &id_b->noderev_id);
}


svn_boolean_t
svn_fs_x__id_check_related(const svn_fs_id_t *a,
                           const svn_fs_id_t *b)
{
  const fs_x__id_t *id_a = (const fs_x__id_t *)a;
  const fs_x__id_t *id_b = (const fs_x__id_t *)b;

  if (a == b)
    return TRUE;

  /* Items from different txns are unrelated. */
  if (   svn_fs_x__is_txn(id_a->noderev_id.change_set)
      && svn_fs_x__is_txn(id_b->noderev_id.change_set)
      && id_a->noderev_id.change_set != id_b->noderev_id.change_set)
    return FALSE;

  /* related if they trace back to the same node creation */
  return svn_fs_x__id_part_eq(&id_a->node_id, &id_b->node_id);
}


svn_fs_node_relation_t
svn_fs_x__id_compare(const svn_fs_id_t *a,
                     const svn_fs_id_t *b)
{
  if (svn_fs_x__id_eq(a, b))
    return svn_fs_node_same;
  return (svn_fs_x__id_check_related(a, b) ? svn_fs_node_common_ancestor
                                           : svn_fs_node_unrelated);
}

int
svn_fs_x__id_part_compare(const svn_fs_x__id_part_t *a,
                          const svn_fs_x__id_part_t *b)
{
  if (a->change_set < b->change_set)
    return -1;
  if (a->change_set > b->change_set)
    return 1;

  return a->number < b->number ? -1 : a->number == b->number ? 0 : 1;
}



/* Creating ID's.  */

static id_vtable_t id_vtable = {
  svn_fs_x__id_unparse,
  svn_fs_x__id_compare
};

svn_fs_id_t *
svn_fs_x__id_txn_create_root(svn_fs_x__txn_id_t txn_id,
                             apr_pool_t *pool)
{
  fs_x__id_t *id = apr_pcalloc(pool, sizeof(*id));

  /* node ID and copy ID are "0" */

  id->noderev_id.change_set = svn_fs_x__change_set_by_txn(txn_id);
  id->noderev_id.number = SVN_FS_X__ITEM_INDEX_ROOT_NODE;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;
  id->pool = pool;

  return (svn_fs_id_t *)id;
}

svn_fs_id_t *svn_fs_x__id_create_root(const svn_revnum_t revision,
                                      apr_pool_t *pool)
{
  fs_x__id_t *id = apr_pcalloc(pool, sizeof(*id));

  /* node ID and copy ID are "0" */

  id->noderev_id.change_set = svn_fs_x__change_set_by_rev(revision);
  id->noderev_id.number = SVN_FS_X__ITEM_INDEX_ROOT_NODE;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;
  id->pool = pool;

  return (svn_fs_id_t *)id;
}

svn_fs_id_t *
svn_fs_x__id_txn_create(const svn_fs_x__id_part_t *node_id,
                        const svn_fs_x__id_part_t *copy_id,
                        svn_fs_x__txn_id_t txn_id,
                        apr_uint64_t item,
                        apr_pool_t *pool)
{
  fs_x__id_t *id = apr_pcalloc(pool, sizeof(*id));

  id->node_id = *node_id;
  id->copy_id = *copy_id;

  id->noderev_id.change_set = svn_fs_x__change_set_by_txn(txn_id);
  id->noderev_id.number = item;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;
  id->pool = pool;

  return (svn_fs_id_t *)id;
}


svn_fs_id_t *
svn_fs_x__id_create(const svn_fs_x__id_part_t *node_id,
                    const svn_fs_x__id_part_t *copy_id,
                    const svn_fs_x__id_part_t *noderev_id,
                    apr_pool_t *pool)
{
  fs_x__id_t *id = apr_pcalloc(pool, sizeof(*id));

  id->node_id = *node_id;
  id->copy_id = *copy_id;
  id->noderev_id = *noderev_id;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;
  id->pool = pool;

  return (svn_fs_id_t *)id;
}


svn_fs_id_t *
svn_fs_x__id_copy(const svn_fs_id_t *source, apr_pool_t *pool)
{
  const fs_x__id_t *id = (const fs_x__id_t *)source;
  fs_x__id_t *new_id = apr_pmemdup(pool, id, sizeof(*new_id));

  new_id->generic_id.fsap_data = new_id;
  new_id->pool = pool;

  return (svn_fs_id_t *)new_id;
}


svn_fs_id_t *
svn_fs_x__id_parse(char *data,
                   apr_pool_t *pool)
{
  fs_x__id_t *id;
  char *str;

  /* Alloc a new svn_fs_id_t structure. */
  id = apr_pcalloc(pool, sizeof(*id));
  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;
  id->pool = pool;

  /* Now, we basically just need to "split" this data on `.'
     characters.  We will use svn_cstring_tokenize, which will put
     terminators where each of the '.'s used to be.  Then our new
     id field will reference string locations inside our duplicate
     string.*/

  /* Node Id */
  str = svn_cstring_tokenize(".", &data);
  if (str == NULL)
    return NULL;
  if (! part_parse(&id->node_id, str))
    return NULL;

  /* Copy Id */
  str = svn_cstring_tokenize(".", &data);
  if (str == NULL)
    return NULL;
  if (! part_parse(&id->copy_id, str))
    return NULL;

  /* NodeRev Id */
  str = svn_cstring_tokenize(".", &data);
  if (str == NULL)
    return NULL;

  if (! part_parse(&id->noderev_id, str))
    return NULL;

  return (svn_fs_id_t *)id;
}

/* (de-)serialization support */

/* Serialize an ID within the serialization CONTEXT.
 */
void
svn_fs_x__id_serialize(svn_temp_serializer__context_t *context,
                       const svn_fs_id_t * const *in)
{
  const fs_x__id_t *id = (const fs_x__id_t *)*in;

  /* nothing to do for NULL ids */
  if (id == NULL)
    return;

  /* serialize the id data struct itself */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)in,
                                sizeof(fs_x__id_t));
}

/* Deserialize an ID inside the BUFFER.
 */
void
svn_fs_x__id_deserialize(void *buffer,
                         svn_fs_id_t **in_out,
                         apr_pool_t *pool)
{
    fs_x__id_t *id;

  /* The id maybe all what is in the whole buffer.
   * Don't try to fixup the pointer in that case*/
  if (*in_out != buffer)
    svn_temp_deserializer__resolve(buffer, (void**)in_out);

  id = (fs_x__id_t *)*in_out;

  /* no id, no sub-structure fixup necessary */
  if (id == NULL)
    return;

  /* the stored vtable is bogus at best -> set the right one */
  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;
  id->pool = pool;
}

