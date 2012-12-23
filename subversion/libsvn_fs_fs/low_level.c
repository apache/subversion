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

#include <apr_md5.h>
#include <apr_sha1.h>

#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_sorts.h"

#include "../libsvn_fs/fs-loader.h"

#include "low_level.h"
#include "util.h"
#include "pack.h"
#include "cached_data.h"

/* The 256 is an arbitrary size large enough to hold the node id and the
 * various flags. */
#define MAX_CHANGE_LINE_LEN FSFS_MAX_PATH_LEN + 256

/* Given the last "few" bytes (should be at least 40) of revision REV in
 * TRAILER,  parse the last line and return the offset of the root noderev
 * in *ROOT_OFFSET and the offset of the changes list in *CHANGES_OFFSET.
 * All offsets are relative to the revision's start offset.
 * 
 * Note that REV is only used to construct nicer error objects.
 */
svn_error_t *
parse_revision_trailer(apr_off_t *root_offset,
                       apr_off_t *changes_offset,
                       svn_stringbuf_t *trailer,
                       svn_revnum_t rev)
{
  int i, num_bytes;
  const char *str;

  /* This cast should be safe since the maximum amount read, 64, will
     never be bigger than the size of an int. */
  num_bytes = (int) trailer->len;

  /* The last byte should be a newline. */
  if (trailer->len == 0 || trailer->data[trailer->len - 1] != '\n')
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Revision file (r%ld) lacks trailing newline"),
                               rev);
    }

  /* Look for the next previous newline. */
  for (i = num_bytes - 2; i >= 0; i--)
    {
      if (trailer->data[i] == '\n')
        break;
    }

  if (i < 0)
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Final line in revision file (r%ld) longer "
                                 "than 64 characters"),
                               rev);
    }

  i++;
  str = &trailer->data[i];

  /* find the next space */
  for ( ; i < (num_bytes - 2) ; i++)
    if (trailer->data[i] == ' ')
      break;

  if (i == (num_bytes - 2))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Final line in revision file r%ld missing space"),
                             rev);

  if (root_offset)
    {
      apr_int64_t val;

      trailer->data[i] = '\0';
      SVN_ERR(svn_cstring_atoi64(&val, str));
      *root_offset = (apr_off_t)val;
    }

  i++;
  str = &trailer->data[i];

  /* find the next newline */
  for ( ; i < num_bytes; i++)
    if (trailer->data[i] == '\n')
      break;

  if (changes_offset)
    {
      apr_int64_t val;

      trailer->data[i] = '\0';
      SVN_ERR(svn_cstring_atoi64(&val, str));
      *changes_offset = (apr_off_t)val;
    }

  return SVN_NO_ERROR;
}

/* Given the offset of the root noderev in ROOT_OFFSET and the offset of
 * the changes list in CHANGES_OFFSET,  return the corresponding revision's
 * trailer.  Allocate it in POOL.
 */
svn_stringbuf_t *
unparse_revision_trailer(apr_off_t root_offset,
                         apr_off_t changes_offset,
                         apr_pool_t *pool)
{
  return svn_stringbuf_createf(pool,
                               "\n%" APR_OFF_T_FMT " %" APR_OFF_T_FMT "\n",
                               root_offset,
                               changes_offset);
}

/* Given a revision file FILE that has been pre-positioned at the
   beginning of a Node-Rev header block, read in that header block and
   store it in the apr_hash_t HEADERS.  All allocations will be from
   POOL. */
svn_error_t *
read_header_block(apr_hash_t **headers,
                  svn_stream_t *stream,
                  apr_pool_t *pool)
{
  *headers = apr_hash_make(pool);

  while (1)
    {
      svn_stringbuf_t *header_str;
      const char *name, *value;
      apr_size_t i = 0;
      svn_boolean_t eof;

      SVN_ERR(svn_stream_readline(stream, &header_str, "\n", &eof, pool));

      if (eof || header_str->len == 0)
        break; /* end of header block */

      while (header_str->data[i] != ':')
        {
          if (header_str->data[i] == '\0')
            return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                     _("Found malformed header '%s' in "
                                       "revision file"),
                                     header_str->data);
          i++;
        }

      /* Create a 'name' string and point to it. */
      header_str->data[i] = '\0';
      name = header_str->data;

      /* Skip over the NULL byte and the space following it. */
      i += 2;

      if (i > header_str->len)
        {
          /* Restore the original line for the error. */
          i -= 2;
          header_str->data[i] = ':';
          return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                   _("Found malformed header '%s' in "
                                     "revision file"),
                                   header_str->data);
        }

      value = header_str->data + i;

      /* header_str is safely in our pool, so we can use bits of it as
         key and value. */
      apr_hash_set(*headers, name, APR_HASH_KEY_STRING, value);
    }

  return SVN_NO_ERROR;
}

