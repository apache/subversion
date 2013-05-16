/* noderevs.h --- FSFS node revision container
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
#include "private/svn_subr_private.h"
#include "private/svn_temp_serializer.h"

#include "noderevs.h"
#include "string_table.h"
#include "temp_serializer.h"

/* These flags will be used with the FLAGS field in binary_noderev_t.
 */

/* (flags & NODEREV_KIND_MASK) extracts the noderev type */
#define NODEREV_KIND_MASK    0x00007

/* the noderev has merge info */
#define NODEREV_HAS_MINFO    0x00008

/* the noderev has copy-from-path and revision */
#define NODEREV_HAS_COPYFROM 0x00010

/* the noderev has copy-root path and revision */
#define NODEREV_HAS_COPYROOT 0x00020

/* the noderev has copy-root path and revision */
#define NODEREV_HAS_CPATH    0x00040

/* Our internal representation of an id
 * (basically, strip off the txn_id and the fs-agnostic header)
 */
typedef struct binary_id_t
{
  svn_fs_fs__id_part_t node_id;
  svn_fs_fs__id_part_t copy_id;
  svn_fs_fs__id_part_t rev_id;
} binary_id_t;

/* Our internal representation of an representation.
 * We simply omit the uniquifier, which allows us to share instances of
 * binary_representation_t and uniquify them in a shared_representation_t.
 */
typedef struct binary_representation_t
{
  /* Checksums digests for the contents produced by this representation.
     If has_sha1 is FALSE, sha1_digest is not being used. */
  svn_boolean_t has_sha1;
  unsigned char sha1_digest[APR_SHA1_DIGESTSIZE];
  unsigned char md5_digest[APR_MD5_DIGESTSIZE];

  /* Revision where this representation is located. */
  svn_revnum_t revision;

  /* Item index with the the revision. */
  apr_uint64_t item_index;

  /* The size of the representation in bytes as seen in the revision
     file. */
  svn_filesize_t size;

  /* The size of the fulltext of the representation. If this is 0,
   * the fulltext size is equal to representation size in the rev file, */
  svn_filesize_t expanded_size;
} binary_representation_t;

/* Add a uniquifier to binary_representation_t to distinguish between
 * multiple uses of the same shared representation.
 */
typedef struct shared_representation_t
{
  /* For rep-sharing, we need a way of uniquifying node-revs which share the
     same representation (see svn_fs_fs__noderev_same_rep_key() ).  So, we
     store the original txn of the node rev (not the rep!), along with some
     intra-node uniqification content. */
  struct
  {
    svn_fs_fs__id_part_t txn_id;
    apr_uint64_t number;
  } uniquifier;

  /* Index+1 of the representation.
   */
  int representation;
} shared_representation_t;


/* Our internal representation of a node_revision_t.
 * 
 * We will store path strings in a string container and reference them
 * from here.  Similarly, IDs and representations are being stored in
 * separate containers and then also referenced here.  This eliminates
 * the need to store the same IDs and representations more than once.
 */
typedef struct binary_noderev_t
{
  /* node type and presence indicators */
  apr_uint32_t flags;

  /* Index+1 of the node-id for this node-rev. */
  int id;

  /* Index+1 of the predecessor node revision id, or 0 if there is no
     predecessor for this node revision */
  int predecessor_id;

  /* number of predecessors this node revision has (recursively), or
     -1 if not known (for backward compatibility). */
  int predecessor_count;

  /* If this node-rev is a copy, what revision was it copied from? */
  svn_revnum_t copyfrom_rev;

  /* Helper for history tracing, root revision of the parent tree from
     whence this node-rev was copied. */
  svn_revnum_t copyroot_rev;

  /* If this node-rev is a copy, this is the string index+1 of the path
     from which that copy way made. 0, otherwise. */
  int copyfrom_path;

  /* String index+1 of the root of the parent tree from whence this node-
   * rev was copied. */
  int copyroot_path;

  /* Index+1 of the representation key for this node's properties.
     May be 0 if there are no properties.  */
  int prop_rep;

  /* Index+1 of the representation for this node's data.
     May be 0 if there is no data. */
  shared_representation_t data_rep;

  /* String index+1 of the path at which this node first came into
     existence.  */
  int created_path;

  /* Number of nodes with svn:mergeinfo properties that are
     descendants of this node (including it itself) */
  apr_int64_t mergeinfo_count;

} binary_noderev_t;

/* The actual container object.  Node revisions are concatenated into
 * NODEREVS, referenced representations are stored in DATA_REPS / PROP_REPS
 * and the ids in IDs.  PATHS is the string table for all paths.
 *
 * During construction, BUILDER will be used instead of PATHS. IDS_DICT,
 * DATA_REPS_DICT and PROP_REPS_DICT are also only used during construction
 * and are NULL otherwise.
 */
