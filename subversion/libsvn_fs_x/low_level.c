/* low_level.c --- low level r/w access to fs_x file structures
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
#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "private/svn_sorts_private.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"

#include "../libsvn_fs/fs-loader.h"

#include "low_level.h"
#include "util.h"
#include "pack.h"
#include "cached_data.h"

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

/* Kinds of representation. */
#define REP_DELTA          "DELTA"

/* An arbitrary maximum path length, so clients can't run us out of memory
 * by giving us arbitrarily large paths. */
#define FSX_MAX_PATH_LEN 4096

/* The 256 is an arbitrary size large enough to hold the node id and the
 * various flags. */
#define MAX_CHANGE_LINE_LEN FSX_MAX_PATH_LEN + 256

svn_error_t *
svn_fs_x__parse_revision_trailer(apr_off_t *root_offset,
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

svn_stringbuf_t *
svn_fs_x__unparse_revision_trailer(apr_off_t root_offset,
                                   apr_off_t changes_offset,
                                   apr_pool_t *pool)
{
  return svn_stringbuf_createf(pool,
                               "%" APR_OFF_T_FMT " %" APR_OFF_T_FMT "\n",
                               root_offset,
                               changes_offset);
}

/* Given a revision file FILE that has been pre-positioned at the
   beginning of a Node-Rev header block, read in that header block and
   store it in the apr_hash_t HEADERS.  All allocations will be from
   POOL. */
static svn_error_t *
read_header_block(apr_hash_t **headers,
                  svn_stream_t *stream,
                  apr_pool_t *pool)
{
  *headers = svn_hash__make(pool);

  while (1)
    {
      svn_stringbuf_t *header_str;
      const char *name, *value;
      apr_ssize_t i = 0;
      apr_ssize_t name_len;
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
      name_len = i;

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
      apr_hash_set(*headers, name, name_len, value);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__parse_representation(representation_t **rep_p,
                               svn_stringbuf_t *text,
                               apr_pool_t *pool)
{
  representation_t *rep;
  char *str;
  apr_int64_t val;
  char *string = text->data;
  svn_checksum_t *checksum;

  rep = apr_pcalloc(pool, sizeof(*rep));
  *rep_p = rep;

  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  SVN_ERR(svn_cstring_atoi64(&rep->id.change_set, str));

  /* while in transactions, it is legal to simply write "-1" */
  if (rep->id.change_set == -1)
    return SVN_NO_ERROR;

  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    {
      if (rep->id.change_set == SVN_FS_X__INVALID_CHANGE_SET)
        return SVN_NO_ERROR;

      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Malformed text representation offset line in node-rev"));
    }

  SVN_ERR(svn_cstring_atoi64(&val, str));
  rep->id.number = (apr_off_t)val;

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

  SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_md5, str, pool));
  if (checksum)
    memcpy(rep->md5_digest, checksum->digest, sizeof(rep->md5_digest));

  /* The remaining fields are only used for formats >= 4, so check that. */
  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return SVN_NO_ERROR;

  /* Read the SHA1 hash. */
  if (strlen(str) != (APR_SHA1_DIGESTSIZE * 2))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_sha1, str, pool));
  rep->has_sha1 = checksum != NULL;
  if (checksum)
    memcpy(rep->sha1_digest, checksum->digest, sizeof(rep->sha1_digest));

  return SVN_NO_ERROR;
}

/* Wrap read_rep_offsets_body(), extracting its TXN_ID from our NODEREV_ID,
   and adding an error message. */
static svn_error_t *
read_rep_offsets(representation_t **rep_p,
                 char *string,
                 const svn_fs_id_t *noderev_id,
                 apr_pool_t *pool)
{
  svn_error_t *err
    = svn_fs_x__parse_representation(rep_p,
                                     svn_stringbuf_create_wrap(string, pool),
                                     pool);
  if (err)
    {
      const svn_string_t *id_unparsed = svn_fs_x__id_unparse(noderev_id, pool);
      const char *where;
      where = apr_psprintf(pool,
                           _("While reading representation offsets "
                             "for node-revision '%s':"),
                           noderev_id ? id_unparsed->data : "(null)");

      return svn_error_quick_wrap(err, where);
    }

  return SVN_NO_ERROR;
}