/* Parse the description of a representation from STRING and store it
   into *REP_P.  If the representation is mutable (the revision is
   given as -1), then use TXN_ID for the representation's txn_id
   field.  If MUTABLE_REP_TRUNCATED is true, then this representation
   is for property or directory contents, and no information will be
   expected except the "-1" revision number for a mutable
   representation.  Allocate *REP_P in POOL. */
svn_error_t *
read_rep_offsets_body(representation_t **rep_p,
                      char *string,
                      const char *txn_id,
                      svn_boolean_t mutable_rep_truncated,
                      apr_pool_t *pool)
{
  representation_t *rep;
  char *str;
  apr_int64_t val;

  rep = apr_pcalloc(pool, sizeof(*rep));
  *rep_p = rep;

  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));


  rep->revision = SVN_STR_TO_REV(str);
  if (rep->revision == SVN_INVALID_REVNUM)
    {
      rep->txn_id = txn_id;
      if (mutable_rep_truncated)
        return SVN_NO_ERROR;
    }

  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  SVN_ERR(svn_cstring_atoi64(&val, str));
  rep->offset = (apr_off_t)val;

  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  SVN_ERR(svn_cstring_atoi64(&val, str));
  rep->size = (svn_filesize_t)val;

  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  SVN_ERR(svn_cstring_atoi64(&val, str));
  rep->expanded_size = (svn_filesize_t)val;

  /* Read in the MD5 hash. */
  str = svn_cstring_tokenize(" ", &string);
  if ((str == NULL) || (strlen(str) != (APR_MD5_DIGESTSIZE * 2)))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  SVN_ERR(svn_checksum_parse_hex(&rep->md5_checksum, svn_checksum_md5, str,
                                 pool));

  /* The remaining fields are only used for formats >= 4, so check that. */
  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return SVN_NO_ERROR;

  /* Read the SHA1 hash. */
  if (strlen(str) != (APR_SHA1_DIGESTSIZE * 2))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  SVN_ERR(svn_checksum_parse_hex(&rep->sha1_checksum, svn_checksum_sha1, str,
                                 pool));

  /* Read the uniquifier. */
  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  rep->uniquifier = apr_pstrdup(pool, str);

  return SVN_NO_ERROR;
}

/* Wrap read_rep_offsets_body(), extracting its TXN_ID from our NODEREV_ID,
   and adding an error message. */
