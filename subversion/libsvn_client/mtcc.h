/*
 * mtcc.h :  Multi Command Context definitions
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

#ifndef SVN_LIBSVN_CLIENT_MTCC_H
#define SVN_LIBSVN_CLIENT_H

/* The kind of operation to perform in an svn_client_mtcc_op_t */
typedef enum svn_client_mtcc_kind_t
{
  OP_OPEN_DIR,
  OP_OPEN_FILE,
  OP_ADD_DIR,
  OP_ADD_FILE,
  OP_DELETE,
} svn_client_mtcc_kind_t;

typedef struct svn_client_mtcc_op_t
{
  const char *name;                 /* basename of operation */
  svn_client_mtcc_kind_t kind;      /* editor operation */

  apr_array_header_t *children;     /* List of svn_client_mtcc_op_t * */

  const char *src_relpath;              /* For ADD_DIR, ADD_FILE */
  svn_revnum_t src_rev;                 /* For ADD_DIR, ADD_FILE */
  svn_stream_t *src_stream;             /* For ADD_FILE, OPEN_FILE */
  svn_checksum_t *src_checksum;         /* For ADD_FILE, OPEN_FILE */
  svn_stream_t *base_stream;            /* For ADD_FILE, OPEN_FILE */
  const svn_checksum_t *base_checksum;  /* For ADD_FILE, OPEN_FILE */

  apr_array_header_t *prop_mods;        /* For all except DELETE
                                           List of svn_prop_t */
} svn_client_mtcc_op_t;


struct svn_client_mtcc_t
{
  apr_pool_t *pool;
  svn_revnum_t base_revision;

  svn_ra_session_t *ra_session;
  svn_client_ctx_t *ctx;

  svn_client_mtcc_op_t *root_op;
};

#endif