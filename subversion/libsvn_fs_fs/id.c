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

#include <string.h>
#include <stdlib.h>

#include "id.h"
#include "../libsvn_fs/fs-loader.h"
#include "private/svn_temp_serializer.h"
#include "private/svn_string_private.h"


typedef struct fs_fs__id_t
{
  /* API visible part */
  svn_fs_id_t generic_id;

  /* private members */
  const char *node_id;
  const char *copy_id;
  const char *txn_id;
  svn_revnum_t rev;
  apr_uint64_t item;
} fs_fs__id_t;


/* Accessing ID Pieces.  */

const char *
svn_fs_fs__id_node_id(const svn_fs_id_t *fs_id)
{
  fs_fs__id_t *id = (fs_fs__id_t *)fs_id;

  return id->node_id;
}


const char *
svn_fs_fs__id_copy_id(const svn_fs_id_t *fs_id)
{
  fs_fs__id_t *id = (fs_fs__id_t *)fs_id;

  return id->copy_id;
}


const char *
svn_fs_fs__id_txn_id(const svn_fs_id_t *fs_id)
{
  fs_fs__id_t *id = (fs_fs__id_t *)fs_id;

  return id->txn_id;
}


svn_revnum_t
svn_fs_fs__id_rev(const svn_fs_id_t *fs_id)
{
  fs_fs__id_t *id = (fs_fs__id_t *)fs_id;

  return id->rev;
}


apr_uint64_t
svn_fs_fs__id_item(const svn_fs_id_t *fs_id)
{
  fs_fs__id_t *id = (fs_fs__id_t *)fs_id;

  return id->item;
}


svn_string_t *
svn_fs_fs__id_unparse(const svn_fs_id_t *fs_id,
                      apr_pool_t *pool)
{
  fs_fs__id_t *id = (fs_fs__id_t *)fs_id;

  if ((! id->txn_id))
    {
      char rev_string[SVN_INT64_BUFFER_SIZE];
      char offset_string[SVN_INT64_BUFFER_SIZE];

      svn__i64toa(rev_string, id->rev);
      svn__i64toa(offset_string, id->item);
      return svn_string_createf(pool, "%s.%s.r%s/%s",
                                id->node_id, id->copy_id,
                                rev_string, offset_string);
    }
  else
    {
      return svn_string_createf(pool, "%s.%s.t%s",
                                id->node_id, id->copy_id,
                                id->txn_id);
    }
}


/*** Comparing node IDs ***/

svn_boolean_t
svn_fs_fs__id_eq(const svn_fs_id_t *a,
                 const svn_fs_id_t *b)
{
  fs_fs__id_t *id_a = (fs_fs__id_t *)a;
  fs_fs__id_t *id_b = (fs_fs__id_t *)b;

  if (a == b)
    return TRUE;
  if (strcmp(id_a->node_id, id_b->node_id) != 0)
     return FALSE;
  if (strcmp(id_a->copy_id, id_b->copy_id) != 0)
    return FALSE;
  if ((id_a->txn_id == NULL) != (id_b->txn_id == NULL))
    return FALSE;
  if (id_a->txn_id && id_b->txn_id && strcmp(id_a->txn_id, id_b->txn_id) != 0)
    return FALSE;
  if (id_a->rev != id_b->rev)
    return FALSE;
  if (id_a->item != id_b->item)
    return FALSE;
  return TRUE;
}


svn_boolean_t
svn_fs_fs__id_check_related(const svn_fs_id_t *a,
                            const svn_fs_id_t *b)
{
  fs_fs__id_t *id_a = (fs_fs__id_t *)a;
  fs_fs__id_t *id_b = (fs_fs__id_t *)b;

  if (a == b)
    return TRUE;
  /* If both node_ids start with _ and they have differing transaction
     IDs, then it is impossible for them to be related. */
  if (id_a->node_id[0] == '_')
    {
      if (id_a->txn_id && id_b->txn_id &&
          (strcmp(id_a->txn_id, id_b->txn_id) != 0))
        return FALSE;
    }

  return (strcmp(id_a->node_id, id_b->node_id) == 0);
}


int
svn_fs_fs__id_compare(const svn_fs_id_t *a,
                      const svn_fs_id_t *b)
{
  if (svn_fs_fs__id_eq(a, b))
    return 0;
  return (svn_fs_fs__id_check_related(a, b) ? 1 : -1);
}



/* Creating ID's.  */

static id_vtable_t id_vtable = {
  svn_fs_fs__id_unparse,
  svn_fs_fs__id_compare
};

svn_fs_id_t *
svn_fs_fs__id_txn_create_root(const char *txn_id,
                              apr_pool_t *pool)
{
  fs_fs__id_t *id = apr_pcalloc(pool, sizeof(*id));

  /* node ID and copy ID are "0" */
  
  id->node_id = "0";
  id->copy_id = "0";
  id->txn_id = apr_pstrdup(pool, txn_id);
  id->rev = SVN_INVALID_REVNUM;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = &id;

  return (svn_fs_id_t *)id;
}

svn_fs_id_t *
svn_fs_fs__id_txn_create(const char *node_id,
                         const char *copy_id,
                         const char *txn_id,
                         apr_pool_t *pool)
{
  fs_fs__id_t *id = apr_pcalloc(pool, sizeof(*id));

  id->node_id = apr_pstrdup(pool, node_id);
  id->copy_id = apr_pstrdup(pool, copy_id);
  id->txn_id = apr_pstrdup(pool, txn_id);
  id->rev = SVN_INVALID_REVNUM;
  id->item = 0;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = &id;

  return (svn_fs_id_t *)id;
}