svn_error_t *
read_rep_offsets(representation_t **rep_p,
                 char *string,
                 const svn_fs_id_t *noderev_id,
                 svn_boolean_t mutable_rep_truncated,
                 apr_pool_t *pool)
{
  svn_error_t *err;
  const char *txn_id;

  if (noderev_id)
    txn_id = svn_fs_fs__id_txn_id(noderev_id);
  else
    txn_id = NULL;

  err = read_rep_offsets_body(rep_p, string, txn_id, mutable_rep_truncated,
                              pool);
  if (err)
    {
      const svn_string_t *id_unparsed = svn_fs_fs__id_unparse(noderev_id, pool);
      const char *where;
      where = apr_psprintf(pool,
                           _("While reading representation offsets "
                             "for node-revision '%s':"),
                           noderev_id ? id_unparsed->data : "(null)");

      return svn_error_quick_wrap(err, where);
    }
  else
    return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_noderev(node_revision_t **noderev_p,
                        svn_stream_t *stream,
                        apr_pool_t *pool)
{
  apr_hash_t *headers;
  node_revision_t *noderev;
  char *value;
  const char *noderev_id;

  SVN_ERR(read_header_block(&headers, stream, pool));

  noderev = apr_pcalloc(pool, sizeof(*noderev));

  /* Read the node-rev id. */
  value = apr_hash_get(headers, HEADER_ID, APR_HASH_KEY_STRING);
  if (value == NULL)
      /* ### More information: filename/offset coordinates */
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Missing id field in node-rev"));

  SVN_ERR(svn_stream_close(stream));

  noderev->id = svn_fs_fs__id_parse(value, strlen(value), pool);
  noderev_id = value; /* for error messages later */

  /* Read the type. */
  value = apr_hash_get(headers, HEADER_TYPE, APR_HASH_KEY_STRING);

  if ((value == NULL) ||
      (strcmp(value, KIND_FILE) != 0 && strcmp(value, KIND_DIR)))
    /* ### s/kind/type/ */
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Missing kind field in node-rev '%s'"),
                             noderev_id);

  noderev->kind = (strcmp(value, KIND_FILE) == 0) ? svn_node_file
    : svn_node_dir;

  /* Read the 'count' field. */
  value = apr_hash_get(headers, HEADER_COUNT, APR_HASH_KEY_STRING);
  if (value)
    SVN_ERR(svn_cstring_atoi(&noderev->predecessor_count, value));
  else
    noderev->predecessor_count = 0;

  /* Get the properties location. */
  value = apr_hash_get(headers, HEADER_PROPS, APR_HASH_KEY_STRING);
  if (value)
    {
      SVN_ERR(read_rep_offsets(&noderev->prop_rep, value,
                               noderev->id, TRUE, pool));
    }

  /* Get the data location. */
  value = apr_hash_get(headers, HEADER_TEXT, APR_HASH_KEY_STRING);
  if (value)
    {
      SVN_ERR(read_rep_offsets(&noderev->data_rep, value,
                               noderev->id,
                               (noderev->kind == svn_node_dir), pool));
    }

  /* Get the created path. */
  value = apr_hash_get(headers, HEADER_CPATH, APR_HASH_KEY_STRING);
  if (value == NULL)
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Missing cpath field in node-rev '%s'"),
                               noderev_id);
    }
  else
    {
      noderev->created_path = apr_pstrdup(pool, value);
    }

  /* Get the predecessor ID. */
  value = apr_hash_get(headers, HEADER_PRED, APR_HASH_KEY_STRING);
  if (value)
    noderev->predecessor_id = svn_fs_fs__id_parse(value, strlen(value),
                                                  pool);

  /* Get the copyroot. */
  value = apr_hash_get(headers, HEADER_COPYROOT, APR_HASH_KEY_STRING);
  if (value == NULL)
    {
      noderev->copyroot_path = apr_pstrdup(pool, noderev->created_path);
      noderev->copyroot_rev = svn_fs_fs__id_rev(noderev->id);
    }
  else
    {
      char *str;

      str = svn_cstring_tokenize(" ", &value);
      if (str == NULL)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 _("Malformed copyroot line in node-rev '%s'"),
                                 noderev_id);

      noderev->copyroot_rev = SVN_STR_TO_REV(str);

      if (*value == '\0')
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 _("Malformed copyroot line in node-rev '%s'"),
                                 noderev_id);
      noderev->copyroot_path = apr_pstrdup(pool, value);
    }

  /* Get the copyfrom. */
  value = apr_hash_get(headers, HEADER_COPYFROM, APR_HASH_KEY_STRING);
  if (value == NULL)
    {
      noderev->copyfrom_path = NULL;
      noderev->copyfrom_rev = SVN_INVALID_REVNUM;
    }
  else
    {
      char *str = svn_cstring_tokenize(" ", &value);
      if (str == NULL)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 _("Malformed copyfrom line in node-rev '%s'"),
                                 noderev_id);

      noderev->copyfrom_rev = SVN_STR_TO_REV(str);

      if (*value == 0)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 _("Malformed copyfrom line in node-rev '%s'"),
                                 noderev_id);
      noderev->copyfrom_path = apr_pstrdup(pool, value);
    }

  /* Get whether this is a fresh txn root. */
  value = apr_hash_get(headers, HEADER_FRESHTXNRT, APR_HASH_KEY_STRING);
  noderev->is_fresh_txn_root = (value != NULL);

  /* Get the mergeinfo count. */
  value = apr_hash_get(headers, HEADER_MINFO_CNT, APR_HASH_KEY_STRING);
  if (value)
    SVN_ERR(svn_cstring_atoi64(&noderev->mergeinfo_count, value));
  else
    noderev->mergeinfo_count = 0;

  /* Get whether *this* node has mergeinfo. */
  value = apr_hash_get(headers, HEADER_MINFO_HERE, APR_HASH_KEY_STRING);
  noderev->has_mergeinfo = (value != NULL);

  *noderev_p = noderev;

  return SVN_NO_ERROR;
}


