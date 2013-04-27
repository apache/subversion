/* changes.h --- FSFS changes lists container
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

#include "svn_private_config.h"

#include "private/svn_packed_data.h"

#include "changes.h"
#include "string_table.h"

/* These flags will be used with the FLAGS field in binary_change_t.
 */

/* the change contains a text modification */
#define CHANGE_TEXT_MOD     0x00001

/* the change contains a property modification */
#define CHANGE_PROP_MOD     0x00002

/* the last part (rev_id) of node revision ID is a transaction ID */
#define CHANGE_TXN_NODE     0x00004

/* (flags & CHANGE_NODE_MASK) >> CHANGE_NODE_SHIFT extracts the node type */
#define CHANGE_NODE_SHIFT   0x00003
#define CHANGE_NODE_MASK    0x00018

/* node types according to svn_node_kind_t */
#define CHANGE_NODE_NONE    0x00000
#define CHANGE_NODE_FILE    0x00008
#define CHANGE_NODE_DIR     0x00010
#define CHANGE_NODE_UNKNOWN 0x00018

/* (flags & CHANGE_KIND_MASK) >> CHANGE_KIND_SHIFT extracts the change type */
#define CHANGE_KIND_SHIFT   0x00005
#define CHANGE_KIND_MASK    0x000E0

/* node types according to svn_fs_path_change_kind_t */
#define CHANGE_KIND_MODIFY  0x00000
#define CHANGE_KIND_ADD     0x00020
#define CHANGE_KIND_DELETE  0x00040
#define CHANGE_KIND_REPLACE 0x00060
#define CHANGE_KIND_RESET   0x00080

/* Our internal representation of a change */
typedef struct binary_change_t
{
  /* define the kind of change and what specific information is present */
  int flags;

  /* Path of the change. */
  apr_size_t path;

  /* copy-from information.
   * Not present if COPYFROM_REV is SVN_INVALID_REVNUM. */
  svn_revnum_t copyfrom_rev;
  apr_size_t copyfrom_path;

  /* Relevant parts of the node revision ID of the change.
   * Empty, if REV_ID is not "used". */
  svn_fs_fs__id_part_t node_id;
  svn_fs_fs__id_part_t copy_id;
  svn_fs_fs__id_part_t rev_id;  /* txn ID if CHANGE_TXN_NODE set in FLAGS */

} binary_change_t;

/* The actual container object.  Change lists are concatenated into CHANGES
 * and and their begins and ends are stored in OFFSETS.
 */
struct svn_fs_fs__changes_t
{
  /* The paths - either in 'builder' mode or finalized mode.
   * The respective other pointer will be NULL. */
  string_table_builder_t *builder;
  string_table_t *paths;

  /* All changes of all change lists concatenated.
   * Array elements are binary_change_t.structs (not pointer!) */
  apr_array_header_t *changes;

  /* [Offsets[index] .. Offsets[index+1]) is the range in CHANGES that
   * forms the contents of change list INDEX. */
  apr_array_header_t *offsets;
};

/* Create and return a new container object, allocated in POOL and with
 * and initial capacity of INITIAL_COUNT changes.  The PATH and BUILDER
 * members must be initialized by the caller afterwards.
 */
static svn_fs_fs__changes_t *
changes_create_body(apr_size_t initial_count,
                    apr_pool_t *pool)
{
  svn_fs_fs__changes_t *changes = apr_pcalloc(pool, sizeof(*changes));

  changes->changes = apr_array_make(pool, (int)initial_count,
                                    sizeof(binary_change_t));
  changes->offsets = apr_array_make(pool, 16, sizeof(int));
  APR_ARRAY_PUSH(changes->offsets, int) = 0;

  return changes;
}

svn_fs_fs__changes_t *
svn_fs_fs__changes_create(apr_size_t initial_count,
                          apr_pool_t *pool)
{
  svn_fs_fs__changes_t *changes = changes_create_body(initial_count, pool);
  changes->builder = svn_fs_fs__string_table_builder_create(pool);

  return changes;
}

/* Add CHANGE to the latest change list in CHANGES.
 */