static const char *
auto_escape_path(const char *path,
                 apr_pool_t *pool)
{
  apr_size_t len = strlen(path);
  apr_size_t i;
  const char esc = '\x1b';

  for (i = 0; i < len; ++i)
    if (path[i] < ' ')
      {
        svn_stringbuf_t *escaped = svn_stringbuf_create_ensure(2 * len, pool);
        for (i = 0; i < len; ++i)
          if (path[i] < ' ')
            {
              svn_stringbuf_appendbyte(escaped, esc);
              svn_stringbuf_appendbyte(escaped, path[i] + 'A' - 1);
            }
          else
            {
              svn_stringbuf_appendbyte(escaped, path[i]);
            }

        return escaped->data;
      }
      
   return path;
}

static const char *
auto_unescape_path(const char *path,
                   apr_pool_t *pool)
{
  const char esc = '\x1b';
  if (strchr(path, esc))
    {
      apr_size_t len = strlen(path);
      apr_size_t i;

      svn_stringbuf_t *unescaped = svn_stringbuf_create_ensure(len, pool);
      for (i = 0; i < len; ++i)
        if (path[i] == esc)
          svn_stringbuf_appendbyte(unescaped, path[++i] + 1 - 'A');
        else
          svn_stringbuf_appendbyte(unescaped, path[i]);

      return unescaped->data;
    }

   return path;
}

svn_error_t *
svn_fs_x__read_noderev(node_revision_t **noderev_p,
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
  value = svn_hash_gets(headers, HEADER_ID);
  if (value == NULL)
      /* ### More information: filename/offset coordinates */
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Missing id field in node-rev"));

  SVN_ERR(svn_stream_close(stream));

  noderev->id = svn_fs_x__id_parse(value, strlen(value), pool);
  noderev_id = value; /* for error messages later */

  /* Read the type. */
  value = svn_hash_gets(headers, HEADER_TYPE);

  if ((value == NULL) ||
      (   strcmp(value, SVN_FS_X__KIND_FILE)
       && strcmp(value, SVN_FS_X__KIND_DIR)))
    /* ### s/kind/type/ */
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Missing kind field in node-rev '%s'"),
                             noderev_id);

  noderev->kind = (strcmp(value, SVN_FS_X__KIND_FILE) == 0)
                ? svn_node_file
                : svn_node_dir;

  /* Read the 'count' field. */
  value = svn_hash_gets(headers, HEADER_COUNT);
  if (value)
    SVN_ERR(svn_cstring_atoi(&noderev->predecessor_count, value));
  else
    noderev->predecessor_count = 0;

  /* Get the properties location. */
  value = svn_hash_gets(headers, HEADER_PROPS);
  if (value)
    {
      SVN_ERR(read_rep_offsets(&noderev->prop_rep, value,
                               noderev->id, pool));
    }

  /* Get the data location. */
  value = svn_hash_gets(headers, HEADER_TEXT);
  if (value)
    {
      SVN_ERR(read_rep_offsets(&noderev->data_rep, value,
                               noderev->id, pool));
    }

  /* Get the created path. */
  value = svn_hash_gets(headers, HEADER_CPATH);
  if (value == NULL)
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Missing cpath field in node-rev '%s'"),
                               noderev_id);
    }
  else
    {
      noderev->created_path = auto_unescape_path(value, pool);
    }

  /* Get the predecessor ID. */
  value = svn_hash_gets(headers, HEADER_PRED);
  if (value)
    noderev->predecessor_id = svn_fs_x__id_parse(value, strlen(value), pool);

  /* Get the copyroot. */
  value = svn_hash_gets(headers, HEADER_COPYROOT);
  if (value == NULL)
    {
      noderev->copyroot_path = noderev->created_path;
      noderev->copyroot_rev = svn_fs_x__id_rev(noderev->id);
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
      noderev->copyroot_path = auto_unescape_path(value, pool);
    }

  /* Get the copyfrom. */
  value = svn_hash_gets(headers, HEADER_COPYFROM);
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
      noderev->copyfrom_path = auto_unescape_path(value, pool);
    }

  /* Get whether this is a fresh txn root. */
  value = svn_hash_gets(headers, HEADER_FRESHTXNRT);
  noderev->is_fresh_txn_root = (value != NULL);

  /* Get the mergeinfo count. */
  value = svn_hash_gets(headers, HEADER_MINFO_CNT);
  if (value)
    SVN_ERR(svn_cstring_atoi64(&noderev->mergeinfo_count, value));
  else
    noderev->mergeinfo_count = 0;

  /* Get whether *this* node has mergeinfo. */
  value = svn_hash_gets(headers, HEADER_MINFO_HERE);
  noderev->has_mergeinfo = (value != NULL);

  *noderev_p = noderev;

  return SVN_NO_ERROR;
}