/* Return a formatted string, compatible with filesystem format FORMAT,
   that represents the location of representation REP.  If
   MUTABLE_REP_TRUNCATED is given, the rep is for props or dir contents,
   and only a "-1" revision number will be given for a mutable rep.
   If MAY_BE_CORRUPT is true, guard for NULL when constructing the string.
   Perform the allocation from POOL.  */
const char *
representation_string(representation_t *rep,
                      int format,
                      svn_boolean_t mutable_rep_truncated,
                      svn_boolean_t may_be_corrupt,
                      apr_pool_t *pool)
{
  if (rep->txn_id && mutable_rep_truncated)
    return "-1";

#define DISPLAY_MAYBE_NULL_CHECKSUM(checksum)          \
  ((may_be_corrupt == FALSE || (checksum) != NULL)     \
   ? svn_checksum_to_cstring_display((checksum), pool) \
   : "(null)")

  if (format < SVN_FS_FS__MIN_REP_SHARING_FORMAT || rep->sha1_checksum == NULL)
    return apr_psprintf(pool, "%ld %" APR_OFF_T_FMT " %" SVN_FILESIZE_T_FMT
                        " %" SVN_FILESIZE_T_FMT " %s",
                        rep->revision, rep->offset, rep->size,
                        rep->expanded_size,
                        DISPLAY_MAYBE_NULL_CHECKSUM(rep->md5_checksum));

  return apr_psprintf(pool, "%ld %" APR_OFF_T_FMT " %" SVN_FILESIZE_T_FMT
                      " %" SVN_FILESIZE_T_FMT " %s %s %s",
                      rep->revision, rep->offset, rep->size,
                      rep->expanded_size,
                      DISPLAY_MAYBE_NULL_CHECKSUM(rep->md5_checksum),
                      DISPLAY_MAYBE_NULL_CHECKSUM(rep->sha1_checksum),
                      rep->uniquifier);

#undef DISPLAY_MAYBE_NULL_CHECKSUM

}


svn_error_t *
svn_fs_fs__write_noderev(svn_stream_t *outfile,
                         node_revision_t *noderev,
                         int format,
                         svn_boolean_t include_mergeinfo,
                         apr_pool_t *pool)
{
  SVN_ERR(svn_stream_printf(outfile, pool, HEADER_ID ": %s\n",
                            svn_fs_fs__id_unparse(noderev->id,
                                                  pool)->data));

  SVN_ERR(svn_stream_printf(outfile, pool, HEADER_TYPE ": %s\n",
                            (noderev->kind == svn_node_file) ?
                            KIND_FILE : KIND_DIR));

  if (noderev->predecessor_id)
    SVN_ERR(svn_stream_printf(outfile, pool, HEADER_PRED ": %s\n",
                              svn_fs_fs__id_unparse(noderev->predecessor_id,
                                                    pool)->data));

  SVN_ERR(svn_stream_printf(outfile, pool, HEADER_COUNT ": %d\n",
                            noderev->predecessor_count));

  if (noderev->data_rep)
    SVN_ERR(svn_stream_printf(outfile, pool, HEADER_TEXT ": %s\n",
                              representation_string(noderev->data_rep,
                                                    format,
                                                    (noderev->kind
                                                     == svn_node_dir),
                                                    FALSE,
                                                    pool)));

  if (noderev->prop_rep)
    SVN_ERR(svn_stream_printf(outfile, pool, HEADER_PROPS ": %s\n",
                              representation_string(noderev->prop_rep, format,
                                                    TRUE, FALSE, pool)));

  SVN_ERR(svn_stream_printf(outfile, pool, HEADER_CPATH ": %s\n",
                            noderev->created_path));

  if (noderev->copyfrom_path)
    SVN_ERR(svn_stream_printf(outfile, pool, HEADER_COPYFROM ": %ld"
                              " %s\n",
                              noderev->copyfrom_rev,
                              noderev->copyfrom_path));

  if ((noderev->copyroot_rev != svn_fs_fs__id_rev(noderev->id)) ||
      (strcmp(noderev->copyroot_path, noderev->created_path) != 0))
    SVN_ERR(svn_stream_printf(outfile, pool, HEADER_COPYROOT ": %ld"
                              " %s\n",
                              noderev->copyroot_rev,
                              noderev->copyroot_path));

  if (noderev->is_fresh_txn_root)
    SVN_ERR(svn_stream_puts(outfile, HEADER_FRESHTXNRT ": y\n"));

  if (include_mergeinfo)
    {
      if (noderev->mergeinfo_count > 0)
        SVN_ERR(svn_stream_printf(outfile, pool, HEADER_MINFO_CNT ": %"
                                  APR_INT64_T_FMT "\n",
                                  noderev->mergeinfo_count));

      if (noderev->has_mergeinfo)
        SVN_ERR(svn_stream_puts(outfile, HEADER_MINFO_HERE ": y\n"));
    }

  return svn_stream_puts(outfile, "\n");
}

