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
 * @file svn_element.h
 * @brief Tree elements
 *
 * @since New in 1.10.
 */

#ifndef SVN_ELEMENT_H
#define SVN_ELEMENT_H

#include <apr_pools.h>

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** A location in a committed revision.
 *
 * @a rev shall not be #SVN_INVALID_REVNUM unless the interface using this
 * type specifically allows it and defines its meaning. */
typedef struct svn_pathrev_t
{
  svn_revnum_t rev;
  const char *relpath;
} svn_pathrev_t;

/* Return a duplicate of OLD, allocated in RESULT_POOL. */
svn_pathrev_t
svn_pathrev_dup(svn_pathrev_t old,
                apr_pool_t *result_pool);

/* Return true iff PEG_PATH1 and PEG_PATH2 are both the same location.
 */
svn_boolean_t
svn_pathrev_equal(svn_pathrev_t *peg_path1,
                  svn_pathrev_t *peg_path2);

/** Versioned payload of an element, excluding tree structure information.
 *
 * This specifies the properties and the text of a file or target of a
 * symlink, directly, or by reference to an existing committed element, or
 * by a delta against such a reference payload.
 *
 * ### An idea: If the sender and receiver agree, the payload for an element
 *     may be specified as "null" to designate that the payload is not
 *     available. For example, when a client performing a WC update has
 *     no read authorization for a given path, the server may send null
 *     payload and the client may record an 'absent' WC node. (This
 *     would not make sense in a commit.)
 */
typedef struct svn_element_payload_t svn_element_payload_t;

/*
 * ========================================================================
 * Element Payload Interface
 * ========================================================================
 *
 * @defgroup svn_element_payload Element payload interface
 * @{
 */

/** Versioned payload of a node, excluding tree structure information.
 *
 * Payload is described by setting fields in one of the following ways.
 * Other fields SHOULD be null (or equivalent).
 *
 *   by reference:  (kind=unknown, ref)
 *   dir:           (kind=dir, props)
 *   file:          (kind=file, props, text)
 *   symlink:       (kind=symlink, props, target)
 *
 * ### Idea for the future: Specify payload as an (optional) reference
 *     plus (optional) overrides or deltas against the reference?
 */
struct svn_element_payload_t
{
  /* The node kind for this payload: dir, file, symlink, or unknown. */
  svn_node_kind_t kind;

  /* Reference existing, committed payload at REF (for kind=unknown).
   * The 'null' value is (SVN_INVALID_REVNUM, NULL). */
  svn_pathrev_t ref;

  /* Properties (for kind != unknown).
   * Maps (const char *) name -> (svn_string_t) value.
   * An empty hash means no properties. (SHOULD NOT be NULL.)
   * ### Presently NULL means 'no change' in some contexts. */
  apr_hash_t *props;

  /* File text (for kind=file; otherwise SHOULD be NULL). */
  svn_stringbuf_t *text;

  /* Symlink target (for kind=symlink; otherwise SHOULD be NULL). */
  const char *target;

};

/** Duplicate a node-payload @a old into @a result_pool.
 */
svn_element_payload_t *
svn_element_payload_dup(const svn_element_payload_t *old,
                        apr_pool_t *result_pool);

/* Return true iff the payload of LEFT is identical to that of RIGHT.
 * References are not supported. Node kind 'unknown' is not supported.
 */
svn_boolean_t
svn_element_payload_equal(const svn_element_payload_t *left,
                          const svn_element_payload_t *right,
                          apr_pool_t *scratch_pool);

/** Create a new node-payload object by reference to an existing node.
 *
 * Set the node kind to 'unknown'.
 *
 * Allocate the result in @a result_pool, but only shallow-copy the
 * given arguments.
 */
svn_element_payload_t *
svn_element_payload_create_ref(svn_pathrev_t ref,
                               apr_pool_t *result_pool);

/** Create a new node-payload object for a directory node.
 *
 * Allocate the result in @a result_pool, but only shallow-copy the
 * given arguments.
 */
svn_element_payload_t *
svn_element_payload_create_dir(apr_hash_t *props,
                               apr_pool_t *result_pool);

/** Create a new node-payload object for a file node.
 *
 * Allocate the result in @a result_pool, but only shallow-copy the
 * given arguments.
 */
svn_element_payload_t *
svn_element_payload_create_file(apr_hash_t *props,
                                svn_stringbuf_t *text,
                                apr_pool_t *result_pool);

/** Create a new node-payload object for a symlink node.
 *
 * Allocate the result in @a result_pool, but only shallow-copy the
 * given arguments.
 */
svn_element_payload_t *
svn_element_payload_create_symlink(apr_hash_t *props,
                                   const char *target,
                                   apr_pool_t *result_pool);

/** @} */



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_ELEMENT_H */