struct svn_fs_fs__noderevs_t
{
  /* The paths - either in 'builder' mode or finalized mode.
   * The respective other pointer will be NULL. */
  string_table_builder_t *builder;
  string_table_t *paths;

  /* During construction, maps a full binary_id_t to an index into IDS */
  apr_hash_t *ids_dict;

  /* During construction, maps a full binary_representation_t to an index
   * into DATA_REPS. */
  apr_hash_t *data_reps_dict;

  /* During construction, maps a full binary_representation_t to an index
   * into PROP_REPS. */
  apr_hash_t *prop_reps_dict;

  /* array of binary_id_t */
  apr_array_header_t *ids;

  /* arrays of binary_representation_t */
  apr_array_header_t *data_reps;
  apr_array_header_t *prop_reps;

  /* array of binary_noderev_t. */
  apr_array_header_t *noderevs;
};

svn_fs_fs__noderevs_t *
svn_fs_fs__noderevs_create(apr_size_t initial_count,
                           apr_pool_t* pool)
{
  svn_fs_fs__noderevs_t *noderevs = apr_palloc(pool, sizeof(*noderevs));

  noderevs->builder = svn_fs_fs__string_table_builder_create(pool);
  noderevs->ids_dict = svn_hash__make(pool);
  noderevs->data_reps_dict = svn_hash__make(pool);
  noderevs->prop_reps_dict = svn_hash__make(pool);
  noderevs->paths = NULL;

  noderevs->ids
    = apr_array_make(pool, initial_count, sizeof(binary_id_t));
  noderevs->data_reps
    = apr_array_make(pool, initial_count, sizeof(binary_representation_t));
  noderevs->prop_reps
    = apr_array_make(pool, initial_count, sizeof(binary_representation_t));
  noderevs->noderevs
    = apr_array_make(pool, initial_count, sizeof(binary_noderev_t));

  return noderevs;
}

/* Given the ID, return the index+1 into IDS that contains a binary_id
 * for it.  Returns 0 for NULL IDs.  We use DICT to detect duplicates.
 */
static int
store_id(apr_array_header_t *ids,
         apr_hash_t *dict,
         const svn_fs_id_t *id)
{
  binary_id_t bin_id = { { 0 } };
  int idx;

  if (id == NULL)
    return 0;
  
  bin_id.node_id = *svn_fs_fs__id_node_id(id);
  bin_id.copy_id = *svn_fs_fs__id_copy_id(id);
  bin_id.rev_id = *svn_fs_fs__id_rev_item(id);

  idx = (int)(apr_uintptr_t)apr_hash_get(dict, &bin_id, sizeof(bin_id));
  if (idx == 0)
    {
      APR_ARRAY_PUSH(ids, binary_id_t) = bin_id;
      idx = ids->nelts;
      apr_hash_set(dict, ids->elts + (idx-1) * ids->elt_size,
                   ids->elt_size, (void*)(apr_uintptr_t)idx);
    }

  return idx;
}

/* Given the REP, return the index+1 into REPS that contains a copy of it.
 * Returns 0 for NULL IDs.  We use DICT to detect duplicates.
 */
static int
store_representation(apr_array_header_t *reps,
                     apr_hash_t *dict,
                     const representation_t *rep)
{
  binary_representation_t binary_rep = { 0 };
  int idx;

  if (rep == NULL)
    return 0;

  binary_rep.has_sha1 = rep->has_sha1;
  memcpy(binary_rep.sha1_digest, rep->sha1_digest, sizeof(rep->sha1_digest));
  memcpy(binary_rep.md5_digest, rep->md5_digest, sizeof(rep->md5_digest));
  binary_rep.revision = rep->revision;
  binary_rep.item_index = rep->item_index;
  binary_rep.size = rep->size;
  binary_rep.expanded_size = rep->expanded_size;

  idx = (int)(apr_uintptr_t)apr_hash_get(dict, &binary_rep,
                                         sizeof(binary_rep));
  if (idx == 0)
    {
      APR_ARRAY_PUSH(reps, binary_representation_t) = binary_rep;
      idx = reps->nelts;
      apr_hash_set(dict, reps->elts + (idx-1) * reps->elt_size,
                   reps->elt_size, (void*)(apr_uintptr_t)idx);
    }

  return idx;
}