/* Read the next line from file FILE and parse it as a text
   representation entry.  Return the parsed entry in *REP_ARGS_P.
   Perform all allocations in POOL. */
svn_error_t *
read_rep_line(rep_args_t **rep_args_p,
              svn_stream_t *stream,
              apr_pool_t *pool)
{
  svn_stringbuf_t *buffer;
  rep_args_t *rep_args;
  char *str, *last_str;
  apr_int64_t val;
  svn_boolean_t eol = FALSE;

  SVN_ERR(svn_stream_readline(stream, &buffer, "\n", &eol, pool));

  rep_args = apr_pcalloc(pool, sizeof(*rep_args));
  rep_args->is_delta = FALSE;

  if (strcmp(buffer->data, REP_PLAIN) == 0)
    {
      *rep_args_p = rep_args;
      return SVN_NO_ERROR;
    }

  if (strcmp(buffer->data, REP_DELTA) == 0)
    {
      /* This is a delta against the empty stream. */
      rep_args->is_delta = TRUE;
      rep_args->is_delta_vs_empty = TRUE;
      *rep_args_p = rep_args;
      return SVN_NO_ERROR;
    }

  rep_args->is_delta = TRUE;
  rep_args->is_delta_vs_empty = FALSE;

  /* We have hopefully a DELTA vs. a non-empty base revision. */
  last_str = buffer->data;
  str = svn_cstring_tokenize(" ", &last_str);
  if (! str || (strcmp(str, REP_DELTA) != 0))
    goto error;

  str = svn_cstring_tokenize(" ", &last_str);
  if (! str)
    goto error;
  rep_args->base_revision = SVN_STR_TO_REV(str);

  str = svn_cstring_tokenize(" ", &last_str);
  if (! str)
    goto error;
  SVN_ERR(svn_cstring_atoi64(&val, str));
  rep_args->base_offset = (apr_off_t)val;

  str = svn_cstring_tokenize(" ", &last_str);
  if (! str)
    goto error;
  SVN_ERR(svn_cstring_atoi64(&val, str));
  rep_args->base_length = (svn_filesize_t)val;

  *rep_args_p = rep_args;
  return SVN_NO_ERROR;

 error:
  return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                           _("Malformed representation header"));
}

svn_error_t *
write_rep_line(rep_args_t *rep_args,
               svn_stream_t *stream,
               apr_pool_t *pool)
{
  const char *header;
  
  if (rep_args->is_delta)
    {
      header = REP_PLAIN "\n";
    }
  else if (rep_args->is_delta_vs_empty)
    {
      header = REP_DELTA "\n";
    }
  else
    {
      header = apr_psprintf(pool, REP_DELTA " %ld %" APR_OFF_T_FMT " %"
                            SVN_FILESIZE_T_FMT "\n",
                            rep_args->base_revision, rep_args->base_offset,
                            rep_args->base_length);
    }

  return svn_error_trace(svn_stream_puts(stream, header));
}

/* Read the next entry in the changes record from file FILE and store
   the resulting change in *CHANGE_P.  If there is no next record,
   store NULL there.  Perform all allocations from POOL. */
