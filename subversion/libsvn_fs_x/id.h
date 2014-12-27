/* id.h : interface to node ID functions, private to libsvn_fs_x
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

#ifndef SVN_LIBSVN_FS_X_ID_H
#define SVN_LIBSVN_FS_X_ID_H

#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Unique identifier for a transaction within the given repository. */
typedef apr_int64_t svn_fs_x__txn_id_t;

/* svn_fs_x__txn_id_t value for everything that is not a transaction. */
#define SVN_FS_X__INVALID_TXN_ID ((svn_fs_x__txn_id_t)(-1))

/* Change set is the umbrella term for transaction and revision in FSX.
 * Revision numbers (>=0) map 1:1 onto change sets while txns are mapped
 * onto the negatve value range. */
typedef apr_int64_t svn_fs_x__change_set_t;

/* Invalid / unused change set number. */
#define SVN_FS_X__INVALID_CHANGE_SET  ((svn_fs_x__change_set_t)(-1))

/* Return TRUE iff the CHANGE_SET refers to a revision
   (will return FALSE for SVN_INVALID_REVNUM). */
svn_boolean_t svn_fs_x__is_revision(svn_fs_x__change_set_t change_set);

/* Return TRUE iff the CHANGE_SET refers to a transaction
   (will return FALSE for SVN_FS_X__INVALID_TXN_ID). */
svn_boolean_t svn_fs_x__is_txn(svn_fs_x__change_set_t change_set);

/* Return the revision number that corresponds to CHANGE_SET.
   Will SVN_INVALID_REVNUM for transactions. */
svn_revnum_t svn_fs_x__get_revnum(svn_fs_x__change_set_t change_set);

/* Return the transaction ID that corresponds to CHANGE_SET.
   Will SVN_FS_X__INVALID_TXN_ID for revisions. */
apr_int64_t svn_fs_x__get_txn_id(svn_fs_x__change_set_t change_set);

/* Convert REVNUM into a change set number */
svn_fs_x__change_set_t svn_fs_x__change_set_by_rev(svn_revnum_t revnum);

/* Convert TXN_ID into a change set number */
svn_fs_x__change_set_t svn_fs_x__change_set_by_txn(apr_int64_t txn_id);

/* A rev node ID in FSX consists of a 3 of sub-IDs ("parts") that consist
 * of a creation CHANGE_SET number and some revision-local counter value
 * (NUMBER).
 */
typedef struct svn_fs_x__id_part_t
{
  svn_fs_x__change_set_t change_set;

  apr_uint64_t number;
} svn_fs_x__id_part_t;

/* Noderevs are identified by part IDs like everything else in FSX. */
typedef svn_fs_x__id_part_t svn_fs_x__noderev_id_t;


/*** Operations on ID parts. ***/

/* Return TRUE, if both elements of the PART is 0, i.e. this is the default
 * value if e.g. no copies were made of this node. */
svn_boolean_t svn_fs_x__id_part_is_root(const svn_fs_x__id_part_t *part);

/* Return TRUE, if all element values of *LHS and *RHS match. */
svn_boolean_t svn_fs_x__id_part_eq(const svn_fs_x__id_part_t *lhs,
                                   const svn_fs_x__id_part_t *rhs);

/* Parse the NUL-terminated ID part at DATA and write the result into *PART.
 */
svn_error_t *
svn_fs_x__id_part_parse(svn_fs_x__id_part_t *part,
                        const char *data);

/* Convert ID into string form, allocated in POOL. */
svn_string_t *
svn_fs_x__id_part_unparse(const svn_fs_x__id_part_t*id,
                          apr_pool_t *pool);

/* Set *PART to "unused". */
void svn_fs_x__id_part_reset(svn_fs_x__id_part_t *part);

/* Return TRUE if *PART is belongs to either a revision or transaction. */
svn_boolean_t svn_fs_x__id_part_used(const svn_fs_x__id_part_t *part);

/* Return 0 if A and B are equal, 1 if A is "greater than" B, -1 otherwise. */
int svn_fs_x__id_part_compare(const svn_fs_x__id_part_t *a,
                              const svn_fs_x__id_part_t *b);

/* Set *NODEREV_ID to the root node ID of transaction TXN_ID. */
void
svn_fs_x__init_txn_root(svn_fs_x__noderev_id_t *noderev_id,
                        svn_fs_x__txn_id_t txn_id);

/* Set *NODEREV_ID to the root node ID of revision REV. */
void
svn_fs_x__init_rev_root(svn_fs_x__noderev_id_t *noderev_id,
                        svn_revnum_t rev);


/*** ID accessor functions. ***/

/* Get the "node id" portion of ID. */
const svn_fs_x__id_part_t *svn_fs_x__id_node_id(const svn_fs_id_t *id);

/* Get the "noderev id" portion of ID. */
const svn_fs_x__noderev_id_t *svn_fs_x__id_noderev_id(const svn_fs_id_t *id);

/* Return TRUE, if this is a transaction ID. */
svn_boolean_t svn_fs_x__id_is_txn(const svn_fs_id_t *id);

/* Convert ID into string form, allocated in POOL. */
svn_string_t *svn_fs_x__id_unparse(const svn_fs_id_t *id,
                                   apr_pool_t *pool);

/* Return true if A and B are equal. */
svn_boolean_t svn_fs_x__id_eq(const svn_fs_id_t *a,
                              const svn_fs_id_t *b);

/* Create a permanent ID based on NODE_ID and NODEREV_ID, allocated in
   POOL.  Return NULL, if the NODEREV_ID is "unused". */
svn_fs_id_t *svn_fs_x__id_create(const svn_fs_x__id_part_t *node_id,
                                 const svn_fs_x__id_part_t *noderev_id,
                                 apr_pool_t *pool);

/* Return a copy of ID, allocated from POOL. */
svn_fs_id_t *svn_fs_x__id_copy(const svn_fs_id_t *id,
                               apr_pool_t *pool);

/* Return an ID in *ID_P resulting from parsing the string DATA, or an error
   if DATA is an invalid ID string. *DATA will be modified / invalidated by
   this call. */
svn_error_t *
svn_fs_x__id_parse(const svn_fs_id_t **id_p,
                   char *data,
                   apr_pool_t *pool);


/* (de-)serialization support*/

struct svn_temp_serializer__context_t;

/**
 * Serialize an @a id within the serialization @a context.
 */
void
svn_fs_x__id_serialize(struct svn_temp_serializer__context_t *context,
                        const svn_fs_id_t * const *id);

/**
 * Deserialize an @a id within the @a buffer and associate it with @a pool.
 */
void
svn_fs_x__id_deserialize(void *buffer,
                         svn_fs_id_t **id,
                         apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_X_ID_H */