svn_fs_id_t *
svn_fs_fs__id_rev_create(const char *node_id,
                         const char *copy_id,
                         svn_revnum_t rev,
                         apr_uint64_t item,
                         apr_pool_t *pool)
{
  fs_fs__id_t *id = apr_pcalloc(pool, sizeof(*id));

  id->node_id = apr_pstrdup(pool, node_id);
  id->copy_id = apr_pstrdup(pool, copy_id);
  id->txn_id = NULL;
  id->rev = rev;
  id->item = item;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = &id;

  return (svn_fs_id_t *)id;
}


svn_fs_id_t *
svn_fs_fs__id_copy(const svn_fs_id_t *source, apr_pool_t *pool)
{
  fs_fs__id_t *id = (fs_fs__id_t *)source;
  fs_fs__id_t *new_id = apr_palloc(pool, sizeof(*new_id));

  new_id->node_id = apr_pstrdup(pool, id->node_id);
  new_id->copy_id = apr_pstrdup(pool, id->copy_id);
  new_id->txn_id = id->txn_id ? apr_pstrdup(pool, id->txn_id) : NULL;
  new_id->rev = id->rev;
  new_id->item = id->item;

  new_id->generic_id.vtable = &id_vtable;
  new_id->generic_id.fsap_data = &new_id;

  return (svn_fs_id_t *)new_id;
}


svn_fs_id_t *
svn_fs_fs__id_parse(const char *data,
                    apr_size_t len,
                    apr_pool_t *pool)
{
  fs_fs__id_t *id;
  char *data_copy, *str;

  /* Dup the ID data into POOL.  Our returned ID will have references
     into this memory. */
  data_copy = apr_pstrmemdup(pool, data, len);

  /* Alloc a new svn_fs_id_t structure. */
  id = apr_pcalloc(pool, sizeof(*id));
  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = &id;

  /* Now, we basically just need to "split" this data on `.'
     characters.  We will use svn_cstring_tokenize, which will put
     terminators where each of the '.'s used to be.  Then our new
     id field will reference string locations inside our duplicate
     string.*/

  /* Node Id */
  str = svn_cstring_tokenize(".", &data_copy);
  if (str == NULL)
    return NULL;
  id->node_id = str;

  /* Copy Id */
  str = svn_cstring_tokenize(".", &data_copy);
  if (str == NULL)
    return NULL;
  id->copy_id = str;

  /* Txn/Rev Id */
  str = svn_cstring_tokenize(".", &data_copy);
  if (str == NULL)
    return NULL;

  if (str[0] == 'r')
    {
      apr_int64_t val;
      svn_error_t *err;

      /* This is a revision type ID */
      id->txn_id = NULL;

      data_copy = str + 1;
      str = svn_cstring_tokenize("/", &data_copy);
      if (str == NULL)
        return NULL;
      id->rev = SVN_STR_TO_REV(str);

      str = svn_cstring_tokenize("/", &data_copy);
      if (str == NULL)
        return NULL;
      err = svn_cstring_atoi64(&val, str);
      if (err)
        {
          svn_error_clear(err);
          return NULL;
        }
      id->item = (apr_uint32_t)val;
    }
  else if (str[0] == 't')
    {
      /* This is a transaction type ID */
      id->txn_id = str + 1;
      id->rev = SVN_INVALID_REVNUM;
      id->item = 0;
    }
  else
    return NULL;

  return (svn_fs_id_t *)id;
}

/* (de-)serialization support */

/* Serialize an ID within the serialization CONTEXT.
 */
void
svn_fs_fs__id_serialize(svn_temp_serializer__context_t *context,
                        const struct svn_fs_id_t * const *in)
{
  const fs_fs__id_t *id = (const fs_fs__id_t *)*in;
  
  /* nothing to do for NULL ids */
  if (id == NULL)
    return;

  /* serialize the id data struct itself */
  svn_temp_serializer__push(context,
                            (const void * const *)in,
                            sizeof(fs_fs__id_t));

  /* append the referenced strings */
  svn_temp_serializer__add_string(context, &id->node_id);
  svn_temp_serializer__add_string(context, &id->copy_id);
  svn_temp_serializer__add_string(context, &id->txn_id);

  /* return to caller's nesting level */
  svn_temp_serializer__pop(context);
}

/* Deserialize an ID inside the BUFFER.
 */
void
svn_fs_fs__id_deserialize(void *buffer, svn_fs_id_t **in_out)
{
  fs_fs__id_t *id;

  /* The id maybe all what is in the whole buffer.
   * Don't try to fixup the pointer in that case*/
  if (*in_out != buffer)
    svn_temp_deserializer__resolve(buffer, (void**)in_out);

  id = (fs_fs__id_t *)*in_out;

  /* no id, no sub-structure fixup necessary */
  if (id == NULL)
    return;

  /* the stored vtable is bogus at best -> set the right one */
  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;

  /* handle sub-structures */
  svn_temp_deserializer__resolve(id, (void**)&id->node_id);
  svn_temp_deserializer__resolve(id, (void**)&id->copy_id);
  svn_temp_deserializer__resolve(id, (void**)&id->txn_id);
}