static svn_error_t *
read_change(change_t **change_p,
            apr_file_t *file,
            apr_pool_t *pool)
{
  char buf[MAX_CHANGE_LINE_LEN];
  apr_size_t len = sizeof(buf);
  change_t *change;
  char *str, *last_str = buf, *kind_str;
  svn_error_t *err;

  /* Default return value. */
  *change_p = NULL;

  err = svn_io_read_length_line(file, buf, &len, pool);

  /* Check for a blank line. */
  if (err || (len == 0))
    {
      if (err && APR_STATUS_IS_EOF(err->apr_err))
        {
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
      if ((len == 0) && (! err))
        return SVN_NO_ERROR;
      return svn_error_trace(err);
    }

  change = apr_pcalloc(pool, sizeof(*change));

  /* Get the node-id of the change. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  change->noderev_id = svn_fs_fs__id_parse(str, strlen(str), pool);
  if (change->noderev_id == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  /* Get the change type. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  /* Don't bother to check the format number before looking for
   * node-kinds: just read them if you find them. */
  change->node_kind = svn_node_unknown;
  kind_str = strchr(str, '-');
  if (kind_str)
    {
      /* Cap off the end of "str" (the action). */
      *kind_str = '\0';
      kind_str++;
      if (strcmp(kind_str, KIND_FILE) == 0)
        change->node_kind = svn_node_file;
      else if (strcmp(kind_str, KIND_DIR) == 0)
        change->node_kind = svn_node_dir;
      else
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid changes line in rev-file"));
    }

  if (strcmp(str, ACTION_MODIFY) == 0)
    {
      change->kind = svn_fs_path_change_modify;
    }
  else if (strcmp(str, ACTION_ADD) == 0)
    {
      change->kind = svn_fs_path_change_add;
    }
  else if (strcmp(str, ACTION_DELETE) == 0)
    {
      change->kind = svn_fs_path_change_delete;
    }
  else if (strcmp(str, ACTION_REPLACE) == 0)
    {
      change->kind = svn_fs_path_change_replace;
    }
  else if (strcmp(str, ACTION_RESET) == 0)
    {
      change->kind = svn_fs_path_change_reset;
    }
  else
    {
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Invalid change kind in rev file"));
    }

  /* Get the text-mod flag. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  if (strcmp(str, FLAG_TRUE) == 0)
    {
      change->text_mod = TRUE;
    }
  else if (strcmp(str, FLAG_FALSE) == 0)
    {
      change->text_mod = FALSE;
    }
  else
    {
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Invalid text-mod flag in rev-file"));
    }

  /* Get the prop-mod flag. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  if (strcmp(str, FLAG_TRUE) == 0)
    {
      change->prop_mod = TRUE;
    }
  else if (strcmp(str, FLAG_FALSE) == 0)
    {
      change->prop_mod = FALSE;
    }
  else
    {
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Invalid prop-mod flag in rev-file"));
    }

  /* Get the changed path. */
  change->path = apr_pstrdup(pool, last_str);


  /* Read the next line, the copyfrom line. */
  len = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(file, buf, &len, pool));

  if (len == 0)
    {
      change->copyfrom_rev = SVN_INVALID_REVNUM;
      change->copyfrom_path = NULL;
    }
  else
    {
      last_str = buf;
      str = svn_cstring_tokenize(" ", &last_str);
      if (! str)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid changes line in rev-file"));
      change->copyfrom_rev = SVN_STR_TO_REV(str);

      if (! last_str)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid changes line in rev-file"));

      change->copyfrom_path = apr_pstrdup(pool, last_str);
    }

  *change_p = change;

  return SVN_NO_ERROR;
}

/* Fetch all the changes from FILE and store them in *CHANGES.  Do all
   allocations in POOL. */
svn_error_t *
read_all_changes(apr_array_header_t **changes,
                 apr_file_t *file,
                 apr_pool_t *pool)
{
  change_t *change;

  /* pre-allocate enough room for most change lists
     (will be auto-expanded as necessary) */
  *changes = apr_array_make(pool, 30, sizeof(change_t *));
  
  SVN_ERR(read_change(&change, file, pool));
  while (change)
    {
      APR_ARRAY_PUSH(*changes, change_t*) = change;
      SVN_ERR(read_change(&change, file, pool));
    }

  return SVN_NO_ERROR;
}