/* Return a textual representation of the DIGEST of given KIND.
 * If IS_NULL is TRUE, no digest is available.
 * Use POOL for allocations.
 */
static const char *
format_digest(const unsigned char *digest,
              svn_checksum_kind_t kind,
              svn_boolean_t is_null,
              apr_pool_t *pool)
{
  svn_checksum_t checksum;
  checksum.digest = digest;
  checksum.kind = kind;
  
  if (is_null)
    return "(null)";

  return svn_checksum_to_cstring_display(&checksum, pool);
}

svn_stringbuf_t *
svn_fs_x__unparse_representation(representation_t *rep,
                                 int format,
                                 svn_boolean_t mutable_rep_truncated,
                                 apr_pool_t *pool)
{
  if (!rep->has_sha1)
    return svn_stringbuf_createf
            (pool,
             "%" APR_INT64_T_FMT " %" APR_UINT64_T_FMT " %" SVN_FILESIZE_T_FMT
             " %" SVN_FILESIZE_T_FMT " %s",
             rep->id.change_set, rep->id.number, rep->size,
             rep->expanded_size,
             format_digest(rep->md5_digest, svn_checksum_md5, FALSE, pool));

  return svn_stringbuf_createf
          (pool,
           "%" APR_INT64_T_FMT " %" APR_UINT64_T_FMT " %" SVN_FILESIZE_T_FMT
           " %" SVN_FILESIZE_T_FMT " %s %s",
           rep->id.change_set, rep->id.number, rep->size,
           rep->expanded_size,
           format_digest(rep->md5_digest, svn_checksum_md5, FALSE, pool),
           format_digest(rep->sha1_digest, svn_checksum_sha1,
                         !rep->has_sha1, pool));
}