apr_size_t
svn_fs_fs__noderevs_add(svn_fs_fs__noderevs_t *container,
                        node_revision_t *noderev)
{
  binary_noderev_t binary_noderev = { 0 };

  binary_noderev.flags = (noderev->has_mergeinfo ? NODEREV_HAS_MINFO : 0)
                       | (noderev->copyfrom_path ? NODEREV_HAS_COPYFROM : 0)
                       | (noderev->copyroot_rev  ? NODEREV_HAS_COPYROOT : 0)
                       | (noderev->created_path  ? NODEREV_HAS_CPATH : 0)
                       | (int)noderev->kind;

  binary_noderev.id
    = store_id(container->ids, container->ids_dict, noderev->id);
  binary_noderev.predecessor_id
    = store_id(container->ids, container->ids_dict, noderev->predecessor_id);

  if (noderev->copyfrom_path)
    {
      binary_noderev.copyfrom_path
        = svn_fs_fs__string_table_builder_add(container->builder,
                                              noderev->copyfrom_path,
                                              0);
      binary_noderev.copyfrom_rev = noderev->copyfrom_rev;
    }

  if (noderev->copyroot_rev)
    {
      binary_noderev.copyroot_path
        = svn_fs_fs__string_table_builder_add(container->builder,
                                              noderev->copyroot_path,
                                              0);
      binary_noderev.copyroot_rev = noderev->copyroot_rev;
    }

  binary_noderev.predecessor_count = noderev->predecessor_count;
  binary_noderev.prop_rep = store_representation(container->prop_reps,
                                                 container->prop_reps_dict,
                                                 noderev->prop_rep);
  if (noderev->data_rep)
    {
      binary_noderev.data_rep.representation
        = store_representation(container->data_reps,
                               container->data_reps_dict,
                               noderev->data_rep);
      binary_noderev.data_rep.uniquifier.txn_id
        = noderev->data_rep->uniquifier.txn_id;
      binary_noderev.data_rep.uniquifier.number
        = noderev->data_rep->uniquifier.number;
    }

  if (noderev->created_path)
    binary_noderev.created_path
      = svn_fs_fs__string_table_builder_add(container->builder,
                                            noderev->created_path,
                                            0);

  binary_noderev.mergeinfo_count = noderev->mergeinfo_count;
  
  APR_ARRAY_PUSH(container->noderevs, binary_noderev_t) = binary_noderev;

  return container->noderevs->nelts - 1;
}

apr_size_t
svn_fs_fs__noderevs_estimate_size(const svn_fs_fs__noderevs_t *container)
{
  /* CONTAINER must be in 'builder' mode */
  if (container->builder == NULL)
    return 0;

  /* string table code makes its own prediction,
   * noderevs should be < 16 bytes each,
   * ids < 10 bytes each,
   * data representations < 40 bytes each,
   * property representations < 30 bytes each,
   * some static overhead should be assumed */
  return svn_fs_fs__string_table_builder_estimate_size(container->builder)
       + container->noderevs->nelts * 16
       + container->ids->nelts * 10
       + container->data_reps->nelts * 40
       + container->prop_reps->nelts * 30
       + 100;
}

/* Create an svn_fs_id_t in *ID, allocated in POOL based on the id stored
 * at index IDX in IDS.
 */
static svn_error_t *
get_id(const svn_fs_id_t **id,
       const apr_array_header_t *ids,
       int idx,
       apr_pool_t *pool)
{
  binary_id_t *binary_id;

  /* handle NULL IDs  */
  if (idx == 0)
    {
      *id = NULL;
      return SVN_NO_ERROR;
    }

  /* check for corrupted data */
  if (idx < 0 || idx > ids->nelts)
    return svn_error_createf(SVN_ERR_FS_CONTAINER_INDEX, NULL,
                             _("Node revision ID index %d" 
                               " exceeds container size %d"),
                             idx, ids->nelts);

  /* create a svn_fs_id_t from stored info */
  binary_id = &APR_ARRAY_IDX(ids, idx - 1, binary_id_t);
  *id = svn_fs_fs__id_rev_create(&binary_id->node_id,
                                 &binary_id->copy_id,
                                 &binary_id->rev_id,
                                 pool);

  return SVN_NO_ERROR;
}

/* Create a representation_t in *REP, allocated in POOL based on the
 * representation stored at index IDX in REPS.
 */
