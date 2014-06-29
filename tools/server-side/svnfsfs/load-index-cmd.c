/* load-index-cmd.c -- implements the dump-index sub-command.
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

#include "svn_ctype.h"
#include "svn_dirent_uri.h"
#include "svn_io.h"
#include "svn_pools.h"

#include "private/svn_sorts_private.h"

#include "../../../subversion/libsvn_fs_fs/index.h"
#include "../../../subversion/libsvn_fs_fs/transaction.h"
#include "../../../subversion/libsvn_fs_fs/util.h"

#include "svn_private_config.h"

#include "svnfsfs.h"

/* Calculate the FNV1 checksum over the offset range in REV_FILE, covered by
 * ENTRY.  Store the result in ENTRY->FNV1_CHECKSUM.  Use POOL for temporary
 * allocations. */
static svn_error_t *
calc_fnv1(svn_fs_fs__p2l_entry_t *entry,
          svn_fs_fs__revision_file_t *rev_file,
          apr_pool_t *pool)
{
  unsigned char buffer[4096];
  svn_checksum_t *checksum;
  svn_checksum_ctx_t *context
    = svn_checksum_ctx_create(svn_checksum_fnv1a_32x4, pool);
  apr_off_t size = entry->size;

  /* Special rules apply to unused sections / items.  The data must be a
   * sequence of NUL bytes (not checked here) and the checksum is fixed to 0.
   */
  if (entry->type == SVN_FS_FS__ITEM_TYPE_UNUSED)
    {
      entry->fnv1_checksum = 0;
      return SVN_NO_ERROR;
    }

  /* Read the block and feed it to the checksum calculator. */
  SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &entry->offset, pool));
  while (size > 0)
    {
      apr_size_t to_read = size > sizeof(buffer)
                         ? sizeof(buffer)
                         : (apr_size_t)size;
      SVN_ERR(svn_io_file_read_full2(rev_file->file, buffer, to_read, NULL,
                                     NULL, pool));
      SVN_ERR(svn_checksum_update(context, buffer, to_read));
      size -= to_read;
    }

  /* Store final checksum in ENTRY. */
  SVN_ERR(svn_checksum_final(&checksum, context, pool));
  entry->fnv1_checksum = ntohl(*(const apr_uint32_t *)checksum->digest);

  return SVN_NO_ERROR;
}

/* For FS, create a new P2L auto-deleting proto index file in POOL and return
 * its name in *INDEX_NAME.  All entries to write are given in ENTRIES and
 * of type svn_fs_fs__p2l_entry_t*.  The FVN1 checksums are not taken from
 * ENTRIES but are begin calculated from the current contents of REV_FILE
 * as we go.  Use POOL for allocations.
 */
static svn_error_t *
write_p2l_index(const char **index_name,
                svn_fs_t *fs,
                svn_fs_fs__revision_file_t *rev_file,
                apr_array_header_t *entries,
                apr_pool_t *pool)
{
  apr_file_t *proto_index;

  /* Use a subpool for immediate temp file cleanup at the end of this
   * function. */
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;

  /* Create a proto-index file. */
  SVN_ERR(svn_io_open_unique_file3(NULL, index_name, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   pool, iterpool));
  SVN_ERR(svn_fs_fs__p2l_proto_index_open(&proto_index, *index_name, pool));

  /* Write ENTRIES to proto-index file and calculate checksums as we go. */
  for (i = 0; i < entries->nelts; ++i)
    {
      svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t *);
      svn_pool_clear(iterpool);

      SVN_ERR(calc_fnv1(entry, rev_file, iterpool));
      SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry(proto_index, entry,
                                                   iterpool));
    }

  /* Convert proto-index into final index and move it into position.
   * Note that REV_FILE contains the start revision of the shard file if it
   * has been packed while REVISION may be somewhere in the middle.  For
   * non-packed shards, they will have identical values. */
  SVN_ERR(svn_io_file_close(proto_index, iterpool));

  /* Temp file cleanup. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* A svn_sort__array compatible comparator function, sorting the
 * svn_fs_fs__p2l_entry_t** given in LHS, RHS by revision. */
static int
compare_p2l_entry_revision(const void *lhs,
                           const void *rhs)
{
  const svn_fs_fs__p2l_entry_t *lhs_entry
    =*(const svn_fs_fs__p2l_entry_t **)lhs;
  const svn_fs_fs__p2l_entry_t *rhs_entry
    =*(const svn_fs_fs__p2l_entry_t **)rhs;

  if (lhs_entry->item.revision < rhs_entry->item.revision)
    return -1;

  return lhs_entry->item.revision == rhs_entry->item.revision ? 0 : 1;
}