svn_error_t *
svn_fs_x__write_noderev(svn_stream_t *outfile,
                        node_revision_t *noderev,
                        int format,
                        apr_pool_t *pool)
{
  SVN_ERR(svn_stream_printf(outfile, pool, HEADER_ID ": %s\n",
                            svn_fs_x__id_unparse(noderev->id, pool)->data));

  SVN_ERR(svn_stream_printf(outfile, pool, HEADER_TYPE ": %s\n",
                            (noderev->kind == svn_node_file) ?
                            SVN_FS_X__KIND_FILE : SVN_FS_X__KIND_DIR));

  if (noderev->predecessor_id)
    SVN_ERR(svn_stream_printf(outfile, pool, HEADER_PRED ": %s\n",
                              svn_fs_x__id_unparse(noderev->predecessor_id,
                                                   pool)->data));

  SVN_ERR(svn_stream_printf(outfile, pool, HEADER_COUNT ": %d\n",
                            noderev->predecessor_count));

  if (noderev->data_rep)
    SVN_ERR(svn_stream_printf(outfile, pool, HEADER_TEXT ": %s\n",
                              svn_fs_x__unparse_representation
                                (noderev->data_rep,
                                 format,
                                 noderev->kind == svn_node_dir,
                                 pool)->data));

  if (noderev->prop_rep)
    SVN_ERR(svn_stream_printf(outfile, pool, HEADER_PROPS ": %s\n",
                              svn_fs_x__unparse_representation
                                (noderev->prop_rep, format,
                                 TRUE, pool)->data));

  SVN_ERR(svn_stream_printf(outfile, pool, HEADER_CPATH ": %s\n",
                            auto_escape_path(noderev->created_path, pool)));

  if (noderev->copyfrom_path)
    SVN_ERR(svn_stream_printf(outfile, pool, HEADER_COPYFROM ": %ld"
                              " %s\n",
                              noderev->copyfrom_rev,
                              auto_escape_path(noderev->copyfrom_path, pool)));

  if ((noderev->copyroot_rev != svn_fs_x__id_rev(noderev->id)) ||
      (strcmp(noderev->copyroot_path, noderev->created_path) != 0))
    SVN_ERR(svn_stream_printf(outfile, pool, HEADER_COPYROOT ": %ld"
                              " %s\n",
                              noderev->copyroot_rev,
                              auto_escape_path(noderev->copyroot_path, pool)));

  if (noderev->is_fresh_txn_root)
    SVN_ERR(svn_stream_puts(outfile, HEADER_FRESHTXNRT ": y\n"));

  if (noderev->mergeinfo_count > 0)
    SVN_ERR(svn_stream_printf(outfile, pool, HEADER_MINFO_CNT ": %"
                              APR_INT64_T_FMT "\n",
                              noderev->mergeinfo_count));

  if (noderev->has_mergeinfo)
    SVN_ERR(svn_stream_puts(outfile, HEADER_MINFO_HERE ": y\n"));

  return svn_stream_puts(outfile, "\n");
}

svn_error_t *
svn_fs_x__read_rep_header(svn_fs_x__rep_header_t **header,
                          svn_stream_t *stream,
                          apr_pool_t *pool)
{
  svn_stringbuf_t *buffer;
  char *str, *last_str;
  apr_int64_t val;
  svn_boolean_t eol = FALSE;

  SVN_ERR(svn_stream_readline(stream, &buffer, "\n", &eol, pool));

  *header = apr_pcalloc(pool, sizeof(**header));
  (*header)->header_size = buffer->len + 1;
  if (strcmp(buffer->data, REP_DELTA) == 0)
    {
      /* This is a delta against the empty stream. */
      (*header)->type = svn_fs_x__rep_self_delta;
      return SVN_NO_ERROR;
    }

  (*header)->type = svn_fs_x__rep_delta;

  /* We have hopefully a DELTA vs. a non-empty base revision. */
  last_str = buffer->data;
  str = svn_cstring_tokenize(" ", &last_str);
  if (! str || (strcmp(str, REP_DELTA) != 0))
    goto error;

  str = svn_cstring_tokenize(" ", &last_str);
  if (! str)
    goto error;
  (*header)->base_revision = SVN_STR_TO_REV(str);

  str = svn_cstring_tokenize(" ", &last_str);
  if (! str)
    goto error;
  SVN_ERR(svn_cstring_atoi64(&val, str));
  (*header)->base_item_index = (apr_off_t)val;

  str = svn_cstring_tokenize(" ", &last_str);
  if (! str)
    goto error;
  SVN_ERR(svn_cstring_atoi64(&val, str));
  (*header)->base_length = (svn_filesize_t)val;

  return SVN_NO_ERROR;

 error:
  return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                           _("Malformed representation header"));
}

svn_error_t *
svn_fs_x__write_rep_header(svn_fs_x__rep_header_t *header,
                           svn_stream_t *stream,
                           apr_pool_t *pool)
{
  const char *text;
  
  switch (header->type)
    {
      case svn_fs_x__rep_self_delta:
        text = REP_DELTA "\n";
        break;

      default:
        text = apr_psprintf(pool, REP_DELTA " %ld %" APR_OFF_T_FMT " %"
                            SVN_FILESIZE_T_FMT "\n",
                            header->base_revision, header->base_item_index,
                            header->base_length);
    }

  return svn_error_trace(svn_stream_puts(stream, text));
}