/* Write a single change entry, path PATH, change CHANGE, and copyfrom
   string COPYFROM, into the file specified by FILE.  Only include the
   node kind field if INCLUDE_NODE_KIND is true.  All temporary
   allocations are in POOL. */
svn_error_t *
write_change_entry(svn_stream_t *stream,
                   const char *path,
                   svn_fs_path_change2_t *change,
                   svn_boolean_t include_node_kind,
                   apr_pool_t *pool)
{
  const char *idstr, *buf;
  const char *change_string = NULL;
  const char *kind_string = "";

  switch (change->change_kind)
    {
    case svn_fs_path_change_modify:
      change_string = ACTION_MODIFY;
      break;
    case svn_fs_path_change_add:
      change_string = ACTION_ADD;
      break;
    case svn_fs_path_change_delete:
      change_string = ACTION_DELETE;
      break;
    case svn_fs_path_change_replace:
      change_string = ACTION_REPLACE;
      break;
    case svn_fs_path_change_reset:
      change_string = ACTION_RESET;
      break;
    default:
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Invalid change type %d"),
                               change->change_kind);
    }

  if (change->node_rev_id)
    idstr = svn_fs_fs__id_unparse(change->node_rev_id, pool)->data;
  else
    idstr = ACTION_RESET;

  if (include_node_kind)
    {
      SVN_ERR_ASSERT(change->node_kind == svn_node_dir
                     || change->node_kind == svn_node_file);
      kind_string = apr_psprintf(pool, "-%s",
                                 change->node_kind == svn_node_dir
                                 ? KIND_DIR : KIND_FILE);
    }
  buf = apr_psprintf(pool, "%s %s%s %s %s %s\n",
                     idstr, change_string, kind_string,
                     change->text_mod ? FLAG_TRUE : FLAG_FALSE,
                     change->prop_mod ? FLAG_TRUE : FLAG_FALSE,
                     path);

  SVN_ERR(svn_stream_puts(stream, buf));

  if (SVN_IS_VALID_REVNUM(change->copyfrom_rev))
    {
      buf = apr_psprintf(pool, "%ld %s", change->copyfrom_rev,
                         change->copyfrom_path);
      SVN_ERR(svn_stream_puts(stream, buf));
    }

  return svn_error_trace(svn_stream_puts(stream, "\n"));
}

/* Write the changed path info from transaction TXN_ID in filesystem
   FS to the permanent rev-file FILE.  *OFFSET_P is set the to offset
   in the file of the beginning of this information.  Perform
   temporary allocations in POOL. */