/* For FS, create a new L2P auto-deleting proto index file in POOL and return
 * its name in *INDEX_NAME.  All entries to write are given in ENTRIES and
 * entries are of type svn_fs_fs__p2l_entry_t* (sic!).  The ENTRIES array
 * will be reordered.  Use POOL for allocations.
 */
static svn_error_t *
write_l2p_index(const char **index_name,
                svn_fs_t *fs,
                apr_array_header_t *entries,
                apr_pool_t *pool)
{
  apr_file_t *proto_index;

  /* Use a subpool for immediate temp file cleanup at the end of this
   * function. */
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;
  svn_revnum_t last_revision = SVN_INVALID_REVNUM;
  svn_revnum_t revision = SVN_INVALID_REVNUM;

  /* L2P index must be written in revision order.
   * Sort ENTRIES accordingly. */
  svn_sort__array(entries, compare_p2l_entry_revision);

  /* Find the first revision in the index
   * (must exist since no truly empty revs are allowed). */
  for (i = 0; i < entries->nelts && !SVN_IS_VALID_REVNUM(revision); ++i)
    revision = APR_ARRAY_IDX(entries, i, const svn_fs_fs__p2l_entry_t *)
               ->item.revision;

  /* Create the temporary proto-rev file. */
  SVN_ERR(svn_io_open_unique_file3(NULL, index_name, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   pool, iterpool));
  SVN_ERR(svn_fs_fs__l2p_proto_index_open(&proto_index, *index_name, pool));

  /*  Write all entries. */
  for (i = 0; i < entries->nelts; ++i)
    {
      const svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i, const svn_fs_fs__p2l_entry_t *);
      svn_pool_clear(iterpool);

      if (entry->type == SVN_FS_FS__ITEM_TYPE_UNUSED)
        continue;

      if (last_revision != entry->item.revision)
        {
          SVN_ERR(svn_fs_fs__l2p_proto_index_add_revision(proto_index, pool));
          last_revision = entry->item.revision;
        }

      SVN_ERR(svn_fs_fs__l2p_proto_index_add_entry(proto_index,
                                                   entry->offset,
                                                   entry->item.number,
                                                   iterpool));
    }

  /* Convert proto-index into final index and move it into position. */
  SVN_ERR(svn_io_file_close(proto_index, iterpool));

  /* Temp file cleanup. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Map svn_fs_fs__p2l_entry_t.type to C string. */
static const char *item_type_str[]
  = {"none", "frep", "drep", "fprop", "dprop", "node", "chgs", "rep"};

/* Reverse lookup in ITEM_TYPE_STR: Set *TYPE to the index that contains STR.
 * Return an error for invalid strings. */
static svn_error_t *
str_to_item_type(unsigned *type,
                 const char *str)
{
  unsigned i;
  for (i = 0; i < sizeof(item_type_str) / sizeof(item_type_str[0]); ++i)
    if (strcmp(item_type_str[i], str) == 0)
      {
        *type = i;
        return SVN_NO_ERROR;
      }

  return svn_error_createf(SVN_ERR_BAD_TOKEN, NULL,
                           _("Unknown item type '%s'"), str);
}

/* Parse the hex string given as const char * at IDX in TOKENS and return
 * its value in *VALUE_P.  Check for index overflows and non-hex chars.
 */
static svn_error_t *
token_to_i64(apr_int64_t *value_p,
             apr_array_header_t *tokens,
             int idx)
{
  const char *hex;
  char *end;
  apr_int64_t value;

  /* Tell the user when there is not enough information. */
  SVN_ERR_ASSERT(idx >= 0);
  if (tokens->nelts <= idx)
    return svn_error_createf(SVN_ERR_INVALID_INPUT, NULL,
                             _("%i columns needed, %i provided"),
                             idx + 1, tokens->nelts);

  /* hex -> int conversion */
  hex = APR_ARRAY_IDX(tokens, idx, const char *);
  value = apr_strtoi64(hex, &end, 16);

  /* Has the whole token be parsed without error? */
  if (errno || *end != '\0')
    return svn_error_createf(SVN_ERR_INVALID_INPUT, NULL,
                             _("%s is not a value HEX string"), hex);

  *value_p = value;
  return SVN_NO_ERROR;
}

/* Parse the P2L entry given as space separated values in LINE and return it
 * in *ENTRY.  Ignore extra columns.  Allocate the result in RESULT_POOL and
 * use SCRATCH_POOL for temporaries.
 */