/* Read the next entry in the changes record from file FILE and store
   the resulting change in *CHANGE_P.  If there is no next record,
   store NULL there.  Perform all allocations from POOL. */
static svn_error_t *
read_change(change_t **change_p,
            svn_stream_t *stream,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  svn_boolean_t eof = TRUE;
  change_t *change;
  char *str, *last_str, *kind_str;
  svn_fs_path_change2_t *info;

  /* Default return value. */
  *change_p = NULL;

  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));

  /* Check for a blank line. */
  if (eof || (line->len == 0))
    return SVN_NO_ERROR;

  change = apr_pcalloc(result_pool, sizeof(*change));
  info = &change->info;
  last_str = line->data;

  /* Get the node-id of the change. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  info->node_rev_id = svn_fs_x__id_parse(str, strlen(str), result_pool);
  if (info->node_rev_id == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  /* Get the change type. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  /* Don't bother to check the format number before looking for
   * node-kinds: just read them if you find them. */
  info->node_kind = svn_node_unknown;
  kind_str = strchr(str, '-');
  if (kind_str)
    {
      /* Cap off the end of "str" (the action). */
      *kind_str = '\0';
      kind_str++;
      if (strcmp(kind_str, SVN_FS_X__KIND_FILE) == 0)
        info->node_kind = svn_node_file;
      else if (strcmp(kind_str, SVN_FS_X__KIND_DIR) == 0)
        info->node_kind = svn_node_dir;
      else
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid changes line in rev-file"));
    }

  if (strcmp(str, ACTION_MODIFY) == 0)
    {
      info->change_kind = svn_fs_path_change_modify;
    }
  else if (strcmp(str, ACTION_ADD) == 0)
    {
      info->change_kind = svn_fs_path_change_add;
    }
  else if (strcmp(str, ACTION_DELETE) == 0)
    {
      info->change_kind = svn_fs_path_change_delete;
    }
  else if (strcmp(str, ACTION_REPLACE) == 0)
    {
      info->change_kind = svn_fs_path_change_replace;
    }
  else if (strcmp(str, ACTION_RESET) == 0)
    {
      info->change_kind = svn_fs_path_change_reset;
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
      info->text_mod = TRUE;
    }
  else if (strcmp(str, FLAG_FALSE) == 0)
    {
      info->text_mod = FALSE;
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
      info->prop_mod = TRUE;
    }
  else if (strcmp(str, FLAG_FALSE) == 0)
    {
      info->prop_mod = FALSE;
    }
  else
    {
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Invalid prop-mod flag in rev-file"));
    }

  /* Get the mergeinfo-mod flag. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  if (strcmp(str, FLAG_TRUE) == 0)
    {
      info->mergeinfo_mod = svn_tristate_true;
    }
  else if (strcmp(str, FLAG_FALSE) == 0)
    {
      info->mergeinfo_mod = svn_tristate_false;
    }
  else
    {
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Invalid mergeinfo-mod flag in rev-file"));
    }

  /* Get the changed path. */
  change->path.data = auto_unescape_path(apr_pstrmemdup(result_pool,
                                                        last_str,
                                                        strlen(last_str)),
                                         result_pool);
  change->path.len = strlen(change->path.data);

  /* Read the next line, the copyfrom line. */
  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, result_pool));
  info->copyfrom_known = TRUE;
  if (eof || line->len == 0)
    {
      info->copyfrom_rev = SVN_INVALID_REVNUM;
      info->copyfrom_path = NULL;
    }
  else
    {
      last_str = line->data;
      str = svn_cstring_tokenize(" ", &last_str);
      if (! str)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid changes line in rev-file"));
      info->copyfrom_rev = SVN_STR_TO_REV(str);

      if (! last_str)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid changes line in rev-file"));

      info->copyfrom_path = auto_unescape_path(last_str, result_pool);
    }

  *change_p = change;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__read_changes(apr_array_header_t **changes,
                       svn_stream_t *stream,
                       apr_pool_t *pool)
{
  change_t *change;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* pre-allocate enough room for most change lists
     (will be auto-expanded as necessary) */
  *changes = apr_array_make(pool, 30, sizeof(change_t *));

  SVN_ERR(read_change(&change, stream, pool, iterpool));
  while (change)
    {
      APR_ARRAY_PUSH(*changes, change_t*) = change;
      SVN_ERR(read_change(&change, stream, pool, iterpool));
      svn_pool_clear(iterpool);
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__read_changes_incrementally(svn_stream_t *stream,
                                     svn_fs_x__change_receiver_t
                                       change_receiver,
                                     void *change_receiver_baton,
                                     apr_pool_t *pool)
{
  change_t *change;
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(pool);
  do
    {
      svn_pool_clear(iterpool);

      SVN_ERR(read_change(&change, stream, iterpool, iterpool));
      if (change)
        SVN_ERR(change_receiver(change_receiver_baton, change, iterpool));
    }
  while (change);
  svn_pool_destroy(iterpool);
  
  return SVN_NO_ERROR;
}

/* Write a single change entry, path PATH, change CHANGE, and copyfrom
   string COPYFROM, into the file specified by FILE.  All temporary
   allocations are in POOL. */
static svn_error_t *
write_change_entry(svn_stream_t *stream,
                   const char *path,
                   svn_fs_path_change2_t *change,
                   apr_pool_t *pool)
{
  const char *idstr;
  const char *change_string = NULL;
  const char *kind_string = "";
  svn_stringbuf_t *buf;
  apr_size_t len;

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
    idstr = svn_fs_x__id_unparse(change->node_rev_id, pool)->data;
  else
    idstr = ACTION_RESET;

  SVN_ERR_ASSERT(change->node_kind == svn_node_dir
                  || change->node_kind == svn_node_file);
  kind_string = apr_psprintf(pool, "-%s",
                              change->node_kind == svn_node_dir
                              ? SVN_FS_X__KIND_DIR
                              : SVN_FS_X__KIND_FILE);
  buf = svn_stringbuf_createf(pool, "%s %s%s %s %s %s %s\n",
                              idstr, change_string, kind_string,
                              change->text_mod ? FLAG_TRUE : FLAG_FALSE,
                              change->prop_mod ? FLAG_TRUE : FLAG_FALSE,
                              change->mergeinfo_mod == svn_tristate_true
                                               ? FLAG_TRUE : FLAG_FALSE,
                              auto_escape_path(path, pool));

  if (SVN_IS_VALID_REVNUM(change->copyfrom_rev))
    {
      svn_stringbuf_appendcstr(buf, apr_psprintf(pool, "%ld %s",
                             change->copyfrom_rev,
                             auto_escape_path(change->copyfrom_path, pool)));
    }

  svn_stringbuf_appendbyte(buf, '\n');

  /* Write all change info in one write call. */
  len = buf->len;
  return svn_error_trace(svn_stream_write(stream, buf->data, &len));
}

svn_error_t *
svn_fs_x__write_changes(svn_stream_t *stream,
                        svn_fs_t *fs,
                        apr_hash_t *changes,
                        svn_boolean_t terminate_list,
                        apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_array_header_t *sorted_changed_paths;
  int i;

  /* For the sake of the repository administrator sort the changes so
     that the final file is deterministic and repeatable, however the
     rest of the FSX code doesn't require any particular order here. */
  sorted_changed_paths = svn_sort__hash(changes,
                                        svn_sort_compare_items_lexically, pool);

  /* Write all items to disk in the new order. */
  for (i = 0; i < sorted_changed_paths->nelts; ++i)
    {
      svn_fs_path_change2_t *change;
      const char *path;

      svn_pool_clear(iterpool);

      change = APR_ARRAY_IDX(sorted_changed_paths, i, svn_sort__item_t).value;
      path = APR_ARRAY_IDX(sorted_changed_paths, i, svn_sort__item_t).key;

      /* Write out the new entry into the final rev-file. */
      SVN_ERR(write_change_entry(stream, path, change, iterpool));
    }

  if (terminate_list)
    svn_stream_puts(stream, "\n");
  
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