svn_error_t *
write_changed_path_info(svn_stream_t *stream,
                        svn_fs_t *fs,
                        apr_hash_t *changed_paths,
                        apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_boolean_t include_node_kinds =
      ffd->format >= SVN_FS_FS__MIN_KIND_IN_CHANGED_FORMAT;
  apr_array_header_t *sorted_changed_paths;
  int i;

  /* For the sake of the repository administrator sort the changes so
     that the final file is deterministic and repeatable, however the
     rest of the FSFS code doesn't require any particular order here. */
  sorted_changed_paths = svn_sort__hash(changed_paths,
                                        svn_sort_compare_items_lexically, pool);

  /* Iterate through the changed paths one at a time, and convert the
     temporary node-id into a permanent one for each change entry. */
  for (i = 0; i < sorted_changed_paths->nelts; ++i)
    {
      node_revision_t *noderev;
      const svn_fs_id_t *id;
      svn_fs_path_change2_t *change;
      const char *path;

      svn_pool_clear(iterpool);

      change = APR_ARRAY_IDX(sorted_changed_paths, i, svn_sort__item_t).value;
      path = APR_ARRAY_IDX(sorted_changed_paths, i, svn_sort__item_t).key;

      id = change->node_rev_id;

      /* If this was a delete of a mutable node, then it is OK to
         leave the change entry pointing to the non-existent temporary
         node, since it will never be used. */
      if ((change->change_kind != svn_fs_path_change_delete) &&
          (! svn_fs_fs__id_txn_id(id)))
        {
          SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, id, iterpool));

          /* noderev has the permanent node-id at this point, so we just
             substitute it for the temporary one. */
          change->node_rev_id = noderev->id;
        }

      /* Write out the new entry into the final rev-file. */
      SVN_ERR(write_change_entry(stream, path, change, include_node_kinds,
                                 iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Given an open revision file REV_FILE in FS for REV, locate the trailer that
   specifies the offset to the root node-id and to the changed path
   information.  Store the root node offset in *ROOT_OFFSET and the
   changed path offset in *CHANGES_OFFSET.  If either of these
   pointers is NULL, do nothing with it.

   If PACKED is true, REV_FILE should be a packed shard file.
   ### There is currently no such parameter.  This function assumes that
       is_packed_rev(FS, REV) will indicate whether REV_FILE is a packed
       file.  Therefore FS->fsap_data->min_unpacked_rev must not have been
       refreshed since REV_FILE was opened if there is a possibility that
       revision REV may have become packed since then.
       TODO: Take an IS_PACKED parameter instead, in order to remove this
       requirement.

   Allocate temporary variables from POOL. */
svn_error_t *
get_root_changes_offset(apr_off_t *root_offset,
                        apr_off_t *changes_offset,
                        apr_file_t *rev_file,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_off_t offset;
  apr_off_t rev_offset;
  char buf[64];
  int i, num_bytes;
  const char *str;
  apr_size_t len;
  apr_seek_where_t seek_relative;

  /* Determine where to seek to in the file.

     If we've got a pack file, we want to seek to the end of the desired
     revision.  But we don't track that, so we seek to the beginning of the
     next revision.

     Unless the next revision is in a different file, in which case, we can
     just seek to the end of the pack file -- just like we do in the
     non-packed case. */
  if (is_packed_rev(fs, rev) && ((rev + 1) % ffd->max_files_per_dir != 0))
    {
      SVN_ERR(svn_fs_fs__get_packed_offset(&offset, fs, rev + 1, pool));
      seek_relative = APR_SET;
    }
  else
    {
      seek_relative = APR_END;
      offset = 0;
    }

  /* Offset of the revision from the start of the pack file, if applicable. */
  if (is_packed_rev(fs, rev))
    SVN_ERR(svn_fs_fs__get_packed_offset(&rev_offset, fs, rev, pool));
  else
    rev_offset = 0;

  /* We will assume that the last line containing the two offsets
     will never be longer than 64 characters. */
  SVN_ERR(svn_io_file_seek(rev_file, seek_relative, &offset, pool));

  offset -= sizeof(buf);
  SVN_ERR(svn_io_file_seek(rev_file, APR_SET, &offset, pool));

  /* Read in this last block, from which we will identify the last line. */
  len = sizeof(buf);
  SVN_ERR(svn_io_file_read(rev_file, buf, &len, pool));

  /* This cast should be safe since the maximum amount read, 64, will
     never be bigger than the size of an int. */
  num_bytes = (int) len;

  /* The last byte should be a newline. */
  if (buf[num_bytes - 1] != '\n')
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Revision file (r%ld) lacks trailing newline"),
                               rev);
    }

  /* Look for the next previous newline. */
  for (i = num_bytes - 2; i >= 0; i--)
    {
      if (buf[i] == '\n')
        break;
    }

  if (i < 0)
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Final line in revision file (r%ld) longer "
                                 "than 64 characters"),
                               rev);
    }

  i++;
  str = &buf[i];

  /* find the next space */
  for ( ; i < (num_bytes - 2) ; i++)
    if (buf[i] == ' ')
      break;

  if (i == (num_bytes - 2))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Final line in revision file r%ld missing space"),
                             rev);

  if (root_offset)
    {
      apr_int64_t val;

      buf[i] = '\0';
      SVN_ERR(svn_cstring_atoi64(&val, str));
      *root_offset = rev_offset + (apr_off_t)val;
    }

  i++;
  str = &buf[i];

  /* find the next newline */
  for ( ; i < num_bytes; i++)
    if (buf[i] == '\n')
      break;

  if (changes_offset)
    {
      apr_int64_t val;

      buf[i] = '\0';
      SVN_ERR(svn_cstring_atoi64(&val, str));
      *changes_offset = rev_offset + (apr_off_t)val;
    }

  return SVN_NO_ERROR;
}