static svn_error_t *
get_representation(representation_t **rep,
                   const apr_array_header_t *reps,
                   int idx,
                   apr_pool_t *pool)
{
  binary_representation_t *binary_rep;

  /* handle NULL representations  */
  if (idx == 0)
    {
      *rep = NULL;
      return SVN_NO_ERROR;
    }

  /* check for corrupted data */
  if (idx < 0 || idx > reps->nelts)
    return svn_error_createf(SVN_ERR_FS_CONTAINER_INDEX, NULL,
                             _("Node revision ID index %d"
                               " exceeds container size %d"),
                             idx, reps->nelts);

  /* no translation required. Just duplicate the info */
  binary_rep = &APR_ARRAY_IDX(reps, idx - 1, binary_representation_t);

  *rep = apr_pcalloc(pool, sizeof(**rep));
  (*rep)->has_sha1 = binary_rep->has_sha1;
  memcpy((*rep)->sha1_digest, binary_rep->sha1_digest,
         sizeof((*rep)->sha1_digest));
  memcpy((*rep)->md5_digest, binary_rep->md5_digest,
         sizeof((*rep)->md5_digest));
  (*rep)->revision = binary_rep->revision;
  (*rep)->item_index = binary_rep->item_index;
  (*rep)->size = binary_rep->size;
  (*rep)->expanded_size = binary_rep->expanded_size;
  svn_fs_fs__id_txn_reset(&(*rep)->txn_id);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__noderevs_get(node_revision_t **noderev_p,
                        const svn_fs_fs__noderevs_t *container,
                        apr_size_t idx,
                        apr_pool_t *pool)
{
  node_revision_t *noderev;
  binary_noderev_t *binary_noderev;
  
  /* CONTAINER must be in 'finalized' mode */
  SVN_ERR_ASSERT(container->builder == NULL);
  SVN_ERR_ASSERT(container->paths);

  /* validate index */
  if (idx >= (apr_size_t)container->noderevs->nelts)
    return svn_error_createf(SVN_ERR_FS_CONTAINER_INDEX, NULL,
                             _("Node revision index %" APR_SIZE_T_FMT
                               " exceeds container size %d"),
                             idx, container->noderevs->nelts);

  /* allocate result struct and fill it field by field */
  noderev = apr_pcalloc(pool, sizeof(*noderev));
  binary_noderev = &APR_ARRAY_IDX(container->noderevs, idx, binary_noderev_t);
  
  noderev->kind = (svn_node_kind_t)(binary_noderev->flags & NODEREV_KIND_MASK);
  SVN_ERR(get_id(&noderev->id, container->ids, binary_noderev->id, pool));
  SVN_ERR(get_id(&noderev->predecessor_id, container->ids,
                 binary_noderev->predecessor_id, pool));

  if (binary_noderev->flags & NODEREV_HAS_COPYFROM)
    {
      noderev->copyfrom_path
        = svn_fs_fs__string_table_get(container->paths,
                                      binary_noderev->copyfrom_path,
                                      NULL,
                                      pool);
      noderev->copyfrom_rev = binary_noderev->copyfrom_rev;
    }
  else
    {
      noderev->copyfrom_path = NULL;
      noderev->copyfrom_rev = SVN_INVALID_REVNUM;
    }

  if (binary_noderev->flags & NODEREV_HAS_COPYROOT)
    {
      noderev->copyroot_path
        = svn_fs_fs__string_table_get(container->paths,
                                      binary_noderev->copyroot_path,
                                      NULL,
                                      pool);
      noderev->copyroot_rev = binary_noderev->copyroot_rev;
    }
  else
    {
      noderev->copyroot_path = NULL;
      noderev->copyroot_rev = 0;
    }

  noderev->predecessor_count = binary_noderev->predecessor_count;

  SVN_ERR(get_representation(&noderev->prop_rep, container->prop_reps,
                             binary_noderev->prop_rep, pool));
  SVN_ERR(get_representation(&noderev->data_rep, container->data_reps,
                             binary_noderev->data_rep.representation, pool));
  if (noderev->data_rep)
    {
      noderev->data_rep->uniquifier.txn_id
        = binary_noderev->data_rep.uniquifier.txn_id;
      noderev->data_rep->uniquifier.number
        = binary_noderev->data_rep.uniquifier.number;
    }

  if (binary_noderev->flags & NODEREV_HAS_CPATH)
    noderev->created_path
      = svn_fs_fs__string_table_get(container->paths,
                                    binary_noderev->created_path,
                                    NULL,
                                    pool);

  noderev->mergeinfo_count = binary_noderev->mergeinfo_count;

  noderev->has_mergeinfo = (binary_noderev->flags & NODEREV_HAS_MINFO) ? 1 : 0;
  *noderev_p = noderev;

  return SVN_NO_ERROR;
}

/* Create and return a stream for representations in PARENT.
 * Initialize the sub-streams for all fields, except checksums.
 */
static svn_packed__int_stream_t *
create_rep_stream(svn_packed__int_stream_t *parent)
{
  svn_packed__int_stream_t *stream
    = svn_packed__create_int_substream(parent, FALSE, FALSE);

  /* sub-streams for members - except for checksums */
  /* has_sha1 */
  svn_packed__create_int_substream(stream, FALSE, FALSE);

  /* rev, item_index, size, expanded_size */
  svn_packed__create_int_substream(stream, TRUE, FALSE);
  svn_packed__create_int_substream(stream, FALSE, FALSE);
  svn_packed__create_int_substream(stream, FALSE, FALSE);
  svn_packed__create_int_substream(stream, FALSE, FALSE);

  return stream;
}

/* Serialize all representations in REP.  Store checksums in DIGEST_STREAM,
 * put all other fields into REP_STREAM.
 */
static void
write_reps(svn_packed__int_stream_t *rep_stream,
           svn_packed__byte_stream_t *digest_stream,
           apr_array_header_t *reps)
{
  int i;
  for (i = 0; i < reps->nelts; ++i)
    {
      binary_representation_t *rep
        = &APR_ARRAY_IDX(reps, i, binary_representation_t);

      svn_packed__add_uint(rep_stream, rep->has_sha1);

      svn_packed__add_uint(rep_stream, rep->revision);
      svn_packed__add_uint(rep_stream, rep->item_index);
      svn_packed__add_uint(rep_stream, rep->size);
      svn_packed__add_uint(rep_stream, rep->expanded_size);
      
      svn_packed__add_bytes(digest_stream,
                            (const char *)rep->md5_digest,
                            sizeof(rep->md5_digest));
      if (rep->has_sha1)
        svn_packed__add_bytes(digest_stream,
                              (const char *)rep->sha1_digest,
                              sizeof(rep->sha1_digest));
    }
}

svn_error_t *
svn_fs_fs__write_noderevs_container(svn_stream_t *stream,
                                    const svn_fs_fs__noderevs_t *container,
                                    apr_pool_t *pool)
{
  int i;

  string_table_t *paths = container->paths
                        ? container->paths
                        : svn_fs_fs__string_table_create(container->builder,
                                                         pool);

  svn_packed__data_root_t *root = svn_packed__data_create_root(pool);

  /* one common top-level stream for all arrays. One sub-stream */
  svn_packed__int_stream_t *structs_stream
    = svn_packed__create_int_stream(root, FALSE, FALSE);
  svn_packed__int_stream_t *ids_stream
    = svn_packed__create_int_substream(structs_stream, FALSE, FALSE);
  svn_packed__int_stream_t *data_reps_stream
    = create_rep_stream(structs_stream);
  svn_packed__int_stream_t *prop_reps_stream
    = create_rep_stream(structs_stream);
  svn_packed__int_stream_t *noderevs_stream
    = svn_packed__create_int_substream(structs_stream, FALSE, FALSE);
  svn_packed__byte_stream_t *digests_stream
    = svn_packed__create_bytes_stream(root);

  /* structure the CHANGES_STREAM such we can extract much of the redundancy
   * from the binary_change_t structs */
  for (i = 0; i < 3 * 2; ++i)
    svn_packed__create_int_substream(ids_stream, TRUE, FALSE);

  svn_packed__create_int_substream(noderevs_stream, FALSE, FALSE);
  for (i = 0; i < 14; ++i)
    svn_packed__create_int_substream(noderevs_stream, TRUE, FALSE);

  /* serialize ids array */
  for (i = 0; i < container->ids->nelts; ++i)
    {
      binary_id_t *id = &APR_ARRAY_IDX(container->ids, i, binary_id_t);

      svn_packed__add_uint(ids_stream, id->node_id.revision);
      svn_packed__add_uint(ids_stream, id->node_id.number);
      svn_packed__add_uint(ids_stream, id->copy_id.revision);
      svn_packed__add_uint(ids_stream, id->copy_id.number);
      svn_packed__add_uint(ids_stream, id->rev_id.revision);
      svn_packed__add_uint(ids_stream, id->rev_id.number);
    }

  /* serialize rep arrays */
  write_reps(data_reps_stream, digests_stream, container->data_reps);
  write_reps(prop_reps_stream, digests_stream, container->prop_reps);

  /* serialize noderevs array */
  for (i = 0; i < container->noderevs->nelts; ++i)
    {
      const binary_noderev_t *noderev
        = &APR_ARRAY_IDX(container->noderevs, i, binary_noderev_t);

      svn_packed__add_uint(noderevs_stream, noderev->flags);
      
      svn_packed__add_uint(noderevs_stream, noderev->id);
      svn_packed__add_uint(noderevs_stream, noderev->predecessor_id);
      svn_packed__add_uint(noderevs_stream, noderev->predecessor_count);
      
      svn_packed__add_uint(noderevs_stream, noderev->copyfrom_path);
      svn_packed__add_uint(noderevs_stream, noderev->copyfrom_rev);
      svn_packed__add_uint(noderevs_stream, noderev->copyroot_path);
      svn_packed__add_uint(noderevs_stream, noderev->copyroot_rev);

      svn_packed__add_uint(noderevs_stream, noderev->prop_rep);
      svn_packed__add_uint(noderevs_stream, noderev->data_rep.representation);

      svn_packed__add_uint(noderevs_stream,
                           noderev->data_rep.uniquifier.txn_id.revision);
      svn_packed__add_uint(noderevs_stream,
                           noderev->data_rep.uniquifier.txn_id.number);
      svn_packed__add_uint(noderevs_stream,
                           noderev->data_rep.uniquifier.number);

      svn_packed__add_uint(noderevs_stream, noderev->created_path);
      svn_packed__add_uint(noderevs_stream, noderev->mergeinfo_count);
    }

  /* write to disk */
  SVN_ERR(svn_fs_fs__write_string_table(stream, paths, pool));
  SVN_ERR(svn_packed__data_write(stream, root, pool));
  
  return SVN_NO_ERROR;
}

/* Allocate a representation_t array in POOL and return it in *REPS_P.
 * Deserialize the data in REP_STREAM and DIGEST_STREAM and store the
 * resulting representations into the *REPS_P.
 */
static svn_error_t *
read_reps(apr_array_header_t **reps_p,
          svn_packed__int_stream_t *rep_stream,
          svn_packed__byte_stream_t *digest_stream,
          apr_pool_t *pool)
{
  apr_size_t i;
  apr_size_t len;
  const char *bytes;

  apr_size_t count
    = svn_packed__int_count(svn_packed__first_int_substream(rep_stream));
  apr_array_header_t *reps
    = apr_array_make(pool, count, sizeof(binary_representation_t));

  for (i = 0; i < count; ++i)
    {
      binary_representation_t rep;

      rep.has_sha1 = svn_packed__get_uint(rep_stream);

      rep.revision = svn_packed__get_uint(rep_stream);
      rep.item_index = svn_packed__get_uint(rep_stream);
      rep.size = svn_packed__get_uint(rep_stream);
      rep.expanded_size = svn_packed__get_uint(rep_stream);

      /* when extracting the checksums, beware of buffer under/overflows
         caused by disk data corruption. */
      bytes = svn_packed__get_bytes(digest_stream, &len);
      if (len != sizeof(rep.md5_digest))
        return svn_error_createf(SVN_ERR_FS_CONTAINER_INDEX, NULL,
                                _("Unexpected MD5 digest size %"
                                  APR_SIZE_T_FMT),
                                len);

      memcpy(rep.md5_digest, bytes, sizeof(rep.md5_digest));
      if (rep.has_sha1)
        {
          bytes = svn_packed__get_bytes(digest_stream, &len);
          if (len != sizeof(rep.sha1_digest))
            return svn_error_createf(SVN_ERR_FS_CONTAINER_INDEX, NULL,
                                    _("Unexpected SHA1 digest size %"
                                      APR_SIZE_T_FMT),
                                    len);

          memcpy(rep.sha1_digest, bytes, sizeof(rep.sha1_digest));
        }

      APR_ARRAY_PUSH(reps, binary_representation_t) = rep;
    }

  *reps_p = reps;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_noderevs_container(svn_fs_fs__noderevs_t **container,
                                   svn_stream_t *stream,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  apr_size_t i;
  apr_size_t count;

  svn_fs_fs__noderevs_t *noderevs
    = apr_pcalloc(result_pool, sizeof(*noderevs));

  svn_packed__data_root_t *root;
  svn_packed__int_stream_t *structs_stream;
  svn_packed__int_stream_t *ids_stream;
  svn_packed__int_stream_t *data_reps_stream;
  svn_packed__int_stream_t *prop_reps_stream;
  svn_packed__int_stream_t *noderevs_stream;
  svn_packed__byte_stream_t *digests_stream;

  /* read everything from disk */
  SVN_ERR(svn_fs_fs__read_string_table(&noderevs->paths, stream,
                                       result_pool, scratch_pool));
  SVN_ERR(svn_packed__data_read(&root, stream, result_pool, scratch_pool));

  /* get streams */
  structs_stream = svn_packed__first_int_stream(root);
  ids_stream = svn_packed__first_int_substream(structs_stream);
  data_reps_stream = svn_packed__next_int_stream(ids_stream);
  prop_reps_stream = svn_packed__next_int_stream(data_reps_stream);
  noderevs_stream = svn_packed__next_int_stream(prop_reps_stream);
  digests_stream = svn_packed__first_byte_stream(root);

  /* read ids array */
  count
    = svn_packed__int_count(svn_packed__first_int_substream(ids_stream));
  noderevs->ids
    = apr_array_make(result_pool, count, sizeof(binary_id_t));
  for (i = 0; i < count; ++i)
    {
      binary_id_t id;

      id.node_id.revision = svn_packed__get_uint(ids_stream);
      id.node_id.number = svn_packed__get_uint(ids_stream);
      id.copy_id.revision = svn_packed__get_uint(ids_stream);
      id.copy_id.number = svn_packed__get_uint(ids_stream);
      id.rev_id.revision = svn_packed__get_uint(ids_stream);
      id.rev_id.number = svn_packed__get_uint(ids_stream);

      APR_ARRAY_PUSH(noderevs->ids, binary_id_t) = id;
    }
    
  /* read rep arrays */
  SVN_ERR(read_reps(&noderevs->data_reps, data_reps_stream, digests_stream,
                    result_pool));
  SVN_ERR(read_reps(&noderevs->prop_reps, prop_reps_stream, digests_stream,
                    result_pool));

  /* read noderevs array */
  count
    = svn_packed__int_count(svn_packed__first_int_substream(noderevs_stream));
  noderevs->noderevs
    = apr_array_make(result_pool, count, sizeof(binary_noderev_t));
  for (i = 0; i < count; ++i)
    {
      binary_noderev_t noderev;

      noderev.flags = svn_packed__get_uint(noderevs_stream);

      noderev.id = svn_packed__get_uint(noderevs_stream);
      noderev.predecessor_id = svn_packed__get_uint(noderevs_stream);
      noderev.predecessor_count = svn_packed__get_uint(noderevs_stream);

      noderev.copyfrom_path = svn_packed__get_uint(noderevs_stream);
      noderev.copyfrom_rev = svn_packed__get_uint(noderevs_stream);
      noderev.copyroot_path = svn_packed__get_uint(noderevs_stream);
      noderev.copyroot_rev = svn_packed__get_uint(noderevs_stream);

      noderev.prop_rep = svn_packed__get_uint(noderevs_stream);
      noderev.data_rep.representation = svn_packed__get_uint(noderevs_stream);

      noderev.data_rep.uniquifier.txn_id.revision
        = svn_packed__get_uint(noderevs_stream);
      noderev.data_rep.uniquifier.txn_id.number
        = svn_packed__get_uint(noderevs_stream);
      noderev.data_rep.uniquifier.number
        = svn_packed__get_uint(noderevs_stream);

      noderev.created_path = svn_packed__get_uint(noderevs_stream);
      noderev.mergeinfo_count = svn_packed__get_uint(noderevs_stream);

      APR_ARRAY_PUSH(noderevs->noderevs, binary_noderev_t) = noderev;
    }

  *container = noderevs;
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__serialize_noderevs_container(void **data,
                                        apr_size_t *data_len,
                                        void *in,
                                        apr_pool_t *pool)
{
  svn_fs_fs__noderevs_t *noderevs = in;
  svn_stringbuf_t *serialized;
  apr_size_t size
    = noderevs->ids->elt_size * noderevs->ids->nelts
    + noderevs->data_reps->elt_size * noderevs->data_reps->nelts
    + noderevs->prop_reps->elt_size * noderevs->prop_reps->nelts
    + noderevs->noderevs->elt_size * noderevs->noderevs->nelts
    + 10 * noderevs->noderevs->elt_size
    + 100;

  /* serialize array header and all its elements */
  svn_temp_serializer__context_t *context
    = svn_temp_serializer__init(noderevs, sizeof(*noderevs), size, pool);

  /* serialize sub-structures */
  svn_fs_fs__serialize_string_table(context, &noderevs->paths);
  svn_fs_fs__serialize_apr_array(context, &noderevs->ids);
  svn_fs_fs__serialize_apr_array(context, &noderevs->data_reps);
  svn_fs_fs__serialize_apr_array(context, &noderevs->prop_reps);
  svn_fs_fs__serialize_apr_array(context, &noderevs->noderevs);

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *data = serialized->data;
  *data_len = serialized->len;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__deserialize_noderevs_container(void **out,
                                          void *data,
                                          apr_size_t data_len,
                                          apr_pool_t *pool)
{
  svn_fs_fs__noderevs_t *noderevs = (svn_fs_fs__noderevs_t *)data;

  /* de-serialize sub-structures */
  svn_fs_fs__deserialize_string_table(noderevs, &noderevs->paths);
  svn_fs_fs__deserialize_apr_array(noderevs, &noderevs->ids, pool);
  svn_fs_fs__deserialize_apr_array(noderevs, &noderevs->data_reps, pool);
  svn_fs_fs__deserialize_apr_array(noderevs, &noderevs->prop_reps, pool);
  svn_fs_fs__deserialize_apr_array(noderevs, &noderevs->noderevs, pool);

  /* done */
  *out = noderevs;

  return SVN_NO_ERROR;
}

/* Deserialize the cache serialized APR struct at *IN in BUFFER and write
 * the result to OUT.  Note that this will only resolve the pointers and
 * not the array elements themselves. */
static void
resolve_apr_array_header(apr_array_header_t *out,
                         const void *buffer,
                         apr_array_header_t * const *in)
{
  const apr_array_header_t *array
    = svn_temp_deserializer__ptr(buffer, (const void *const *)in);
  const char *elements
    = svn_temp_deserializer__ptr(array, (const void *const *)&array->elts);

  *out = *array;
  out->elts = (char *)elements;
  out->pool = NULL;
}

svn_error_t *
svn_fs_fs__noderevs_get_func(void **out,
                             const void *data,
                             apr_size_t data_len,
                             void *baton,
                             apr_pool_t *pool)
{
  node_revision_t *noderev;
  binary_noderev_t *binary_noderev;
  
  apr_array_header_t ids;
  apr_array_header_t data_reps;
  apr_array_header_t prop_reps;
  apr_array_header_t noderevs;

  apr_uint32_t idx = *(apr_uint32_t *)baton;
  const svn_fs_fs__noderevs_t *container = data;

  /* Resolve all container pointers */
  const string_table_t *paths
    = svn_temp_deserializer__ptr(container,
                         (const void *const *)&container->paths);

  resolve_apr_array_header(&ids, container, &container->ids);
  resolve_apr_array_header(&data_reps, container, &container->data_reps);
  resolve_apr_array_header(&prop_reps, container, &container->prop_reps);
  resolve_apr_array_header(&noderevs, container, &container->noderevs);
  
  /* allocate result struct and fill it field by field */
  noderev = apr_pcalloc(pool, sizeof(*noderev));
  binary_noderev = &APR_ARRAY_IDX(&noderevs, idx, binary_noderev_t);
  
  noderev->kind = (svn_node_kind_t)(binary_noderev->flags & NODEREV_KIND_MASK);
  SVN_ERR(get_id(&noderev->id, &ids, binary_noderev->id, pool));
  SVN_ERR(get_id(&noderev->predecessor_id, &ids,
                 binary_noderev->predecessor_id, pool));

  if (binary_noderev->flags & NODEREV_HAS_COPYFROM)
    {
      noderev->copyfrom_path
        = svn_fs_fs__string_table_get_func(paths,
                                           binary_noderev->copyfrom_path,
                                           NULL,
                                           pool);
      noderev->copyfrom_rev = binary_noderev->copyfrom_rev;
    }
  else
    {
      noderev->copyfrom_path = NULL;
      noderev->copyfrom_rev = SVN_INVALID_REVNUM;
    }

  if (binary_noderev->flags & NODEREV_HAS_COPYROOT)
    {
      noderev->copyroot_path
        = svn_fs_fs__string_table_get_func(paths,
                                           binary_noderev->copyroot_path,
                                           NULL,
                                           pool);
      noderev->copyroot_rev = binary_noderev->copyroot_rev;
    }
  else
    {
      noderev->copyroot_path = NULL;
      noderev->copyroot_rev = 0;
    }

  noderev->predecessor_count = binary_noderev->predecessor_count;

  SVN_ERR(get_representation(&noderev->prop_rep, &prop_reps,
                             binary_noderev->prop_rep, pool));
  SVN_ERR(get_representation(&noderev->data_rep, &data_reps,
                             binary_noderev->data_rep.representation, pool));
  if (noderev->data_rep)
    {
      noderev->data_rep->uniquifier.txn_id
        = binary_noderev->data_rep.uniquifier.txn_id;
      noderev->data_rep->uniquifier.number
        = binary_noderev->data_rep.uniquifier.number;
    }

  if (binary_noderev->flags & NODEREV_HAS_CPATH)
    noderev->created_path
      = svn_fs_fs__string_table_get_func(paths,
                                         binary_noderev->created_path,
                                         NULL,
                                         pool);

  noderev->mergeinfo_count = binary_noderev->mergeinfo_count;

  noderev->has_mergeinfo = (binary_noderev->flags & NODEREV_HAS_MINFO) ? 1 : 0;
  *out = noderev;

  return SVN_NO_ERROR;
}