static svn_error_t *
parse_index_line(svn_fs_fs__p2l_entry_t **entry,
                 svn_stringbuf_t *line,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  apr_array_header_t *tokens = svn_cstring_split(line->data, " ", TRUE,
                                                 scratch_pool);
  svn_fs_fs__p2l_entry_t *result = apr_pcalloc(result_pool, sizeof(*result));
  apr_int64_t value;

  /* Parse the hex columns. */
  SVN_ERR(token_to_i64(&value, tokens, 0));
  result->offset = (apr_off_t)value;
  SVN_ERR(token_to_i64(&value, tokens, 1));
  result->size = (apr_off_t)value;
  SVN_ERR(token_to_i64(&value, tokens, 4));
  result->item.number = (apr_uint64_t)value;

  /* We now know that there were at least 5 columns.
   * Parse the non-hex columns without index check. */
  SVN_ERR(str_to_item_type(&result->type,
                           APR_ARRAY_IDX(tokens, 2, const char *)));
  SVN_ERR(svn_revnum_parse(&result->item.revision,
                           APR_ARRAY_IDX(tokens, 3, const char *), NULL));

  *entry = result;
  return SVN_NO_ERROR;
}

/* Parse the space separated P2L index table from INPUT, one entry per line.
 * Rewrite the respective index files in PATH.  Allocate from POOL. */
static svn_error_t *
load_index(const char *path,
           svn_stream_t *input,
           apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_revnum_t revision = SVN_INVALID_REVNUM;
  apr_array_header_t *entries = apr_array_make(pool, 16, sizeof(void*));
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Check repository type and open it. */
  SVN_ERR(open_fs(&fs, path, pool));

  while (TRUE)
    {
      svn_stringbuf_t *line;
      svn_fs_fs__p2l_entry_t *entry;
      svn_boolean_t eol;

      /* Get the next line from the input and stop if there is none. */
      svn_pool_clear(iterpool);
      svn_stream_readline(input, &line, "\n", &eol, iterpool);
      if (eol)
        break;

      /* Skip header line(s).  They contain the sub-string [Ss]tart. */
      if (strstr(line->data, "tart"))
        continue;

      /* Ignore empty lines (mostly trailing ones but we don't really care).
       */
      svn_stringbuf_strip_whitespace(line);
      if (line->len == 0)
        continue;

      /* Parse the entry and append it to ENTRIES. */
      SVN_ERR(parse_index_line(&entry, line, pool, iterpool));
      APR_ARRAY_PUSH(entries, svn_fs_fs__p2l_entry_t *) = entry;

      /* There should be at least one item that is not empty.
       * Get a revision from (probably inside) the respective shard. */
      if (   revision == SVN_INVALID_REVNUM
          && entry->item.revision != SVN_INVALID_REVNUM)
        {
          revision = entry->item.revision;

          /* Check the FS format number. */
          if (! svn_fs_fs__use_log_addressing(fs, revision))
            return svn_error_create(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL, NULL);
        }

    }

  /* Treat an empty array as a no-op instead error. */
  if (entries->nelts != 0)
    {
      const char *l2p_proto_index;
      const char *p2l_proto_index;
      svn_fs_fs__revision_file_t *rev_file;

      /* Open rev / pack file & trim indexes + footer off it. */
      SVN_ERR(svn_fs_fs__open_pack_or_rev_file_writable(&rev_file, fs,
                                                        revision, iterpool,
                                                        iterpool));
      SVN_ERR(svn_fs_fs__auto_read_footer(rev_file));
      SVN_ERR(svn_io_file_trunc(rev_file->file, rev_file->l2p_offset,
                                iterpool));

      /* Create proto index files for the new index data
       * (will be cleaned up automatically with iterpool). */
      SVN_ERR(write_p2l_index(&p2l_proto_index, fs, rev_file, entries,
                              iterpool));
      SVN_ERR(write_l2p_index(&l2p_proto_index, fs, entries, iterpool));

      /* Combine rev data with new index data. */
      SVN_ERR(svn_fs_fs__add_index_data(fs, rev_file->file, l2p_proto_index,
                                        p2l_proto_index, revision, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
svn_error_t *
subcommand__load_index(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnfsfs__opt_state *opt_state = baton;
  svn_stream_t *input;

  SVN_ERR(svn_stream_for_stdin(&input, pool));
  SVN_ERR(load_index(opt_state->repository_path, input, pool));

  return SVN_NO_ERROR;
}