static svn_error_t *
append_change(svn_fs_fs__changes_t *changes,
              change_t *change)
{
  binary_change_t binary_change = { 0 };
  svn_boolean_t is_txn_id;

  /* CHANGE must be sufficiently complete */
  SVN_ERR_ASSERT(change);
  SVN_ERR_ASSERT(change->path);

  /* define the kind of change and what specific information is present */
  is_txn_id = change->noderev_id && svn_fs_fs__id_is_txn(change->noderev_id);
  binary_change.flags = (change->text_mod ? CHANGE_TEXT_MOD : 0)
                      | (change->prop_mod ? CHANGE_PROP_MOD : 0)
                      | (is_txn_id ? CHANGE_TXN_NODE : 0)
                      | ((int)change->kind << CHANGE_KIND_SHIFT)
                      | ((int)change->node_kind << CHANGE_NODE_SHIFT);

  /* Path of the change. */
  binary_change.path
    = svn_fs_fs__string_table_builder_add(changes->builder, change->path, 0);

  /* copy-from information, if presence is indicated by FLAGS */
  if (SVN_IS_VALID_REVNUM(change->copyfrom_rev))
    {
      binary_change.copyfrom_rev = change->copyfrom_rev;
      binary_change.copyfrom_path
        = svn_fs_fs__string_table_builder_add(changes->builder,
                                              change->copyfrom_path,
                                              0);
    }
  else
    {
      binary_change.copyfrom_rev = SVN_INVALID_REVNUM;
      binary_change.copyfrom_path = 0;
    }

  /* Relevant parts of the revision ID of the change. */
  if (change->noderev_id)
    {
      binary_change.node_id = *svn_fs_fs__id_node_id(change->noderev_id);
      binary_change.copy_id = *svn_fs_fs__id_copy_id(change->noderev_id);
      binary_change.rev_id = is_txn_id
                           ? *svn_fs_fs__id_txn_id(change->noderev_id)
                           : *svn_fs_fs__id_rev_item(change->noderev_id);
    }
  else
    {
      svn_fs_fs__id_txn_reset(&binary_change.rev_id);
    }
  
  APR_ARRAY_PUSH(changes->changes, binary_change_t) = binary_change;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__changes_append_list(apr_size_t *list_index,
                               svn_fs_fs__changes_t *changes,
                               apr_array_header_t *list)
{
  int i;
  
  /* CHANGES must be in 'builder' mode */
  SVN_ERR_ASSERT(changes->builder);
  SVN_ERR_ASSERT(changes->paths == NULL);

  /* simply append the list and all changes */
  for (i = 0; i < list->nelts; ++i)
    append_change(changes, APR_ARRAY_IDX(list, i, change_t *));

  /* terminate the list by storing the next changes offset */
  APR_ARRAY_PUSH(changes->offsets, int) = changes->changes->nelts;
  *list_index = (apr_size_t)(changes->offsets->nelts - 2);

  return SVN_NO_ERROR;
}

apr_size_t
svn_fs_fs__changes_estimate_size(const svn_fs_fs__changes_t *changes)
{
  /* CHANGES must be in 'builder' mode */
  if (changes->builder == NULL)
    return 0;

  /* string table code makes its own prediction,
   * changes should be < 20 bytes each,
   * some static overhead should be assumed */
  return svn_fs_fs__string_table_builder_estimate_size(changes->builder)
       + changes->changes->nelts * 20
       + 100;
}

svn_error_t *
svn_fs_fs__changes_get_list(apr_array_header_t **list,
                            const svn_fs_fs__changes_t *changes,
                            apr_size_t idx,
                            apr_pool_t *pool)
{
  int first;
  int last;
  int i;

  /* CHANGES must be in 'finalized' mode */
  SVN_ERR_ASSERT(changes->builder == NULL);
  SVN_ERR_ASSERT(changes->paths);

  /* validate index */
  if (idx + 1 >= (apr_size_t)changes->offsets->nelts)
    return svn_error_createf(SVN_ERR_FS_CONTAINER_INDEX, NULL,
                             _("Changes list index %" APR_SIZE_T_FMT
                               " exceeds container size %d"),
                             idx, changes->offsets->nelts - 1);

  /* range of changes to return */
  first = APR_ARRAY_IDX(changes->offsets, (int)idx, int);
  last = APR_ARRAY_IDX(changes->offsets, (int)idx + 1, int);

  /* construct result */
  *list = apr_array_make(pool, last - first, sizeof(change_t*));
  for (i = first; i < last; ++i)
    {
      const binary_change_t *binary_change
        = &APR_ARRAY_IDX(changes->changes, i, binary_change_t);

      /* convert BINARY_CHANGE into a standard FSFS change_t */
      change_t *change = apr_pcalloc(pool, sizeof(*change));
      change->path = svn_fs_fs__string_table_get(changes->paths,
                                                 binary_change->path,
                                                 pool);

      if (svn_fs_fs__id_txn_used(&binary_change->rev_id))
        change->noderev_id
          = (binary_change->flags & CHANGE_TXN_NODE)
          ? svn_fs_fs__id_txn_create(&binary_change->node_id,
                                     &binary_change->copy_id,
                                     &binary_change->rev_id,
                                     pool)
          : svn_fs_fs__id_rev_create(&binary_change->node_id,
                                     &binary_change->copy_id,
                                     &binary_change->rev_id,
                                     pool);

      change->kind = (svn_fs_path_change_kind_t)
        ((binary_change->flags & CHANGE_KIND_MASK) >> CHANGE_KIND_SHIFT);
      change->text_mod = (binary_change->flags & CHANGE_TEXT_MOD) != 0;
      change->prop_mod = (binary_change->flags & CHANGE_PROP_MOD) != 0;
      change->node_kind = (svn_node_kind_t)
        ((binary_change->flags & CHANGE_NODE_MASK) >> CHANGE_NODE_SHIFT);

      change->copyfrom_rev = binary_change->copyfrom_rev;
      if (SVN_IS_VALID_REVNUM(binary_change->copyfrom_rev))
        change->copyfrom_path 
          = svn_fs_fs__string_table_get(changes->paths,
                                        binary_change->copyfrom_path,
                                        pool);

      /* add it to the result */
      APR_ARRAY_PUSH(*list, change_t*) = change;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__write_changes_container(svn_stream_t *stream,
                                   const svn_fs_fs__changes_t *changes,
                                   apr_pool_t *pool)
{
  int i;

  string_table_t *paths = changes->paths
                        ? changes->paths
                        : svn_fs_fs__string_table_create(changes->builder,
                                                         pool);

  svn_packed__data_root_t *root = svn_packed__data_create_root(pool);

  /* one top-level stream for each array */
  svn_packed__int_stream_t *offsets_stream
    = svn_packed__create_int_stream(root, TRUE, FALSE);
  svn_packed__int_stream_t *changes_stream
    = svn_packed__create_int_stream(root, FALSE, FALSE);

  /* structure the CHANGES_STREAM such we can extract much of the redundancy
   * from the binary_change_t structs */
  svn_packed__create_int_substream(changes_stream, TRUE, FALSE);
  svn_packed__create_int_substream(changes_stream, TRUE, FALSE);
  svn_packed__create_int_substream(changes_stream, TRUE, TRUE);
  svn_packed__create_int_substream(changes_stream, TRUE, FALSE);
  svn_packed__create_int_substream(changes_stream, TRUE, TRUE);
  svn_packed__create_int_substream(changes_stream, TRUE, FALSE);
  svn_packed__create_int_substream(changes_stream, TRUE, TRUE);
  svn_packed__create_int_substream(changes_stream, TRUE, FALSE);
  svn_packed__create_int_substream(changes_stream, TRUE, TRUE);
  svn_packed__create_int_substream(changes_stream, TRUE, FALSE);
  
  /* serialize offsets array */
  for (i = 0; i < changes->offsets->nelts; ++i)
    svn_packed__add_uint(offsets_stream,
                         APR_ARRAY_IDX(changes->offsets, i, int));

  /* serialize changes array */
  for (i = 0; i < changes->changes->nelts; ++i)
    {
      const binary_change_t *change
        = &APR_ARRAY_IDX(changes->changes, i, binary_change_t);

      svn_packed__add_uint(changes_stream, change->flags);
      svn_packed__add_uint(changes_stream, change->path);

      svn_packed__add_uint(changes_stream, change->copyfrom_rev);
      svn_packed__add_uint(changes_stream, change->copyfrom_path);

      svn_packed__add_uint(changes_stream, change->node_id.revision);
      svn_packed__add_uint(changes_stream, change->node_id.number);
      svn_packed__add_uint(changes_stream, change->copy_id.revision);
      svn_packed__add_uint(changes_stream, change->copy_id.number);
      svn_packed__add_uint(changes_stream, change->rev_id.revision);
      svn_packed__add_uint(changes_stream, change->rev_id.number);
    }

  /* write to disk */
  SVN_ERR(svn_fs_fs__write_string_table(stream, paths, pool));
  SVN_ERR(svn_packed__data_write(stream, root, pool));
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_changes_container(svn_fs_fs__changes_t **changes_p,
                                  svn_stream_t *stream,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  apr_size_t i;
  apr_size_t count;

  svn_fs_fs__changes_t *changes = apr_pcalloc(result_pool, sizeof(*changes));

  svn_packed__data_root_t *root;
  svn_packed__int_stream_t *offsets_stream;
  svn_packed__int_stream_t *changes_stream;

  /* write from disk */
  SVN_ERR(svn_fs_fs__read_string_table(&changes->paths, stream,
                                       result_pool, scratch_pool));

  SVN_ERR(svn_packed__data_read(&root, stream, result_pool, scratch_pool));
  offsets_stream = svn_packed__first_int_stream(root);
  changes_stream = svn_packed__next_int_stream(offsets_stream);

  /* read offsets array */
  count = svn_packed__int_count(offsets_stream);
  changes->offsets = apr_array_make(result_pool, count, sizeof(int));
  for (i = 0; i < count; ++i)
    APR_ARRAY_PUSH(changes->offsets, int)
      = (int)svn_packed__get_uint(offsets_stream);

  /* read changes array */
  count
    = svn_packed__int_count(svn_packed__first_int_substream(changes_stream));
  changes->changes
    = apr_array_make(result_pool, count, sizeof(binary_change_t));
  for (i = 0; i < count; ++i)
    {
      binary_change_t change;

      change.flags = svn_packed__get_uint(changes_stream);
      change.path = svn_packed__get_uint(changes_stream);

      change.copyfrom_rev = svn_packed__get_uint(changes_stream);
      change.copyfrom_path = svn_packed__get_uint(changes_stream);

      change.node_id.revision = svn_packed__get_uint(changes_stream);
      change.node_id.number = svn_packed__get_uint(changes_stream);
      change.copy_id.revision = svn_packed__get_uint(changes_stream);
      change.copy_id.number = svn_packed__get_uint(changes_stream);
      change.rev_id.revision = svn_packed__get_uint(changes_stream);
      change.rev_id.number = svn_packed__get_uint(changes_stream);

      APR_ARRAY_PUSH(changes->changes, binary_change_t) = change;
    }

  *changes_p = changes;
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__serialize_changes_container(void **data,
                                       apr_size_t *data_len,
                                       void *in,
                                       apr_pool_t *pool)
{
  svn_fs_fs__changes_t *changes = in;
  svn_stringbuf_t *serialized;

  /* make a guesstimate on the size of the serialized data.  Erring on the
   * low side will cause the serializer to re-alloc its buffer. */
  apr_size_t size
    = changes->changes->elt_size * changes->changes->nelts
    + changes->offsets->elt_size * changes->offsets->nelts
    + 10 * changes->changes->elt_size
    + 100;

  /* serialize array header and all its elements */
  svn_temp_serializer__context_t *context
    = svn_temp_serializer__init(changes, sizeof(*changes), size, pool);

  /* serialize sub-structures */
  svn_fs_fs__serialize_string_table(context, &changes->paths);
  svn_fs_fs__serialize_apr_array(context, &changes->changes);
  svn_fs_fs__serialize_apr_array(context, &changes->offsets);

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *data = serialized->data;
  *data_len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_changes_container(void **out,
                                         void *data,
                                         apr_size_t data_len,
                                         apr_pool_t *pool)
{
  svn_fs_fs__changes_t *changes = (svn_fs_fs__changes_t *)data;

  /* de-serialize sub-structures */
  svn_fs_fs__deserialize_string_table(changes, &changes->paths);
  svn_fs_fs__deserialize_apr_array(changes, &changes->changes, pool);
  svn_fs_fs__deserialize_apr_array(changes, &changes->offsets, pool);

  /* done */
  *out = changes;

  return SVN_NO_ERROR;
}
