/* low_level.c --- low level r/w access to fs_fs file structures
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

#include "svn_fs.h"

#include "fs_fs.h"
#include "id.h"

/* Headers used to describe node-revision in the revision file. */
#define HEADER_ID          "id"
#define HEADER_TYPE        "type"
#define HEADER_COUNT       "count"
#define HEADER_PROPS       "props"
#define HEADER_TEXT        "text"
#define HEADER_CPATH       "cpath"
#define HEADER_PRED        "pred"
#define HEADER_COPYFROM    "copyfrom"
#define HEADER_COPYROOT    "copyroot"
#define HEADER_FRESHTXNRT  "is-fresh-txn-root"
#define HEADER_MINFO_HERE  "minfo-here"
#define HEADER_MINFO_CNT   "minfo-cnt"

/* Kinds that a change can be. */
#define ACTION_MODIFY      "modify"
#define ACTION_ADD         "add"
#define ACTION_DELETE      "delete"
#define ACTION_REPLACE     "replace"
#define ACTION_RESET       "reset"

/* True and False flags. */
#define FLAG_TRUE          "true"
#define FLAG_FALSE         "false"

/* Kinds that a node-rev can be. */
#define KIND_FILE          "file"
#define KIND_DIR           "dir"

/* Kinds of representation. */
#define REP_PLAIN          "PLAIN"
#define REP_DELTA          "DELTA"

/* An arbitrary maximum path length, so clients can't run us out of memory
 * by giving us arbitrarily large paths. */
#define FSFS_MAX_PATH_LEN 4096

/* Given the last "few" bytes (should be at least 40) of revision REV in
 * TRAILER,  parse the last line and return the offset of the root noderev
 * in *ROOT_OFFSET and the offset of the changes list in *CHANGES_OFFSET.
 * All offsets are relative to the revision's start offset.  ROOT_OFFSET
 * and / or CHANGES_OFFSET may be NULL.
 * 
 * Note that REV is only used to construct nicer error objects.
 */
svn_error_t *
svn_fs_fs__parse_revision_trailer(apr_off_t *root_offset,
                                  apr_off_t *changes_offset,
                                  svn_stringbuf_t *trailer,
                                  svn_revnum_t rev);

/* Given the offset of the root noderev in ROOT_OFFSET and the offset of
 * the changes list in CHANGES_OFFSET,  return the corresponding revision's
 * trailer.  Allocate it in POOL.
 */
svn_stringbuf_t *
svn_fs_fs__unparse_revision_trailer(apr_off_t root_offset,
                                    apr_off_t changes_offset,
                                    apr_pool_t *pool);

/* Parse the description of a representation from TEXT and store it
   into *REP_P.  Allocate *REP_P in POOL. */
svn_error_t *
svn_fs_fs__parse_representation(representation_t **rep_p,
                                svn_stringbuf_t *text,
                                apr_pool_t *pool);

/* Return a formatted string, compatible with filesystem format FORMAT,
   that represents the location of representation REP.  If
   MUTABLE_REP_TRUNCATED is given, the rep is for props or dir contents,
   and only a "-1" revision number will be given for a mutable rep.
   If MAY_BE_CORRUPT is true, guard for NULL when constructing the string.
   Perform the allocation from POOL.  */
svn_stringbuf_t *
svn_fs_fs__unparse_representation(representation_t *rep,
                                  int format,
                                  svn_boolean_t mutable_rep_truncated,
                                  svn_boolean_t may_be_corrupt,
                                  apr_pool_t *pool);

/* Read a node-revision from STREAM. Set *NODEREV to the new structure,
   allocated in POOL. */
svn_error_t *
svn_fs_fs__read_noderev(node_revision_t **noderev,
                        svn_stream_t *stream,
                        apr_pool_t *pool);

/* Write the node-revision NODEREV into the stream OUTFILE, compatible with
   filesystem format FORMAT.  Only write mergeinfo-related metadata if
   INCLUDE_MERGEINFO is true.  Temporary allocations are from POOL. */
svn_error_t *
svn_fs_fs__write_noderev(svn_stream_t *outfile,
                         node_revision_t *noderev,
                         int format,
                         svn_boolean_t include_mergeinfo,
                         apr_pool_t *pool);

/* This structure is used to hold the information stored in a representation
 * header. */
typedef struct svn_fs_fs__rep_header_t
{
  /* if TRUE,  this is a DELTA rep, PLAIN otherwise */
  svn_boolean_t is_delta;

  /* if IS_DELTA is TRUE, this flag indicates that there is no base rep,
   * i.e. this rep is simply self-compressed.  Ignored for PLAIN reps
   * but should be FALSE in that case. */
  svn_boolean_t is_delta_vs_empty;

  /* if this rep is a delta against some other rep, that base rep can
   * be found in this revision.  Should be 0 if there is no base rep. */
  svn_revnum_t base_revision;

  /* if this rep is a delta against some other rep, that base rep can
   * be found at this offset within the base rep's revision.  Should be 0
   * if there is no base rep. */
  apr_off_t base_offset;

  /* if this rep is a delta against some other rep, this is the (deltified)
   * size of that base rep.  Should be 0 if there is no base rep. */
  svn_filesize_t base_length;
} svn_fs_fs__rep_header_t;

/* Read the next line from file FILE and parse it as a text
   representation entry.  Return the parsed entry in *REP_ARGS_P.
   Perform all allocations in POOL. */
svn_error_t *
svn_fs_fs__read_rep_header(svn_fs_fs__rep_header_t **header,
                           svn_stream_t *stream,
                           apr_pool_t *pool);

/* Write the representation HEADER to STREAM.  Use POOL for allocations. */
svn_error_t *
svn_fs_fs__write_rep_header(svn_fs_fs__rep_header_t *header,
                            svn_stream_t *stream,
                            apr_pool_t *pool);

/* Read all the changes from STREAM and store them in *CHANGES.  Do all
   allocations in POOL. */
svn_error_t *
svn_fs_fs__read_changes(apr_array_header_t **changes,
                        svn_stream_t *stream,
                        apr_pool_t *pool);

/* Write the changed path info from CHANGES in filesystem FS to the
   output stream STREAM.  Perform temporary allocations in POOL.
 */
svn_error_t *
svn_fs_fs__write_changes(svn_stream_t *stream,
                         svn_fs_t *fs,
                         apr_hash_t *changes,
                         apr_pool_t *pool);

svn_error_t *
get_root_changes_offset(apr_off_t *root_offset,
                        apr_off_t *changes_offset,
                        apr_file_t *rev_file,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *pool);
