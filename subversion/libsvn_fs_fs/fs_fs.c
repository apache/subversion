/* fs_fs.c --- filesystem operations specific to fs_fs
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>              /* for EINVAL */
#include <ctype.h>

#include <apr_general.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_uuid.h>
#include <apr_md5.h>

#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_hash.h"
#include "svn_md5.h"
#include "id.h"
#include "fs.h"
#include "err.h"
#include "dag.h"
#include "revs-txns.h"
#include "key-gen.h"
#include "fs_fs.h"

/* Notes:

   To avoid opening and closing the rev-files all the time, it would
   probably be advantageous to keep each rev-file open for the
   lifetime of the transaction object.  I'll leave that as a later
   optimization for now.

   I didn't keep track of pool lifetimes at all in this code.  There
   are likely some errors because of that.
   
*/

/* Read a text representation of an apr_hash from STREAM into the hash
   object HASH.  Allocations are from POOL.  This is copied directly
   from svn_hash_read, with file reads replaced by stream reads. */
static svn_error_t *
hash_read (apr_hash_t *hash,
           svn_stream_t *stream,
           apr_pool_t *pool)
{
  svn_stringbuf_t *stringbuf;
  svn_boolean_t eof;
  char buf[SVN_KEYLINE_MAXLEN];
  char c;
  int first_time = 1;


  while (1)
    {
      /* Read a key length line.  Might be END, though. */
      apr_size_t len = sizeof (buf);

      SVN_ERR (svn_stream_readline (stream, &stringbuf, "\n", &eof, pool));
      if (eof)
        {
          /* We got an EOF on our very first attempt to read, which
             means it's a zero-byte file.  No problem, just go home. */
          return SVN_NO_ERROR;
        }
      first_time = 0;

      if ((strcmp (stringbuf->data, "END") == 0)
          || (strcmp (stringbuf->data, "PROPS-END") == 0))
        {
          /* We've reached the end of the dumped hash table, so leave. */
          return SVN_NO_ERROR;
        }
      else if ((stringbuf->data[0] == 'K') && (stringbuf->data[1] == ' '))
        {
          /* Get the length of the key */
          size_t keylen = (size_t) atoi (stringbuf->data + 2);

          /* Now read that much into a buffer, + 1 byte for null terminator */
          void *keybuf = apr_palloc (pool, keylen + 1);
          SVN_ERR (svn_stream_read (stream, keybuf, &keylen));
          ((char *) keybuf)[keylen] = '\0';

          /* Suck up extra newline after key data */
          len = 1;
          SVN_ERR (svn_stream_read (stream, &c, &len));
          
          if (c != '\n')
            return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);

          /* Read a val length line */
          SVN_ERR (svn_stream_readline (stream, &stringbuf, "\n", &eof, pool));

          if ((stringbuf->data[0] == 'V') && (stringbuf->data[1] == ' '))
            {
              svn_string_t *value = apr_palloc (pool, sizeof (*value));

              /* Get the length of the value */
              apr_size_t vallen = atoi (stringbuf->data + 2);

              /* Again, 1 extra byte for the null termination. */
              void *valbuf = apr_palloc (pool, vallen + 1);
              SVN_ERR (svn_stream_read (stream, valbuf, &vallen));
              ((char *) valbuf)[vallen] = '\0';

              /* Suck up extra newline after val data */
              len = 1;
              SVN_ERR (svn_stream_read (stream, &c, &len));
              
              if (c != '\n')
                return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);

              value->data = valbuf;
              value->len = vallen;

              /* The Grand Moment:  add a new hash entry! */
              apr_hash_set (hash, keybuf, keylen, value);
            }
          else
            {
              return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);
            }
        }
      else
        {
          return svn_error_create (SVN_ERR_MALFORMED_FILE, NULL, NULL);
        }
    } /* while (1) */
}

/* Write a text representation of an apr_hash from hash object HASH
   into stream STREAM.  Allocations are from POOL.  This is copied
   directly from svn_hash_write, with file writes replaced by stream
   writes. */
static svn_error_t *
hash_write (apr_hash_t *hash, svn_stream_t *stream, apr_pool_t *pool)
{
  apr_hash_index_t *this;      /* current hash entry */
  apr_pool_t *iterpool;

  iterpool = svn_pool_create (pool);

  for (this = apr_hash_first (pool, hash); this; this = apr_hash_next (this))
    {
      const void *key;
      void *val;
      apr_ssize_t keylen;
      const char* buf;
      const svn_string_t *value;
      apr_size_t limit;

      svn_pool_clear (iterpool);

      /* Get this key and val. */
      apr_hash_this (this, &key, &keylen, &val);
      /* Output name length, then name. */
      buf = apr_psprintf (iterpool, "K %" APR_SSIZE_T_FMT "\n", keylen);

      limit = strlen (buf);
      SVN_ERR (svn_stream_write (stream, buf, &limit));

      SVN_ERR (svn_stream_write (stream, (const char *) key, &keylen));
      SVN_ERR (svn_stream_printf (stream, iterpool, "\n"));

      /* Output value length, then value. */
      value = val;

      buf = apr_psprintf (iterpool, "V %" APR_SIZE_T_FMT "\n", value->len);
      limit = strlen (buf);
      SVN_ERR (svn_stream_write (stream, buf, &limit));

      limit = value->len;
      SVN_ERR (svn_stream_write (stream, value->data, &limit));

      SVN_ERR (svn_stream_printf (stream, iterpool, "\n"));
    }

  svn_pool_destroy (iterpool);

  SVN_ERR (svn_stream_printf (stream, pool, "END\n"));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_open (svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  apr_file_t *current_file;

  /* Attempt to open the 'current' file of this repository.  There
     isn't much need for specific state associated with an open fs_fs
     repository. */

  fs->fs_path = apr_pstrdup (pool, path);

  SVN_ERR (svn_io_file_open (&current_file,
                             svn_path_join (path, SVN_FS_FS__CURRENT, pool),
                             APR_READ, APR_OS_DEFAULT, pool));

  SVN_ERR (svn_io_file_close (current_file, pool));
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_youngest_revision (svn_revnum_t *youngest_p,
                              svn_fs_t *fs,
                              apr_pool_t *pool)
{
  apr_file_t *revision_file;
  char buf[80];
  apr_size_t len;

  SVN_ERR (svn_io_file_open (&revision_file,
                             svn_path_join (fs->fs_path, SVN_FS_FS__CURRENT,
                                            pool),
                             APR_READ, APR_OS_DEFAULT, pool));

  len = sizeof (buf);
  SVN_ERR (svn_io_file_read (revision_file, buf, &len, pool));
  buf[len] = '\0';

  *youngest_p = atoi (buf);

  SVN_ERR (svn_io_file_close (revision_file, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_get_rev (svn_fs__revision_t **rev_p,
                    svn_fs_t *fs,
                    svn_revnum_t rev,
                    apr_pool_t *pool)
{
  abort ();

  return SVN_NO_ERROR;
}

/* Given a revision file FILE that has been pre-positioned at the
   beginning of a Node-Rev header block, read in that header block and
   store it in the apr_hash_t HEADERS.  All allocations will be from
   POOL. */
static svn_error_t * read_header_block (apr_hash_t **headers,
                                        apr_file_t *file,
                                        apr_pool_t *pool)
{
  *headers = apr_hash_make (pool);
  
  while (1)
    {
      char header_str[1024];
      const char *name, *value;
      apr_size_t i = 0, header_len;
      apr_size_t limit;
      char *local_name, *local_value;

      limit = sizeof (header_str);
      SVN_ERR (svn_io_read_length_line (file, header_str, &limit, pool));

      if (strlen (header_str) == 0)
        break; /* end of header block */
      
      header_len = strlen (header_str);

      while (header_str[i] != ':')
        {
          if (header_str[i] == '\0')
            return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                     "Found malformed header in "
                                     "revision file");
          i++;
        }
      
      /* Create a 'name' string and point to it. */
      header_str[i] = '\0';
      name=header_str;

      /* Skip over the NULL byte and the space following it. */
      i += 2;

      if (i > header_len)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Found malformed header in "
                                 "revision file");

      value = header_str + i;

      local_name = apr_pstrdup (pool, name);
      local_value = apr_pstrdup (pool, value);

      apr_hash_set (*headers, local_name, APR_HASH_KEY_STRING, local_value);
    }

  return SVN_NO_ERROR;
}

/* Open the revision file for revision REV in filesystem FS and store
   the newly opened file in FILE.  Seek to location OFFSET before
   returning.  Perform temporary allocations in POOL. */
static svn_error_t *
open_and_seek_revision (apr_file_t **file,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_off_t offset,
                        apr_pool_t *pool)
{
  const char *rev_filename;
  apr_file_t *rev_file;

  rev_filename = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, rev);

  SVN_ERR (svn_io_file_open (&rev_file,
                             svn_path_join_many (pool, 
                                                 fs->fs_path,
                                                 SVN_FS_FS__REVS_DIR,
                                                 rev_filename,
                                                 NULL),
                             APR_READ, APR_OS_DEFAULT, pool));

  SVN_ERR (svn_io_file_seek (rev_file, APR_SET, &offset, pool));

  *file = rev_file;

  return SVN_NO_ERROR;
}

/* Open the representation for a node-revision in transaction TXN_ID
   in filesystem FS and store the newly opened file in FILE.  Seek to
   location OFFSET before returning.  Perform temporary allocations in
   POOL. */
static svn_error_t *
open_and_seek_transaction (apr_file_t **file,
                           svn_fs_t *fs,
                           const svn_fs_id_t *id,
                           const char *txn_id,
                           apr_off_t offset,
                           svn_boolean_t directory_contents,
                           apr_pool_t *pool)
{
  const char *filename;
  apr_file_t *rev_file;

  filename = svn_path_join_many (pool, fs->fs_path, SVN_FS_FS__TXNS_DIR,
                                 apr_pstrcat (pool, txn_id,
                                              SVN_FS_FS__TXNS_EXT, NULL),
                                 NULL);

  if (! directory_contents)
    {
      filename = svn_path_join (filename, SVN_FS_FS__REV, pool);
    }
  else
    {
      filename = svn_path_join (filename,
                                apr_psprintf (pool, "%s.%s"
                                              SVN_FS_FS__CHILDREN_EXT,
                                              id->node_id, id->copy_id),
                                pool);
    }

  SVN_ERR (svn_io_file_open (&rev_file, filename,
                             APR_READ, APR_OS_DEFAULT, pool));

  SVN_ERR (svn_io_file_seek (rev_file, APR_SET, &offset, pool));

  *file = rev_file;

  return SVN_NO_ERROR;
}

/* Given a node-id ID, and a representation REP in filesystem FS, open
   the correct file and seek to the correction location.  Store this
   file in *FILE_P.  Perform any allocations in POOL. */
static svn_error_t *
open_and_seek_representation (apr_file_t **file_p,
                              svn_fs_t *fs,
                              const svn_fs_id_t *id,
                              svn_fs__representation_t *rep,
                              apr_pool_t *pool)
{
  if (! rep->txn_id)
    {
      SVN_ERR (open_and_seek_revision (file_p, fs, rep->revision, rep->offset,
                                       pool));
    }
  else
    {
      SVN_ERR (open_and_seek_transaction (file_p, fs, id, rep->txn_id,
                                          rep->offset,
                                          rep->is_directory_contents, pool));
    }

  return SVN_NO_ERROR;
}

/* Parse the four numbers from a text or prop representation
   descriptor in STRING.  The resulting revision, offset, size, and
   expanded size are stored in *REVISION_P, *OFFSET_P, *SIZE_P, and
   *EXPANDED_SIZE_P respectively. */
static svn_error_t *
read_rep_offsets (svn_fs__representation_t **rep_p,
                  char *string,
                  const char *txn_id,
                  apr_pool_t *pool)
{
  svn_fs__representation_t *rep;
  char *str, *last_str;
  int i;

  rep = apr_pcalloc (pool, sizeof (*rep));
  
  str = apr_strtok (string, " ", &last_str);
  if (str == NULL)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Malformed text rep offset line in node-rev");


  rep->revision = atoi (str);
  if (rep->revision == SVN_INVALID_REVNUM)
      rep->txn_id = txn_id;
  
  str = apr_strtok (NULL, " ", &last_str);
  if (str == NULL)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Malformed text rep offset line in node-rev");
  
  rep->offset = apr_atoi64 (str);
  
  str = apr_strtok (NULL, " ", &last_str);
  if (str == NULL)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Malformed text rep offset line in node-rev");
  
  rep->size = apr_atoi64 (str);
  
  str = apr_strtok (NULL, " ", &last_str);
  if (str == NULL)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Malformed text rep offset line in node-rev");
  
  rep->expanded_size = apr_atoi64 (str);

  /* Read in the MD5 hash. */
  str = apr_strtok (NULL, " ", &last_str);
  if ((str == NULL) || (strlen (str) != (APR_MD5_DIGESTSIZE * 2)))
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Malformed text rep offset line in node-rev");

  /* Parse the hex MD5 hash into digest form. */
  for (i = 0; i < APR_MD5_DIGESTSIZE; i++)
    {
      if ((! isxdigit (str[i * 2])) || (! isxdigit (str[i * 2 + 1])))
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Malformed text rep offset line in node-rev");

      str[i * 2] = tolower (str[i * 2]);
      rep->checksum[i] = (str[i * 2] -
                          ((str[i * 2] <= '9') ? '0' : ('a' - 10))) << 4;

      str[i * 2 + 1] = tolower (str[i * 2 + 1]);
      rep->checksum[i] |= (str[i * 2 + 1] -
                           ((str[i * 2 + 1] <= '9') ? '0' : ('a' - 10)));
    }
  
  *rep_p = rep;

  return SVN_NO_ERROR;
}

/* Open a new file in *FILE_P for the node-revision id of NODE_ID
   . COPY_ID . TXN_ID in filesystem FS.  Peform any allocations in
   POOL. */
static svn_error_t *
open_txn_node_rev (apr_file_t **file_p,
                   svn_fs_t *fs,
                   const char *node_id,
                   const char *copy_id,
                   const char *txn_id,
                   apr_pool_t *pool)
{
  char *filename;
  apr_file_t *file;

  filename = svn_path_join_many (pool, fs->fs_path,
                                 SVN_FS_FS__TXNS_DIR,
                                 apr_pstrcat (pool, txn_id, ".txn", NULL),
                                 apr_pstrcat (pool, node_id, ".",
                                              copy_id, NULL), NULL);

  SVN_ERR (svn_io_file_open (&file, filename, APR_READ,
                             APR_OS_DEFAULT, pool));

  *file_p = file;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_get_node_revision (svn_fs__node_revision_t **noderev_p,
                              svn_fs_t *fs,
                              const svn_fs_id_t *id,
                              apr_pool_t *pool)
{
  apr_file_t *revision_file;
  apr_hash_t *headers;
  svn_fs__node_revision_t *noderev;
  char *value;
  
  if (id->txn_id)
      {
        /* This is a transaction node-rev. */
        SVN_ERR (open_txn_node_rev (&revision_file, fs, svn_fs__id_node_id (id),
                                    svn_fs__id_copy_id (id),
                                    svn_fs__id_txn_id (id), pool));
      }
  else
    {
      /* This is a revision node-rev. */
      SVN_ERR (open_and_seek_revision (&revision_file, fs,
                                       svn_fs__id_rev (id),
                                       svn_fs__id_offset (id),
                                       pool));
    }
  
  SVN_ERR (read_header_block (&headers, revision_file, pool) );

  noderev = apr_pcalloc (pool, sizeof (*noderev));

  /* Read the node-rev id. */
  value = apr_hash_get (headers, SVN_FS_FS__NODE_ID, APR_HASH_KEY_STRING);

  noderev->id = svn_fs_parse_id (value, strlen (value), pool);

  /* Read the type. */
  value = apr_hash_get (headers, SVN_FS_FS__KIND, APR_HASH_KEY_STRING);

  if ((value == NULL) ||
      (strcmp (value, SVN_FS_FS__FILE) != 0 && strcmp (value, SVN_FS_FS__DIR)))
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Missing kind field in node-rev.");

  if (strcmp (value, SVN_FS_FS__FILE) == 0)
    {
      noderev->kind = svn_node_file;
    }
  else
    {
      noderev->kind = svn_node_dir;
    }

  /* Read the 'count' field. */
  value = apr_hash_get (headers, SVN_FS_FS__COUNT, APR_HASH_KEY_STRING);
  if (value == NULL)
    {
      noderev->predecessor_count = 0;
    }
  else
    {
      noderev->predecessor_count = atoi (value);
    }

  /* Get the properties location. */
  value = apr_hash_get (headers, SVN_FS_FS__PROPS, APR_HASH_KEY_STRING);
  if (value)
    {
      SVN_ERR (read_rep_offsets (&noderev->prop_rep, value, id->txn_id, pool));
      noderev->prop_rep->is_directory_contents = FALSE;
    }

  /* Get the data location. */
  value = apr_hash_get (headers, SVN_FS_FS__TEXT, APR_HASH_KEY_STRING);
  if (value)
    {
      SVN_ERR (read_rep_offsets (&noderev->data_rep, value, id->txn_id, pool));
    }

  /* Get the created path. */
  value = apr_hash_get (headers, SVN_FS_FS__CPATH, APR_HASH_KEY_STRING);
  if (value == NULL)
    {
      return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                               "Missing cpath in node-rev");
    }
  else
    {
      noderev->created_path = apr_pstrdup (pool, value);
    }

  /* Get the predecessor ID. */
  value = apr_hash_get (headers, SVN_FS_FS__PRED, APR_HASH_KEY_STRING);
  if (value)
    {
      noderev->predecessor_id = svn_fs_parse_id (value, strlen (value), pool);
    }
  
  /* Get the copyroot. */
  value = apr_hash_get (headers, SVN_FS_FS__COPYROOT, APR_HASH_KEY_STRING);
  if (value == NULL)
    {
      noderev->copyroot_path = apr_pstrdup (pool, noderev->created_path);
      noderev->copyroot_rev = svn_fs__id_rev (noderev->id);
    }
  else
    {
      char *str, *last_str;

      str = apr_strtok (value, " ", &last_str);
      if (str == NULL)
          return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                   "Malformed copyroot line in node-rev");

      noderev->copyroot_rev = atoi (str);
      
      if (last_str == NULL)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Malformed copyroot line in node-rev");
      noderev->copyroot_path = apr_pstrdup (pool, last_str);
    }

  /* Get the copyfrom. */
  value = apr_hash_get (headers, SVN_FS_FS__COPYFROM, APR_HASH_KEY_STRING);
  if (value == NULL)
    {
      noderev->copyfrom_path = NULL;
      noderev->copyfrom_rev = SVN_INVALID_REVNUM;
    }
  else
    {
      char *str, *last_str;

      str = apr_strtok (value, " ", &last_str);
      if (str == NULL)
          return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                   "Malformed copyfrom line in node-rev");

      noderev->copyfrom_rev = atoi (str);
      
      if (last_str == NULL)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Malformed copyfrom line in node-rev");
      noderev->copyfrom_path = apr_pstrdup (pool, last_str);
    }

  if ((noderev->kind == svn_node_dir) && noderev->data_rep)
    noderev->data_rep->is_directory_contents = TRUE;
  
  *noderev_p = noderev;
  
  return SVN_NO_ERROR;
}

/* Return a formatted string that represents the location of
   representation REP.  Perform the allocation from POOL. */
static char *
representation_string (svn_fs__representation_t *rep,
                       apr_pool_t *pool)
{
  const char *rev;

  rev = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, rep->revision);

  return apr_psprintf (pool, "%s %" APR_OFF_T_FMT " %" APR_SIZE_T_FMT " %"
                       APR_SIZE_T_FMT " %s",
                       rev, rep->offset, rep->size, rep->expanded_size,
                       svn_md5_digest_to_cstring (rep->checksum, pool));
}

/* Write the node-revision NODEREV into the file FILE.  Temporary
   allocations are from POOL. */
svn_error_t *
write_noderev_txn (apr_file_t *file,
                   svn_fs__node_revision_t *noderev,
                   apr_pool_t *pool)
{
  svn_stream_t *outfile;

  outfile = svn_stream_from_aprfile (file, pool);

  SVN_ERR (svn_stream_printf (outfile, pool, SVN_FS_FS__NODE_ID ": %s\n",
                              svn_fs_unparse_id (noderev->id, pool)->data));

  SVN_ERR (svn_stream_printf (outfile, pool, SVN_FS_FS__KIND ": %s\n",
                              (noderev->kind == svn_node_file) ?
                              SVN_FS_FS__FILE : SVN_FS_FS__DIR));

  if (noderev->predecessor_id)
    SVN_ERR (svn_stream_printf (outfile, pool, SVN_FS_FS__PRED ": %s\n",
                                svn_fs_unparse_id (noderev->predecessor_id,
                                                   pool)->data));

  SVN_ERR (svn_stream_printf (outfile, pool, SVN_FS_FS__COUNT ": %d\n",
                              noderev->predecessor_count));

  if (noderev->data_rep)
    SVN_ERR (svn_stream_printf (outfile, pool, SVN_FS_FS__TEXT ": %s\n",
                                representation_string (noderev->data_rep,
                                                       pool)));

  if (noderev->prop_rep)
    SVN_ERR (svn_stream_printf (outfile, pool, SVN_FS_FS__PROPS ": %s\n",
                                representation_string (noderev->prop_rep,
                                                       pool)));

  SVN_ERR (svn_stream_printf (outfile, pool, SVN_FS_FS__CPATH ": %s\n",
                              noderev->created_path));

  if (noderev->copyfrom_path)
    SVN_ERR (svn_stream_printf (outfile, pool, SVN_FS_FS__COPYFROM ": %"
                                SVN_REVNUM_T_FMT " %s\n",
                                noderev->copyfrom_rev,
                                noderev->copyfrom_path));

  if ((noderev->copyroot_rev != svn_fs__id_rev (noderev->id)) ||
      (strcmp (noderev->copyroot_path, noderev->created_path) != 0))
    SVN_ERR (svn_stream_printf (outfile, pool, SVN_FS_FS__COPYROOT ": %"
                                SVN_REVNUM_T_FMT " %s\n",
                                noderev->copyroot_rev,
                                noderev->copyroot_path));

  SVN_ERR (svn_stream_printf (outfile, pool, "\n"));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_put_node_revision (svn_fs_t *fs,
                              const svn_fs_id_t *id,
                              svn_fs__node_revision_t *noderev,
                              apr_pool_t *pool)
{
  const char *dirname;
  apr_file_t *noderev_file;

  if (! id->txn_id)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Attempted to write to non-transaction.");

  /* Write out the new node-rev file in this transaction. */
  dirname = svn_path_join_many (pool, fs->fs_path, SVN_FS_FS__TXNS_DIR,
                                apr_pstrcat (pool, id->txn_id, ".txn", NULL),
                                apr_psprintf (pool, "%s.%s", id->node_id,
                                              id->copy_id),
                                NULL);

  SVN_ERR (svn_io_file_open (&noderev_file, dirname,
                             APR_WRITE | APR_CREATE | APR_TRUNCATE,
                             APR_OS_DEFAULT, pool));

  SVN_ERR (write_noderev_txn (noderev_file, noderev, pool));

  SVN_ERR (svn_io_file_close (noderev_file, pool));

  return SVN_NO_ERROR;
}


/* This structure is used to hold the information associated with a
   REP line. */
struct rep_args_t
{
  svn_boolean_t is_delta;
  svn_boolean_t is_delta_vs_empty;
  
  svn_revnum_t delta_revision;
  apr_off_t delta_offset;
  apr_size_t delta_length;
};

/* Read the next line from file FILE and parse it as a text
   representation entry.  Return the parsed entry in REP_ARGS_P.
   Perform all allocations in POOL. */
static svn_error_t *
read_rep_line (struct rep_args_t **rep_args_p,
               apr_file_t *file,
               apr_pool_t *pool)
{
  char buffer[80];
  apr_size_t limit;
  struct rep_args_t *rep_args;
  
  limit = sizeof (buffer);
  SVN_ERR (svn_io_read_length_line (file, buffer, &limit, pool));

  rep_args = apr_pcalloc (pool, sizeof (*rep_args));
  rep_args->is_delta = FALSE;

  if (strcmp (buffer, "PLAIN") == 0)
    {
      *rep_args_p = rep_args;
      return SVN_NO_ERROR;
    }

  if (strcmp (buffer, "DELTA") == 0)
    {
      /* This is a delta against the empty stream. */
      rep_args->is_delta = TRUE;
      rep_args->is_delta_vs_empty = TRUE;
      *rep_args_p = rep_args;
      return SVN_NO_ERROR;
    }

  abort ();

  return SVN_NO_ERROR;
}

/* Given a revision file REV_FILE, find the Node-ID of the header
   located at OFFSET and store it in *ID_P.  Allocate temporary
   variables from POOL. */
static svn_error_t *
get_fs_id_at_offset (svn_fs_id_t **id_p,
                     apr_file_t *rev_file,
                     apr_off_t offset,
                     apr_pool_t *pool)
{
  svn_fs_id_t *id;
  apr_hash_t *headers;
  const char *node_id_str;
  
  SVN_ERR (svn_io_file_seek (rev_file, APR_SET, &offset, pool));

  SVN_ERR (read_header_block (&headers, rev_file, pool));

  node_id_str = apr_hash_get (headers, SVN_FS_FS__NODE_ID,
                              APR_HASH_KEY_STRING);

  if (node_id_str == NULL)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Missing node-id in node-rev");

  id = svn_fs_parse_id (node_id_str, strlen (node_id_str), pool);

  if (id == NULL)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Corrupt node-id in node-rev");

  *id_p = id;

  return SVN_NO_ERROR;
}


/* Given an open revision file REV_FILE, locate the trailer that
   specifies the offset to the root node-id and to the changed path
   information.  Store the root node offset in *ROOT_OFFSET and the
   changed path offset in *CHANGES_OFFSET.  If either of these
   pointers is NULL, do nothing with it. Allocate temporary variables
   from POOL. */
static svn_error_t *
get_root_changes_offset (apr_off_t *root_offset,
                         apr_off_t *changes_offset,
                         apr_file_t *rev_file,
                         apr_pool_t *pool)
{
  apr_off_t offset;
  apr_size_t num_bytes;
  char buf[65];
  int i;
  
  /* We will assume that the last line containing the two offsets
     will never be longer than 64 characters. */
  offset = -64;
  SVN_ERR (svn_io_file_seek (rev_file, APR_END, &offset, pool));

  /* Read in this last block, from which we will identify the last line. */
  num_bytes=64;
  SVN_ERR (svn_io_file_read (rev_file, buf, &num_bytes, pool));

  /* The last byte should be a newline. */
  if (buf[num_bytes - 1] != '\n')
    {
      return svn_error_createf (SVN_ERR_FS_CORRUPT, NULL,
                                "Revision file lacks trailing newline.");
    }

  /* Look for the next previous newline. */
  for (i = num_bytes - 2; i >= 0; i--)
    {
      if (buf[i] == '\n') break;
    }

  if (i < 0)
    {
      return svn_error_createf (SVN_ERR_FS_CORRUPT, NULL,
                                "Final line in revision file longer than 64 "
                                "characters.");
    }

  if (root_offset)
    *root_offset = apr_atoi64 (&buf[i]);

  /* find the next space */
  for ( ; i < (num_bytes - 3) ; i++)
    if (buf[i] == ' ') break;

  if (i == (num_bytes - 2))
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Final line in revision file missing space.");

  i++;

  if (changes_offset)
    *changes_offset = apr_atoi64 (&buf[i]);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_rev_get_root (svn_fs_id_t **root_id_p,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_pool_t *pool)
{
  char *revision_filename;
  apr_file_t *revision_file;
  apr_off_t root_offset;
  svn_fs_id_t *root_id;

  revision_filename = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, rev);

  SVN_ERR (svn_io_file_open (&revision_file,
                             svn_path_join_many (pool, 
                                                 fs->fs_path,
                                                 SVN_FS_FS__REVS_DIR,
                                                 revision_filename,
                                                 NULL),
                             APR_READ, APR_OS_DEFAULT, pool));

  SVN_ERR (get_root_changes_offset (&root_offset, NULL, revision_file, pool));

  SVN_ERR (get_fs_id_at_offset (&root_id, revision_file, root_offset, pool));

  SVN_ERR (svn_io_file_close (revision_file, pool));

  *root_id_p = root_id;
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_set_revision_proplist (svn_fs_t *fs,
                                  svn_revnum_t rev,
                                  apr_hash_t *proplist,
                                  apr_pool_t *pool)
{
  char *revprop_filename;
  apr_file_t *revprop_file;

  revprop_filename = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, rev);

  SVN_ERR (svn_io_file_open (&revprop_file,
                             svn_path_join_many (pool,
                                                 fs->fs_path,
                                                 SVN_FS_FS__REVPROPS_DIR, 
                                                 revprop_filename,
                                                 NULL),
                             APR_WRITE | APR_TRUNCATE | APR_CREATE,
                             APR_OS_DEFAULT, pool));
  
  SVN_ERR (svn_hash_write (proplist, revprop_file, pool));

  SVN_ERR (svn_io_file_close (revprop_file, pool));
  
  return SVN_NO_ERROR;
}  

svn_error_t *
svn_fs__fs_revision_proplist (apr_hash_t **proplist_p,
                              svn_fs_t *fs,
                              svn_revnum_t rev,
                              apr_pool_t *pool)
{
  char *revprop_filename;
  apr_file_t *revprop_file;
  apr_hash_t *proplist;

  revprop_filename = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, rev);

  SVN_ERR (svn_io_file_open (&revprop_file,
                             svn_path_join_many (pool,
                                                 fs->fs_path,
                                                 SVN_FS_FS__REVPROPS_DIR, 
                                                 revprop_filename,
                                                 NULL),
                             APR_READ | APR_CREATE, APR_OS_DEFAULT, pool));

  proplist = apr_hash_make (pool);

  SVN_ERR (svn_hash_read (proplist, revprop_file, pool));

  SVN_ERR (svn_io_file_close (revprop_file, pool));

  *proplist_p = proplist;

  return SVN_NO_ERROR;
}

struct rep_read_baton
{
  /* The FS from which we're reading. */
  svn_fs_t *fs;

  /* Location of the representation we want to read. */
  apr_file_t *rep_file;

  /* How many bytes have been read from the rep file already. */
  svn_filesize_t rep_offset;

  /* How many bytes are there in this deltafied representation. */
  apr_size_t rep_size;

  /* Is this text-representation in delta format? */
  svn_boolean_t is_delta;
  
  /* Streams to use with the delta handler. */
  svn_stream_t *wstream, *target_stream;
  
  /* MD5 checksum.  Initialized when the baton is created, updated as
     we read data, and finalized when the stream is closed. */
  struct apr_md5_ctx_t md5_context;

  /* The length of the rep's contents (as fulltext, that is,
     independent of how the rep actually stores the data.)  This is
     retrieved when the baton is created, and used to determine when
     we have read the last byte, at which point we compare checksums.

     Getting this at baton creation time makes interleaved reads and
     writes on the same rep in the same trail impossible.  But we're
     not doing that, and probably no one ever should.  And anyway if
     they do, they should see problems immediately. */
  svn_filesize_t size;

  /* Set to FALSE when the baton is created, TRUE when the md5_context
     is digestified. */
  svn_boolean_t checksum_finalized;

  /* Used for temporary allocations, iff `trail' (above) is null.  */
  apr_pool_t *pool;

  svn_stringbuf_t *nonconsumed_data;
};

/* Handler to be called when txdelta has a new window of data read.
   BATON is of type rep_read_baton, DATA points to the data being
   written and *LEN contains the length of data being written.

   This function just buffers that data so that it can be read from
   the content stream at the proper time.*/
static svn_error_t *
read_contents_write_handler (void *baton,
                             const char *data,
                             apr_size_t *len)
{
  struct rep_read_baton *b = baton;
  
  svn_stringbuf_appendbytes (b->nonconsumed_data, data, *len);

  return SVN_NO_ERROR;
}

/* Create a rep_read_baton structure for node revision NODEREV in
   filesystem FS and store it in *RB_P.  Perform all allocations in
   POOL.

   This opens the revision file and positions the file stream at the
   beginning of the text representation.  In addition, if the
   representation is in delta format, it sets up the delta handling
   chain.
*/
static svn_error_t *
rep_read_get_baton (struct rep_read_baton **rb_p,
                    svn_fs_t *fs,
                    const svn_fs_id_t *id,
                    svn_fs__representation_t *rep,
                    apr_pool_t *pool)
{
  struct rep_read_baton *b;
  struct rep_args_t *rep_args;
  svn_stream_t *empty_stream;
  svn_txdelta_window_handler_t handler;
  void *handler_baton;

  b = apr_pcalloc (pool, sizeof (*b));
  apr_md5_init (&(b->md5_context));

  b->fs = fs;
  b->pool = pool;
  b->rep_offset = 0;
  b->rep_size = rep->size;
  b->size = rep->expanded_size;
  b->nonconsumed_data = svn_stringbuf_create ("", pool);
  b->is_delta = FALSE;

  /* Open the revision file. */
  SVN_ERR (open_and_seek_representation (&b->rep_file, fs, id, rep, pool));

  /* Read in the REP line. */
  SVN_ERR (read_rep_line (&rep_args, b->rep_file, pool));

  if (rep_args->is_delta)
    {
      /* Set up the delta handler. */
      if (rep_args->is_delta_vs_empty == FALSE)
        abort ();
      
      /* Create a stream that txdelta apply can write to, where we will
         accumulate undeltified data. */
      b->target_stream = svn_stream_create (b, pool);
      svn_stream_set_write (b->target_stream, read_contents_write_handler);
      
      /* For now the empty stream is always our base revision. */
      empty_stream = svn_stream_empty (pool);
      
      /* Create a handler that can process chunks of txdelta. */
      svn_txdelta_apply (empty_stream, b->target_stream, NULL, NULL,
                         pool, &handler, &handler_baton);

      /* Create a writable stream that will call our handler when svndiff
         data is written to it. */
      b->wstream = svn_txdelta_parse_svndiff (handler, handler_baton, 
                                              FALSE, pool);

      b->is_delta = TRUE;
    }

  /* Save our output baton. */
  *rb_p = b;

  return SVN_NO_ERROR;
}

/* Handles closing the content stream.  BATON is of type
   rep_read_baton. */
static svn_error_t *
rep_read_contents_close (void *baton)
{
  struct rep_read_baton *rb = baton;
  
  /* Clean up our baton. */
  SVN_ERR (svn_io_file_close (rb->rep_file, rb->pool));
  if (rb->wstream)
    SVN_ERR (svn_stream_close (rb->wstream));

  if (rb->target_stream)
    SVN_ERR (svn_stream_close (rb->target_stream));

  return SVN_NO_ERROR;
}
  

/* BATON is of type `rep_read_baton':

   Read into BATON->rb->buf the *(BATON->len) bytes starting at
   BATON->rb->offset.  Afterwards, *LEN is the number of bytes
   actually read, and BATON->rb->offset is incremented by that amount.
*/

static svn_error_t *
rep_read_contents (void *baton,
                   char *buf,
                   apr_size_t *len)
{
  struct rep_read_baton *rb = baton;
  apr_size_t size = 0;
  char file_buf[4096];

  if (rb->is_delta)
    {

      while (rb->nonconsumed_data->len < *len) {
        /* Until we have enough data to return, keep trying to send out
           more svndiff data. */
        
        size = sizeof (file_buf);
        if (size > (rb->rep_size - rb->rep_offset))
          size = rb->rep_size - rb->rep_offset;
        
        /* Check to see if we've read the entire representation. */
        if (size == 0) break;
        
        SVN_ERR (svn_io_file_read (rb->rep_file, file_buf, &size, rb->pool));

        rb->rep_offset += size;
        
        SVN_ERR (svn_stream_write (rb->wstream, file_buf, &size));
      }
      
      /* Send out all the data we have, up to *len. */
      size = *len;
      if (size > rb->nonconsumed_data->len)
        size = rb->nonconsumed_data->len;
      
      memcpy (buf, rb->nonconsumed_data->data, size);
      
      /* Remove the things we just wrote from the stringbuf. */
      memmove (rb->nonconsumed_data->data, rb->nonconsumed_data + size,
               rb->nonconsumed_data->len - size);
      
      svn_stringbuf_chop (rb->nonconsumed_data, size);
      
      *len = size;
    }
  else
    {
      /* This is a plaintext file. */

      if ((*len + rb->rep_offset) > rb->size)
        *len = rb->size - rb->rep_offset;
      
      SVN_ERR (svn_io_file_read_full (rb->rep_file, buf, *len, len, rb->pool));

      rb->rep_offset += *len;
    }

  return SVN_NO_ERROR;
}

/* Return a stream in *CONTENTS_P that will read the contents of a
   text representation stored in filesystem FS, revision REV, at
   offset OFFSET.  The size in the revision file should be exactly
   SIZE, and if this is a delta representation it should undeltify to
   a representation of EXPANDED_SIZE bytes.  If it is a plaintext
   representation, SIZE == EXPANDED_SIZE.

   If REV equals SVN_INVALID_REVNUM, then this representation is
   asssumed to be empty, and an empty stream is returned.
*/
static svn_error_t *
get_representation_at_offset (svn_stream_t **contents_p,
                              svn_fs_t *fs,
                              const svn_fs_id_t *id,
                              svn_fs__representation_t *rep,
                              apr_pool_t *pool)
{
  struct rep_read_baton *rb;

  if (! rep)
    {
      *contents_p = svn_stream_empty (pool);
    }
  else
    {
      SVN_ERR (rep_read_get_baton (&rb, fs, id, rep, pool));
      *contents_p = svn_stream_create (rb, pool);
      svn_stream_set_read (*contents_p, rep_read_contents);
      svn_stream_set_close (*contents_p, rep_read_contents_close);
    }
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_get_contents (svn_stream_t **contents_p,
                         svn_fs_t *fs,
                         svn_fs__node_revision_t *noderev,
                         apr_pool_t *pool)
{
  SVN_ERR (get_representation_at_offset (contents_p, fs, noderev->id,
                                         noderev->data_rep, pool));
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_rep_contents_dir (apr_hash_t **entries_p,
                             svn_fs_t *fs,
                             svn_fs__node_revision_t *noderev,
                             apr_pool_t *pool)
{
  svn_stream_t *rep;
  apr_hash_t *entries;
  apr_hash_index_t *hi;

  entries = apr_hash_make (pool);

  /* Read in the directory hash. */
  SVN_ERR (svn_fs__fs_get_contents (&rep, fs, noderev, pool));
  SVN_ERR (hash_read (entries, rep, pool));
  SVN_ERR (svn_stream_close (rep));

  /* Now convert this entries file into a hash of dirents. */
  *entries_p = apr_hash_make (pool);

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      char *str_val;
      char *str, *last_str;
      svn_fs_dirent_t *dirent = apr_pcalloc (pool, sizeof (*dirent));
      apr_hash_this (hi, &key, &klen, &val);

      dirent->name = key;
      str_val = apr_pstrdup (pool, *((char **)val));

      str = apr_strtok (str_val, " ", &last_str);
      if (str == NULL)
          return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                   "Directory entry corrupt");
      
      if (strcmp (str, SVN_FS_FS__FILE) == 0)
        {
          dirent->kind = svn_node_file;
        }
      else if (strcmp (str, SVN_FS_FS__DIR) == 0)
        {
          dirent->kind = svn_node_dir;
        }
      else
        {
          return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                   "Directory entry corrupt");
        }

      str = apr_strtok (NULL, " ", &last_str);
      if (str == NULL)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Directory entry corrupt");
      
      dirent->id = svn_fs_parse_id (str, strlen (str), pool);

      apr_hash_set (*entries_p, key, klen, dirent);
    }
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_get_proplist (apr_hash_t **proplist_p,
                         svn_fs_t *fs,
                         svn_fs__node_revision_t *noderev,
                         apr_pool_t *pool)
{
  apr_hash_t *proplist;
  svn_stream_t *stream;

  proplist = apr_hash_make (pool);

  SVN_ERR (get_representation_at_offset (&stream, fs, noderev->id,
                                         noderev->prop_rep, pool));
  
  SVN_ERR (hash_read (proplist, stream, pool));

  SVN_ERR (svn_stream_close (stream));

  *proplist_p = proplist;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_file_length (svn_filesize_t *length,
                        svn_fs__node_revision_t *noderev,
                        apr_pool_t *pool)
{
  *length = noderev->data_rep->expanded_size;

  return SVN_NO_ERROR;
}

svn_boolean_t
svn_fs__fs_noderev_same_rep_key (svn_fs__representation_t *a,
                                 svn_fs__representation_t *b)
{
  if (a == b)
    return TRUE;

  if (a && (! b))
    return FALSE;

  if (b && (! a))
    return FALSE;

  if (a->offset != b->offset)
    return FALSE;

  if (a->revision != b->revision)
    return FALSE;
  
  return TRUE;
}

svn_error_t *
svn_fs__fs_file_checksum (unsigned char digest[],
                          svn_fs__node_revision_t *noderev,
                          apr_pool_t *pool)
{
  memcpy (digest, noderev->data_rep->checksum, APR_MD5_DIGESTSIZE);

  return SVN_NO_ERROR;
}

svn_fs__representation_t *
svn_fs__fs_rep_copy (svn_fs__representation_t *rep,
                     apr_pool_t *pool)
{
  svn_fs__representation_t *rep_new;
  
  if (rep == NULL)
    return NULL;
    
  rep_new = apr_pcalloc (pool, sizeof (*rep_new));
  
  memcpy (rep_new, rep, sizeof (*rep_new));
  
  return rep_new;
}

/* Merge the internal-use-only CHANGE into a hash of public-FS
   svn_fs_path_change_t CHANGES, collapsing multiple changes into a
   single summarical (is that real word?) change per path. */
static svn_error_t *
fold_change (apr_hash_t *changes,
             const svn_fs__change_t *change)
{
  apr_pool_t *pool = apr_hash_pool_get (changes);
  svn_fs_path_change_t *old_change, *new_change;
  const char *path;

  if ((old_change = apr_hash_get (changes, change->path, APR_HASH_KEY_STRING)))
    {
      /* This path already exists in the hash, so we have to merge
         this change into the already existing one. */

      /* Since the path already exists in the hash, we don't have to
         dup the allocation for the path itself. */
      path = change->path;
      /* Sanity check:  only allow NULL node revision ID in the
         `reset' case. */
      if ((! change->noderev_id) && (change->kind != svn_fs_path_change_reset))
                return svn_error_create
                  (SVN_ERR_FS_CORRUPT, NULL,
                   "Missing required node revision ID");

      /* Sanity check: we should be talking about the same node
         revision ID as our last change except where the last change
         was a deletion. */
      if (change->noderev_id
          && (! svn_fs__id_eq (old_change->node_rev_id, change->noderev_id))
          && (old_change->change_kind != svn_fs_path_change_delete))
                return svn_error_create
                  (SVN_ERR_FS_CORRUPT, NULL,
                   "Invalid change ordering: new node revision ID without delete");

      /* Sanity check: an add, replacement, or reset must be the first
         thing to follow a deletion. */
      if ((old_change->change_kind == svn_fs_path_change_delete)
          && (! ((change->kind == svn_fs_path_change_replace)
                 || (change->kind == svn_fs_path_change_reset)
                 || (change->kind == svn_fs_path_change_add))))
                return svn_error_create
                  (SVN_ERR_FS_CORRUPT, NULL,
                   "Invalid change ordering: non-add change on deleted path");

      /* Now, merge that change in. */
      switch (change->kind)
        {
        case svn_fs_path_change_reset:
          /* A reset here will simply remove the path change from the
             hash. */
          old_change = NULL;
          break;

        case svn_fs_path_change_delete:
          if (old_change->change_kind == svn_fs_path_change_add)
            {
              /* If the path was introduced in this transaction via an
                 add, and we are deleting it, just remove the path
                 altogether. */
              old_change = NULL;
            }
          else
            {
              /* A deletion overrules all previous changes. */
              old_change->change_kind = svn_fs_path_change_delete;
              old_change->text_mod = change->text_mod;
              old_change->prop_mod = change->prop_mod;
            }
          break;

        case svn_fs_path_change_add:
        case svn_fs_path_change_replace:
          /* An add at this point must be following a previous delete,
             so treat it just like a replace. */
          old_change->change_kind = svn_fs_path_change_replace;
          old_change->node_rev_id = svn_fs__id_copy (change->noderev_id, pool);
          old_change->text_mod = change->text_mod;
          old_change->prop_mod = change->prop_mod;
          break;

        case svn_fs_path_change_modify:
        default:
          if (change->text_mod)
            old_change->text_mod = TRUE;
          if (change->prop_mod)
            old_change->prop_mod = TRUE;
          break;
        }

      /* Point our new_change to our (possibly modified) old_change. */
      new_change = old_change;
    }
  else
    {
      /* This change is new to the hash, so make a new public change
         structure from the internal one (in the hash's pool), and dup
         the path into the hash's pool, too. */
      new_change = apr_pcalloc (pool, sizeof (*new_change));
      new_change->node_rev_id = svn_fs__id_copy (change->noderev_id, pool);
      new_change->change_kind = change->kind;
      new_change->text_mod = change->text_mod;
      new_change->prop_mod = change->prop_mod;
      path = apr_pstrdup (pool, change->path);
    }

  /* Add (or update) this path. */
  apr_hash_set (changes, path, APR_HASH_KEY_STRING, new_change);

  return SVN_NO_ERROR;
}


/* Read the next line in the changes record from file FILE and store
   the resulting change in *CHANGE_P.  If there is no next record,
   store NULL there.  Perform all allocations from POOL. */
static svn_error_t *
read_change (svn_fs__change_t **change_p,
             apr_file_t *file,
             apr_pool_t *pool)
{
  char buf[4096];
  apr_size_t len = sizeof (buf);
  svn_fs__change_t *change;
  char *str, *last_str;

  SVN_ERR (svn_io_read_length_line (file, buf, &len, pool));

  /* Check for a blank line. */
  if (strlen (buf) == 0)
    {
      *change_p = NULL;
      return SVN_NO_ERROR;
    }

  change = apr_pcalloc (pool, sizeof (*change));

  /* Get the node-id of the change. */
  str = apr_strtok (buf, " ", &last_str);
  if (str == NULL)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Invalid changes line in rev-file");

  change->noderev_id = svn_fs_parse_id (str, strlen (str), pool);

  /* Get the change type. */
  str = apr_strtok (NULL, " ", &last_str);
  if (str == NULL)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Invalid changes line in rev-file");

  if (strcmp (str, SVN_FS_FS__ACTION_MODIFY) == 0)
    {
      change->kind = svn_fs_path_change_modify;
    }
  else if (strcmp (str, SVN_FS_FS__ACTION_ADD) == 0)
    {
      change->kind = svn_fs_path_change_add;
    }
  else if (strcmp (str, SVN_FS_FS__ACTION_DELETE) == 0)
    {
      change->kind = svn_fs_path_change_delete;
    }
  else if (strcmp (str, SVN_FS_FS__ACTION_REPLACE) == 0)
    {
      change->kind = svn_fs_path_change_replace;
    }
  else if (strcmp (str, SVN_FS_FS__ACTION_RESET) == 0)
    {
      change->kind = svn_fs_path_change_reset;
    }
  else
    {
      return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                               "Invalid change kind in rev file");
    }

  /* Get the text-mod flag. */
  str = apr_strtok (NULL, " ", &last_str);
  if (str == NULL)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Invalid changes line in rev-file");

  if (strcmp (str, SVN_FS_FS__TRUE) == 0)
    {
      change->text_mod = TRUE;
    }
  else if (strcmp (str, SVN_FS_FS__FALSE) == 0)
    {
      change->text_mod = FALSE;
    }
  else
    {
      return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                               "Invalid text-mod flag in rev-file");
    }

  /* Get the prop-mod flag. */
  str = apr_strtok (NULL, " ", &last_str);
  if (str == NULL)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Invalid changes line in rev-file");

  if (strcmp (str, SVN_FS_FS__TRUE) == 0)
    {
      change->prop_mod = TRUE;
    }
  else if (strcmp (str, SVN_FS_FS__FALSE) == 0)
    {
      change->prop_mod = FALSE;
    }
  else
    {
      return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                               "Invalid prop-mod flag in rev-file");
    }

  /* Get the changed path. */
  change->path = apr_pstrdup (pool, last_str);

  *change_p = change;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_paths_changed (apr_hash_t **changed_paths_p,
                          svn_fs_t *fs,
                          svn_revnum_t rev,
                          apr_pool_t *pool)
{
  char *revision_filename;
  apr_off_t changes_offset;
  apr_hash_t *changed_paths;
  svn_fs__change_t *change;
  apr_file_t *revision_file;
  apr_pool_t *iterpool = svn_pool_create (pool);
  
  revision_filename = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, rev);

  SVN_ERR (svn_io_file_open (&revision_file,
                             svn_path_join_many (pool, 
                                                 fs->fs_path,
                                                 SVN_FS_FS__REVS_DIR,
                                                 revision_filename,
                                                 NULL),
                             APR_READ, APR_OS_DEFAULT, pool));

  SVN_ERR (get_root_changes_offset (NULL, &changes_offset, revision_file,
                                    pool));

  SVN_ERR (svn_io_file_seek (revision_file, APR_SET, &changes_offset, pool));

  changed_paths = apr_hash_make (pool);
  
  /* Read in the changes one by one, folding them into our local hash
     as necessary. */
  
  SVN_ERR (read_change (&change, revision_file, pool));

  while (change)
    {
      SVN_ERR (fold_change (changed_paths, change));

      /* Now, if our change was a deletion or replacement, we have to
         blow away any changes thus far on paths that are (or, were)
         children of this path.
         ### i won't bother with another iteration pool here -- at
             most we talking about a few extra dups of paths into what
             is already a temporary subpool.
      */

      if ((change->kind == svn_fs_path_change_delete)
          || (change->kind == svn_fs_path_change_replace))
        {
          apr_hash_index_t *hi;

          for (hi = apr_hash_first (iterpool, changed_paths);
               hi;
               hi = apr_hash_next (hi))
            {
              /* KEY is the path. */
              const void *hashkey;
              apr_ssize_t klen;
              apr_hash_this (hi, &hashkey, &klen, NULL);

              /* If we come across our own path, ignore it. */
              if (strcmp (change->path, hashkey) == 0)
                continue;

              /* If we come across a child of our path, remove it. */
              if (svn_path_is_child (change->path, hashkey, iterpool))
                apr_hash_set (changed_paths, hashkey, klen, NULL);
            }
        }

      SVN_ERR (read_change (&change, revision_file, pool));

      /* Clear the per-iteration subpool. */
      svn_pool_clear (iterpool);
    }

  /* Destroy the per-iteration subpool. */
  svn_pool_destroy (iterpool);

  /* Close the revision file. */
  SVN_ERR (svn_io_file_close (revision_file, pool));

  *changed_paths_p = changed_paths;

  return SVN_NO_ERROR;
}

/* Copy a revision node-rev SRC into the current transaction TXN_ID in
   the filesystem FS.  Allocations are from POOL.  */
static svn_error_t *
create_new_txn_noderev_from_rev (svn_fs_t *fs,
                                 const char *txn_id,
                                 svn_fs_id_t *src,
                                 apr_pool_t *pool)
{
  svn_fs__node_revision_t *noderev;
  svn_fs_id_t *my_id;

  SVN_ERR (svn_fs__fs_get_node_revision (&noderev, fs, src, pool));

  if (svn_fs__id_txn_id (noderev->id))
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Copying from transactions not allowed.");

  noderev->predecessor_id = noderev->id;
  noderev->predecessor_count++;
  noderev->copyfrom_path = NULL;
  noderev->copyfrom_rev = SVN_INVALID_REVNUM;

  /* For the transaction root, the copyroot never changes. */

  my_id = svn_fs__id_copy (noderev->id, pool);
  my_id->txn_id = apr_pstrdup (pool, txn_id);
  my_id->rev = SVN_INVALID_REVNUM;
  noderev->id = my_id;

  SVN_ERR (svn_fs__fs_put_node_revision (fs, noderev->id, noderev, pool));

  return SVN_NO_ERROR;
}

svn_error_t *svn_fs__fs_begin_txn (svn_fs_txn_t **txn_p,
                                   svn_fs_t *fs,
                                   svn_revnum_t rev,
                                   apr_pool_t *pool)
{
  apr_file_t *txn_file, *next_ids_file;
  svn_stream_t *next_ids_stream;
  svn_fs_txn_t *txn;
  svn_fs_id_t *root_id;
  char *txn_filename = svn_path_join_many (pool, fs->fs_path,
                                           SVN_FS_FS__TXNS_DIR, "XXXXXX",
                                           NULL);
  char *txn_dirname;

  /* Create a temporary file so that we have a unique txn_id, then
     make a directory based on this name.  They will both be removed
     when the transaction is aborted or removed. */
  if (apr_file_mktemp (&txn_file, txn_filename, 0, pool) != APR_SUCCESS)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Unable to create new transaction.");

  /* Create the transaction directory based on this temporary file. */
  txn_dirname = apr_pstrcat (pool, txn_filename, SVN_FS_FS__TXNS_EXT, NULL);

  SVN_ERR (svn_io_make_dir_recursively (txn_dirname, pool));

  SVN_ERR (svn_io_file_close (txn_file, pool));

  txn = apr_pcalloc (pool, sizeof (*txn));

  txn->fs = fs;
  txn->base_rev = rev;

  /* Get the txn_id. */
  svn_path_split (txn_filename, NULL, &txn->id, pool);
  *txn_p = txn;
  
  /* Create a new root node for this transaction. */
  SVN_ERR (svn_fs__fs_rev_get_root (&root_id, fs, rev, pool));
  SVN_ERR (create_new_txn_noderev_from_rev (fs, txn->id, root_id, pool));

  /* Create an empty rev file. */
  SVN_ERR (svn_io_file_create (svn_path_join (txn_dirname, SVN_FS_FS__REV,
                                              pool),
                               "", pool));
  
  /* Write the next-ids file. */
  SVN_ERR (svn_io_file_open (&next_ids_file,
                             svn_path_join (txn_dirname, SVN_FS_FS__NEXT_IDS,
                                            pool),
                             APR_WRITE | APR_CREATE | APR_TRUNCATE,
                             APR_OS_DEFAULT, pool));

  next_ids_stream = svn_stream_from_aprfile (next_ids_file, pool);

  SVN_ERR (svn_stream_printf (next_ids_stream, pool, "0 0\n"));

  SVN_ERR (svn_io_file_close (next_ids_file, pool));

  return SVN_NO_ERROR;
}

/* Store the property list for transaction TXN_ID in PROPLIST and also
   store the filename of the transaction file in *FILENAME as seen in
   filesystem FS.  Perform temporary allocations in POOL. */
static svn_error_t *
get_txn_proplist (const char **filename,
                  apr_hash_t *proplist,
                  svn_fs_t *fs,
                  const char *txn_id,
                  apr_pool_t *pool)
{
  apr_file_t *txn_prop_file;
  const char *prop_filename;

  prop_filename = svn_path_join_many (pool, fs->fs_path,
                                      SVN_FS_FS__TXNS_DIR,
                                      apr_pstrcat (pool, txn_id,
                                                   SVN_FS_FS__TXNS_EXT, NULL),
                                      SVN_FS_FS__TXNS_PROPS, NULL);

  /* Open the transaction properties file. */
  SVN_ERR (svn_io_file_open (&txn_prop_file, prop_filename,
                             APR_READ | APR_CREATE, APR_OS_DEFAULT, pool));

  /* Read in the property list. */
  SVN_ERR (svn_hash_read (proplist, txn_prop_file, pool));

  SVN_ERR (svn_io_file_close (txn_prop_file, pool));

  if (filename)
    *filename = prop_filename;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_change_txn_prop (svn_fs_txn_t *txn,
                            const char *name,
                            const svn_string_t *value,
                            apr_pool_t *pool)
{
  apr_file_t *txn_prop_file;
  const char *prop_filename;
  apr_hash_t *txn_prop = apr_hash_make (pool);

  SVN_ERR (get_txn_proplist (&prop_filename, txn_prop, txn->fs, txn->id,
                             pool));

  apr_hash_set (txn_prop, name, APR_HASH_KEY_STRING, value);

  /* Create a new version of the file and write out the new props. */
  /* Open the transaction properties file. */
  SVN_ERR (svn_io_file_open (&txn_prop_file, prop_filename,
                             APR_READ | APR_WRITE | APR_CREATE | APR_TRUNCATE,
                             APR_OS_DEFAULT, pool));

  SVN_ERR (svn_hash_write (txn_prop, txn_prop_file, pool));

  SVN_ERR (svn_io_file_close (txn_prop_file, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_get_txn (svn_fs__transaction_t **txn_p,
                    svn_fs_t *fs,
                    const char *txn_id,
                    apr_pool_t *pool)
{
  svn_fs__transaction_t *txn;
  svn_fs__node_revision_t *noderev;
  svn_fs_id_t *root_id;

  txn = apr_pcalloc (pool, sizeof (*txn));
  txn->revision = SVN_INVALID_REVNUM;
  txn->proplist = apr_hash_make (pool);

  SVN_ERR (get_txn_proplist (NULL, txn->proplist, fs, txn_id, pool));
  root_id = svn_fs__create_id ("0", "0",
                               apr_pstrcat (pool, "t", txn_id, NULL),
                               pool);

  SVN_ERR (svn_fs__fs_get_node_revision (&noderev, fs, root_id, pool));

  txn->root_id = svn_fs__id_copy (noderev->id, pool);
  txn->base_id = svn_fs__id_copy (noderev->predecessor_id, pool);
  txn->copies = NULL;

  txn->kind = svn_fs__transaction_kind_normal;

  *txn_p = txn;

  return SVN_NO_ERROR;
}

/* Write out the currently available next node_id NODE_ID and copy_id
   COPY_ID for transaction TXN_ID in filesystem FS.  Perform temporary
   allocations in POOL. */
static svn_error_t *
write_next_ids (svn_fs_t *fs,
                const char *txn_id,
                const char *node_id,
                const char *copy_id,
                apr_pool_t *pool)
{
  const char *id_filename;
  apr_file_t *file;
  svn_stream_t *out_stream;

  id_filename = svn_path_join_many (pool, fs->fs_path, SVN_FS_FS__TXNS_DIR,
                                    apr_pstrcat (pool, txn_id,
                                                 SVN_FS_FS__TXNS_EXT, NULL),
                                    SVN_FS_FS__NEXT_IDS, NULL);

  SVN_ERR (svn_io_file_open (&file, id_filename, APR_WRITE | APR_TRUNCATE,
                             APR_OS_DEFAULT, pool));

  out_stream = svn_stream_from_aprfile (file, pool);

  SVN_ERR (svn_stream_printf (out_stream, pool, "%s %s\n", node_id, copy_id));

  SVN_ERR (svn_stream_close (out_stream));

  return SVN_NO_ERROR;
}

/* Find out what the next unique node-id and copy-id are for
   transaction TXN_ID in filesystem FS.  Store the results in *NODE_ID
   and *COPY_ID.  Perform all allocations in POOL. */
static svn_error_t *
read_next_ids (const char **node_id,
               const char **copy_id,
               svn_fs_t *fs,
               const char *txn_id,
               apr_pool_t *pool)
{
  apr_file_t *file;
  const char *id_filename;
  char buf[SVN_FS__MAX_KEY_SIZE*2+3];
  apr_size_t limit;
  char *str, *last_str;

  id_filename = svn_path_join_many (pool, fs->fs_path, SVN_FS_FS__TXNS_DIR,
                                    apr_pstrcat (pool, txn_id,
                                                 SVN_FS_FS__TXNS_EXT, NULL),
                                    SVN_FS_FS__NEXT_IDS, NULL);

  SVN_ERR (svn_io_file_open (&file, id_filename, APR_READ, APR_OS_DEFAULT,
                             pool));

  limit = sizeof (buf);
  SVN_ERR (svn_io_read_length_line (file, buf, &limit, pool));

  SVN_ERR (svn_io_file_close (file, pool));

  /* Parse this into two separate strings. */

  str = apr_strtok (buf, " ", &last_str);
  if (! str)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "next-id file corrupt");

  *node_id = apr_pstrdup (pool, str);

  str = apr_strtok (NULL, " ", &last_str);
  if (! str)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "next-id file corrupt");

  *copy_id = apr_pstrdup (pool, str);

  return SVN_NO_ERROR;
}

/* Get a new and unique to this transaction node-id for transaction
   TXN_ID in filesystem FS.  Store the new node-id in *NODE_ID_P.
   Perform all allocations in POOL. */
static svn_error_t *
get_new_txn_node_id (const char **node_id_p,
                     svn_fs_t *fs,
                     const char *txn_id,
                     apr_pool_t *pool)
{
  const char *cur_node_id, *cur_copy_id;
  char *node_id;
  apr_size_t len;

  /* First read in the current next-ids file. */
  SVN_ERR (read_next_ids (&cur_node_id, &cur_copy_id, fs, txn_id, pool));

  node_id = apr_pcalloc (pool, strlen (cur_node_id) + 2);

  len = strlen(cur_node_id);
  svn_fs__next_key (cur_node_id, &len, node_id);

  SVN_ERR (write_next_ids (fs, txn_id, node_id, cur_copy_id, pool));

  *node_id_p = apr_pstrcat (pool, "_", cur_node_id, NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_create_node (const svn_fs_id_t **id_p,
                        svn_fs_t *fs,
                        svn_fs__node_revision_t *noderev,
                        const char *copy_id,
                        const char *txn_id,
                        apr_pool_t *pool)
{
  const char *node_id;
  const svn_fs_id_t *id;

  /* Get a new node-id for this node. */
  SVN_ERR (get_new_txn_node_id (&node_id, fs, txn_id, pool));

  id = svn_fs__create_id (node_id, copy_id,
                          apr_pstrcat (pool, "t", txn_id, NULL),
                          pool);

  noderev->id = id;

  SVN_ERR (svn_fs__fs_put_node_revision (fs, noderev->id, noderev, pool));

  *id_p = id;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_purge_txn (svn_fs_t *fs,
                      const char *txn_id,
                      apr_pool_t *pool)
{
  /* No-op for debugging purposes. */
  /*
  txn_dir = svn_path_join_many (pool, fs->fs_path, SVN_FS_FS__TXNS_DIR,
                                apr_pstrcat (pool, txn_id,
                                             SVN_FS_FS__TXNS_EXT, NULL), NULL);

  SVN_ERR (svn_io_remove_dir (txn_dir, pool));
  */
  return SVN_NO_ERROR;
}

/* Given a hash ENTRIES of dirent structions, return a hash in
   *STR_ENTRIES_P, that has svn_string_t as the values in the format
   specified by the fs_fs directory contents file.  Perform
   allocations in POOL. */
static svn_error_t *
unparse_dir_entries (apr_hash_t **str_entries_p,
                     apr_hash_t *entries,
                     apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  *str_entries_p = apr_hash_make (pool);

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_fs_dirent_t *dirent;
      const svn_string_t *new_val;

      apr_hash_this (hi, &key, &klen, &val);

      dirent = val;

      new_val = svn_string_createf (pool, "%s %s",
                                    (dirent->kind == svn_node_file) ?
                                    SVN_FS_FS__FILE : SVN_FS_FS__DIR,
                                    svn_fs_unparse_id (dirent->id,
                                                       pool)->data);

      apr_hash_set (*str_entries_p, key, klen, new_val);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__fs_set_entry (svn_fs_t *fs,
                      const char *txn_id,
                      svn_fs__node_revision_t *parent_noderev,
                      const char *name,
                      const svn_fs_id_t *id,
                      svn_node_kind_t kind,
                      apr_pool_t *pool)
{
  apr_hash_t *entries, *str_entries;
  svn_fs_dirent_t *dirent = NULL;
  svn_stream_t *out_stream;

  /* First read in the existing directory entry. */
  SVN_ERR (svn_fs__fs_rep_contents_dir (&entries, fs, parent_noderev, pool));

  if (id)
    {
      dirent = apr_pcalloc (pool, sizeof (*dirent));
      dirent->id = svn_fs__id_copy (id, pool);
      dirent->kind = kind;
    }
  apr_hash_set (entries, name, APR_HASH_KEY_STRING, dirent);

  SVN_ERR (unparse_dir_entries (&str_entries, entries, pool));
  SVN_ERR (svn_fs__fs_set_contents (&out_stream, fs, parent_noderev, pool));
  SVN_ERR (hash_write (str_entries, out_stream, pool));
  SVN_ERR (svn_stream_close (out_stream));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_add_change (svn_fs_t *fs,
                       const char *txn_id,
                       const char *path,
                       const svn_fs_id_t *id,
                       svn_fs_path_change_kind_t change_kind,
                       svn_boolean_t text_mod,
                       svn_boolean_t prop_mod,
                       apr_pool_t *pool)
{
  apr_file_t *file;
  svn_stream_t *stream;
  const char *txn_dir, *change_string;

  txn_dir = svn_path_join_many (pool, fs->fs_path, SVN_FS_FS__TXNS_DIR,
                                apr_pstrcat (pool, txn_id,
                                             SVN_FS_FS__TXNS_EXT, NULL),
                                NULL);

  SVN_ERR (svn_io_file_open (&file, svn_path_join (txn_dir, SVN_FS_FS__CHANGES,
                                                   pool),
                             APR_APPEND | APR_WRITE | APR_CREATE,
                             APR_OS_DEFAULT, pool));

  stream = svn_stream_from_aprfile (file, pool);

  switch (change_kind)
    {
    case svn_fs_path_change_modify:
      change_string = SVN_FS_FS__ACTION_MODIFY;
      break;
    case svn_fs_path_change_add:
      change_string = SVN_FS_FS__ACTION_ADD;
      break;
    case svn_fs_path_change_delete:
      change_string = SVN_FS_FS__ACTION_DELETE;
      break;
    case svn_fs_path_change_replace:
      change_string = SVN_FS_FS__ACTION_REPLACE;
      break;
    case svn_fs_path_change_reset:
      change_string = SVN_FS_FS__ACTION_RESET;
      break;
    }

  SVN_ERR (svn_stream_printf (stream, pool, "%s %s %s %s %s\n",
                              svn_fs_unparse_id (id, pool)->data,
                              change_string,
                              text_mod ? SVN_FS_FS__TRUE : SVN_FS_FS__FALSE,
                              prop_mod ? SVN_FS_FS__TRUE : SVN_FS_FS__FALSE,
                              path));

  SVN_ERR (svn_stream_close (stream));

  return SVN_NO_ERROR;
}

/* This baton is used by the representation writing streams.  It keeps
   track of the checksum information as well as the total size of the
   representation so far. */
struct rep_write_baton
{
  /* The FS we are writing to. */
  svn_fs_t *fs;

  /* Location of the representation we are writing. */
  svn_stream_t *rep_stream;

  /* Where is this representation stored. */
  apr_off_t rep_offset;

  /* How many bytes have been written to this rep already. */
  svn_filesize_t rep_size;

  /* The node revision for which we're writing out info. */
  svn_fs__node_revision_t *noderev;

  /* Is this the data representation? */
  svn_boolean_t is_data_rep;

  struct apr_md5_ctx_t md5_context;

  apr_pool_t *pool;
};

/* Handler for the write method of the representation writable stream.
   BATON is a rep_write_baton, DATA is the data to write, and *LEN is
   the length of this data. */
static svn_error_t *
rep_write_contents (void *baton,
                    const char *data,
                    apr_size_t *len)
{
  struct rep_write_baton *b = baton;

  apr_md5_update (&b->md5_context, data, *len);

  SVN_ERR (svn_stream_write (b->rep_stream, data, len));
  b->rep_size += *len;

  return SVN_NO_ERROR;
}

/* Open the correct writable file to append a representation for
   node-id ID in filesystem FS.  If this representation is for a
   directory node's contents, IS_DIRECTORY_CONTENTS should be TRUE.
   Store the resulting writable file in *FILE_P.  Perform allocations
   in POOL. */
static svn_error_t *
open_and_seek_representation_write (apr_file_t **file_p,
                                    svn_fs_t *fs,
                                    const svn_fs_id_t *id,
                                    svn_boolean_t is_directory_contents,
                                    apr_pool_t *pool)
{
  apr_file_t *file;
  const char *txn_dir;

  txn_dir = svn_path_join_many (pool, fs->fs_path, SVN_FS_FS__TXNS_DIR,
                                apr_pstrcat (pool, id->txn_id,
                                             SVN_FS_FS__TXNS_EXT, NULL),
                                NULL);

  /* If this is a normal representation, i.e. not directory contents,
     then just open up the rev file in append mode. */
  if (! is_directory_contents)
    {
      SVN_ERR (svn_io_file_open (&file, svn_path_join (txn_dir,
                                                       SVN_FS_FS__REV,
                                                       pool),
                                 APR_APPEND | APR_WRITE | APR_CREATE,
                                 APR_OS_DEFAULT, pool));
    }
  else
    {
      const char *children_name;

      children_name = svn_path_join (txn_dir,
                                     apr_psprintf (pool, "%s.%s"
                                                   SVN_FS_FS__CHILDREN_EXT,
                                                   id->node_id, id->copy_id),
                                     pool);

      SVN_ERR (svn_io_file_open (&file, children_name,
                                 APR_WRITE | APR_TRUNCATE | APR_CREATE,
                                 APR_OS_DEFAULT, pool));
    }

  *file_p = file;

  return SVN_NO_ERROR;
}

/* Get a rep_write_baton and store it in *WB_P for the representation
   indicated by NODEREV and IS_DATA_REP in filesystem FS.  Perform
   allocations in POOL. */
static svn_error_t *
rep_write_get_baton (struct rep_write_baton **wb_p,
                     svn_fs_t *fs,
                     svn_fs__node_revision_t *noderev,
                     svn_boolean_t is_data_rep,
                     apr_pool_t *pool)
{
  struct rep_write_baton *b;
  apr_file_t *file;

  b = apr_pcalloc (pool, sizeof (*b));

  apr_md5_init (&(b->md5_context));

  b->fs = fs;
  b->pool = pool;
  b->rep_size = 0;
  b->is_data_rep = is_data_rep;
  b->noderev = noderev;

  /* Open the file we are writing to. */
  SVN_ERR (open_and_seek_representation_write (&file, fs, noderev->id,
                                               (noderev->kind == svn_node_dir)
                                               && is_data_rep,
                                               pool));


  b->rep_stream = svn_stream_from_aprfile (file, pool);

  SVN_ERR (svn_stream_printf (b->rep_stream, pool, "\n"));

  b->rep_offset = 0;
  SVN_ERR (svn_io_file_seek (file, APR_CUR, &b->rep_offset, pool));

  /* Write out the REP line. */
  SVN_ERR (svn_stream_printf (b->rep_stream, pool, "PLAIN\n"));

  *wb_p = b;

  return SVN_NO_ERROR;
}

/* Close handler for the representation write stream.  BATON is a
   rep_write_baton.  Writes out a new node-rev that correctly
   references the representation we just finished writing. */
static svn_error_t *
rep_write_contents_close (void *baton)
{
  struct rep_write_baton *b = baton;
  svn_fs__representation_t *rep;

  rep = apr_pcalloc (b->pool, sizeof (*rep));
  rep->offset = b->rep_offset;
  rep->size = b->rep_size;
  rep->expanded_size = b->rep_size;
  rep->txn_id = b->noderev->id->txn_id;
  rep->revision = SVN_INVALID_REVNUM;
  if ((b->noderev->kind == svn_node_dir) && b->is_data_rep)
    rep->is_directory_contents = TRUE;

  apr_md5_final (rep->checksum, &b->md5_context);

  if (b->is_data_rep)
    {
      b->noderev->data_rep = rep;
      if (b->noderev->kind != svn_node_dir)
        SVN_ERR (svn_stream_printf (b->rep_stream, b->pool, "END\n"));
    }
  else
    {
      b->noderev->prop_rep = rep;
    }

  SVN_ERR (svn_stream_close (b->rep_stream));

  /* Write out the new node-rev information. */
  SVN_ERR (svn_fs__fs_put_node_revision (b->fs, b->noderev->id, b->noderev,
                                         b->pool));

  return SVN_NO_ERROR;
}

/* Store a writable stream in *CONTENTS_P that will receive all data
   written and store it as the representation referenced by NODEREV
   and IS_DATA_REP in filesystem FS.  Perform temporary allocations in
   POOL. */
static svn_error_t *
set_representation (svn_stream_t **contents_p,
                    svn_fs_t *fs,
                    svn_fs__node_revision_t *noderev,
                    svn_boolean_t is_data_rep,
                    apr_pool_t *pool)
{
  struct rep_write_baton *wb;

  SVN_ERR (rep_write_get_baton (&wb, fs, noderev, is_data_rep, pool));

  *contents_p = svn_stream_create (wb, pool);
  svn_stream_set_write (*contents_p, rep_write_contents);
  svn_stream_set_close (*contents_p, rep_write_contents_close);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_set_contents (svn_stream_t **stream,
                         svn_fs_t *fs,
                         svn_fs__node_revision_t *noderev,
                         apr_pool_t *pool)
{
  SVN_ERR (set_representation (stream, fs, noderev, TRUE, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_create_successor (const svn_fs_id_t **new_id_p,
                             svn_fs_t *fs,
                             const svn_fs_id_t *old_idp,
                             svn_fs__node_revision_t *new_noderev,
                             const char *copy_id,
                             const char *txn_id,
                             apr_pool_t *pool)
{
  const svn_fs_id_t *id;

  id = svn_fs__create_id (svn_fs__id_node_id (old_idp), copy_id ?
                          copy_id : svn_fs__id_copy_id (old_idp),
                          apr_pstrcat (pool, "t", txn_id, NULL),
                          pool);

  new_noderev->id = id;

  if (! new_noderev->copyroot_path)
    {
    new_noderev->copyroot_path = apr_pstrdup (pool, new_noderev->created_path);
    new_noderev->copyroot_rev = svn_fs__id_rev (new_noderev->id);
    }

  SVN_ERR (svn_fs__fs_put_node_revision (fs, new_noderev->id, new_noderev,
                                         pool));

  *new_id_p = id;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_set_proplist (svn_fs_t *fs,
                         svn_fs__node_revision_t *noderev,
                         apr_hash_t *proplist,
                         apr_pool_t *pool)
{
  svn_stream_t *out_stream;
  
  SVN_ERR (set_representation (&out_stream, fs, noderev, FALSE, pool));
  SVN_ERR (hash_write (proplist, out_stream, pool));
  SVN_ERR (svn_stream_close (out_stream));
  
  return SVN_NO_ERROR;
}

/* Read the 'current' file for filesystem FS and store the next
   available node id in *NODE_ID, and the next available copy id in
   *COPY_ID.  Allocations are performed from POOL. */
static svn_error_t *
get_next_revision_ids (const char **node_id,
                       const char **copy_id,
                       svn_fs_t *fs,
                       apr_pool_t *pool)
{
  apr_file_t *revision_file;
  char buf[80];
  apr_size_t len;
  char *str, *last_str;

  SVN_ERR (svn_io_file_open (&revision_file,
                             svn_path_join (fs->fs_path, SVN_FS_FS__CURRENT,
                                            pool),
                             APR_READ, APR_OS_DEFAULT, pool));

  len = sizeof (buf);
  SVN_ERR (svn_io_read_length_line (revision_file, buf, &len, pool));

  str = apr_strtok (buf, " ", &last_str);
  if (! str)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Corrupt current file.");

  str = apr_strtok (NULL, " ", &last_str);
  if (! str)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Corrupt current file.");

  *node_id = apr_pstrdup (pool, str);

  str = apr_strtok (NULL, " ", &last_str);
  if (! str)
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Corrupt current file.");

  *copy_id = apr_pstrdup (pool, str);

  return SVN_NO_ERROR;
}

/* This baton is used by the stream created for write_hash_rep. */
struct write_hash_baton
{
  svn_stream_t *stream;

  apr_size_t size;

  struct apr_md5_ctx_t md5_context;
};

/* The handler for the write_hash_rep stream.  BATON is a
   write_hash_baton, DATA has the data to write and *LEN is the number
   of bytes to write. */
static svn_error_t *
write_hash_handler (void *baton,
                    const char *data,
                    apr_size_t *len)
{
  struct write_hash_baton *whb = baton;

  apr_md5_update (&whb->md5_context, data, *len);

  SVN_ERR (svn_stream_write (whb->stream, data, len));
  whb->size += *len;

  return SVN_NO_ERROR;
}

/* Write out the hash HASH as a text representation to file FILE.  In
   the process, record the total size of the dump in *SIZE, and the
   md5 digest in CHECKSUM.  Perform temporary allocations in POOL. */
static svn_error_t *
write_hash_rep (apr_size_t *size,
                char checksum[APR_MD5_DIGESTSIZE],
                apr_file_t *file,
                apr_hash_t *hash,
                apr_pool_t *pool)
{
  svn_stream_t *stream;
  struct write_hash_baton *whb;

  whb = apr_pcalloc (pool, sizeof (*whb));

  whb->stream = svn_stream_from_aprfile (file, pool);
  whb->size = 0;
  apr_md5_init (&(whb->md5_context));

  stream = svn_stream_create (whb, pool);
  svn_stream_set_write (stream, write_hash_handler);

  SVN_ERR (svn_stream_printf (whb->stream, pool, "PLAIN\n"));
  
  SVN_ERR (hash_write (hash, stream, pool));

  /* Store the results. */
  apr_md5_final (checksum, &whb->md5_context);
  *size = whb->size;
  
  return SVN_NO_ERROR;
}

/* Copy a node-revision specified by id ID in fileystem FS from a
   transaction into the permanent rev-file FILE.  Return the offset of
   the new node-revision in *OFFSET.  If this is a directory, all
   children are copied as well.  START_NODE_ID and START_COPY_ID are
   the first available node and copy ids for this filesystem.
   Temporary allocations are from POOL. */
static svn_error_t *
write_final_rev (const svn_fs_id_t **new_id_p,
                 apr_file_t *file,
                 svn_revnum_t rev,
                 svn_fs_t *fs,
                 const svn_fs_id_t *id,
                 const char *start_node_id,
                 const char *start_copy_id,
                 apr_pool_t *pool)
{
  svn_fs__node_revision_t *noderev;
  apr_off_t my_offset;
  char my_node_id[SVN_FS__MAX_KEY_SIZE + 2];
  char my_copy_id[SVN_FS__MAX_KEY_SIZE + 2];
  const char *my_txn_id;
  const svn_fs_id_t *new_id;

  *new_id_p = NULL;
  
  /* Check to see if this is a transaction node. */
  if (! id->txn_id)
    return SVN_NO_ERROR;

  SVN_ERR (svn_fs__fs_get_node_revision (&noderev, fs, id, pool));

  if (noderev->kind == svn_node_dir)
    {
      apr_pool_t *subpool;
      apr_hash_t *entries, *str_entries;
      svn_fs_dirent_t *dirent;
      void *val;
      apr_hash_index_t *hi;
      
      /* This is a directory.  Write out all the children first. */
      subpool = svn_pool_create (pool);

      SVN_ERR (svn_fs__fs_rep_contents_dir (&entries, fs, noderev, pool));

      for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
        {
          svn_pool_clear (subpool);
          apr_hash_this (hi, NULL, NULL, &val);
          dirent = val;
          SVN_ERR (write_final_rev (&new_id, file, rev, fs, dirent->id,
                                    start_node_id, start_copy_id, subpool));
          if (new_id && (new_id->rev == rev))
              dirent->id = svn_fs__id_copy (new_id, pool);
        }
      svn_pool_destroy (subpool);

      if (noderev->data_rep && noderev->data_rep->txn_id)
        {
          /* Write out the contents of this directory as a text rep. */
          SVN_ERR (unparse_dir_entries (&str_entries, entries, pool));

          noderev->data_rep->txn_id = NULL;
          noderev->data_rep->revision = rev;
          noderev->data_rep->offset = 0;
          SVN_ERR (svn_io_file_seek (file, APR_CUR, &noderev->data_rep->offset,
                                     pool));
          SVN_ERR (write_hash_rep (&noderev->data_rep->size,
                                   noderev->data_rep->checksum, file,
                                   str_entries, pool));
          noderev->data_rep->expanded_size = noderev->data_rep->size;
        }          
    }
  else
    {
      /* This is a file.  We should make sure the data rep, if it
         exists in a "this" state, gets rewritten to our new revision
         num. */

      if (noderev->data_rep && noderev->data_rep->txn_id)
        {
          noderev->data_rep->txn_id = NULL;
          noderev->data_rep->revision = rev;
        }
    }

  /* Fix up the property reps. */
  if (noderev->prop_rep && noderev->prop_rep->txn_id)
    {
      noderev->prop_rep->txn_id = NULL;
      noderev->prop_rep->revision = rev;
    }

  /* The offset won't be guaranteed to be good until we have written
     something. */
  SVN_ERR (svn_io_file_write_full (file, "\n", 1, NULL, pool));
  
  /* Convert our temporary ID into a permanent revision one. */
  my_offset = 0;
  SVN_ERR (svn_io_file_seek (file, APR_CUR, &my_offset, pool));
  
  if (noderev->id->node_id[0] == '_')
    {
      svn_fs__add_keys (start_node_id, &noderev->id->node_id[1], my_node_id);
    }
  else
    {
      strcpy (my_node_id, noderev->id->node_id);
    }

  if (noderev->id->copy_id[0] == '_')
    {
      svn_fs__add_keys (start_copy_id, &noderev->id->copy_id[1], my_copy_id);
    }
  else
    {
      strcpy (my_copy_id, noderev->id->copy_id);
    }

  if (noderev->copyroot_rev == SVN_INVALID_REVNUM)
    noderev->copyroot_rev = rev;

  my_txn_id = apr_psprintf (pool, "r%" SVN_REVNUM_T_FMT "/%" APR_OFF_T_FMT,
                            rev, my_offset);

  new_id = svn_fs__create_id (my_node_id, my_copy_id, my_txn_id, pool);

  noderev->id = new_id;

  /* Write out our new node-revision. */
  SVN_ERR (write_noderev_txn (file, noderev, pool));

  SVN_ERR (svn_fs__fs_put_node_revision (fs, id, noderev, pool));

  /* Return our ID that references the revision file. */
  *new_id_p = noderev->id;

  return SVN_NO_ERROR;
}

/* Write the changed path info from transaction TXN_ID in filesystem
   FS to the permanent rev-file FILE.  *OFFSET_P is set the to offset
   in the file of the beginning of this information.  Perform
   temporary allocations in POOL. */
static svn_error_t *
write_final_changed_path_info (apr_off_t *offset_p,
                               apr_file_t *file,
                               svn_fs_t *fs,
                               const char *txn_id,
                               apr_pool_t *pool)
{
  apr_file_t *changes_file;
  const char *txn_dir;
  svn_stream_t *changes_stream;
  apr_pool_t *iterpool = svn_pool_create (pool);
  apr_off_t offset;
  
  SVN_ERR (svn_io_file_write_full (file, "\n", 1, NULL, pool));
  offset = 0;
  SVN_ERR (svn_io_file_seek (file, APR_CUR, &offset, pool));
  
  txn_dir = svn_path_join_many (pool, fs->fs_path, SVN_FS_FS__TXNS_DIR,
                                apr_pstrcat (pool, txn_id,
                                             SVN_FS_FS__TXNS_EXT, NULL),
                                NULL);

  SVN_ERR (svn_io_file_open (&changes_file, svn_path_join (txn_dir,
                                                           SVN_FS_FS__CHANGES,
                                                           pool),
                             APR_READ, APR_OS_DEFAULT, pool));

  changes_stream = svn_stream_from_aprfile (changes_file, pool);
  
  /* Read the lines in one at a time, and convert the temporary
     node-id into a permanent one for each change entry. */
  while (1)
    {
      char *str, *last_str;
      svn_stringbuf_t *line, *buf;
      svn_boolean_t eof;
      svn_fs__node_revision_t *noderev;
      svn_fs_id_t *id;

      svn_pool_clear (iterpool);
      
      SVN_ERR (svn_stream_readline (changes_stream, &line, "\n", &eof, iterpool));

      /* Check of end of file. */
      if (eof)
        break;

      /* Get the temporary node-id. */
      str = apr_strtok (line->data, " ", &last_str);

      if (! str)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Malformed changes line.");

      id = svn_fs_parse_id (str, strlen (str), iterpool);

      SVN_ERR (svn_fs__fs_get_node_revision (&noderev, fs, id, iterpool));

      /* noderev has the permanent node-id at this point, so we just
         substitute it for the temporary one. */

      buf = svn_stringbuf_createf (iterpool, "%s %s\n",
                                   svn_fs_unparse_id (noderev->id,
                                                      iterpool)->data,
                                   last_str);

      SVN_ERR (svn_io_file_write_full (file, buf->data, buf->len, NULL,
                                       iterpool));
    }

  svn_pool_destroy (iterpool);

  SVN_ERR (svn_stream_close (changes_stream));

  *offset_p = offset;
  
  return SVN_NO_ERROR;
}

/* Update the current file to hold the correct next node and copy_ids
   from transaction TXN_ID in filesystem FS.  The current revision is
   set to REV.  Perform temporary allocations in POOL. */
static svn_error_t *
write_final_current (svn_fs_t *fs,
                     const char *txn_id,
                     svn_revnum_t rev,
                     const char *start_node_id,
                     const char *start_copy_id,
                     apr_pool_t *pool)
{
  const char *txn_node_id, *txn_copy_id;
  char new_node_id[SVN_FS__MAX_KEY_SIZE + 2];
  char new_copy_id[SVN_FS__MAX_KEY_SIZE + 2];
  char *buf;
  apr_file_t *file;
  
  /* To find the next available ids, we add the id that used to be in
     the current file, to the next ids from the transaction file. */
  SVN_ERR (read_next_ids (&txn_node_id, &txn_copy_id, fs, txn_id, pool));

  svn_fs__add_keys (start_node_id, txn_node_id, new_node_id);
  svn_fs__add_keys (start_copy_id, txn_copy_id, new_copy_id);

  /* Now we can just write out this line. */
  buf = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT " %s %s\n", rev, new_node_id,
                      new_copy_id);

  SVN_ERR (svn_io_file_open (&file,
                             svn_path_join (fs->fs_path, SVN_FS_FS__CURRENT,
                                            pool),
                             APR_WRITE | APR_TRUNCATE, APR_OS_DEFAULT, pool));

  SVN_ERR (svn_io_file_write_full (file, buf, strlen (buf), NULL, pool));

  SVN_ERR (svn_io_file_close (file, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
get_write_lock (svn_fs_t *fs,
                apr_pool_t *pool)
{
  const char *lock_filename;
  svn_node_kind_t kind;
  
  lock_filename = svn_path_join (fs->fs_path, SVN_FS_FS__LOCK_FILE, pool);

  SVN_ERR (svn_io_check_path (lock_filename, &kind, pool));
  if ((kind == svn_node_unknown) || (kind == svn_node_none))
    SVN_ERR (svn_io_file_create (lock_filename, "", pool));
  
  SVN_ERR (svn_io_file_lock (lock_filename, TRUE, pool));
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_commit (svn_revnum_t *new_rev_p,
                   svn_fs_t *fs,
                   svn_fs_txn_t *txn,
                   apr_pool_t *pool)
{
  const char *rev_filename, *proto_filename;
  const char *revprop_filename, *final_revprop;
  const svn_fs_id_t *root_id, *new_root_id;
  const char *start_node_id, *start_copy_id;
  svn_revnum_t new_rev;
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_file_t *rev_file;
  apr_off_t  changed_path_offset;
  char *buf;

  /* First grab a write lock. */
  SVN_ERR (get_write_lock (fs, subpool));

  /* Get the current youngest revision. */
  SVN_ERR (svn_fs__fs_youngest_revision (&new_rev, fs, subpool));

  /* Get the next node_id and copy_id to use. */
  SVN_ERR (get_next_revision_ids (&start_node_id, &start_copy_id, fs,
                                  subpool));

  /* We are going to be one better than this puny old revision. */
  new_rev++;

  /* Copy the proto revision file into place. */
  rev_filename = svn_path_join_many (subpool, fs->fs_path, SVN_FS_FS__REVS_DIR,
                                     apr_psprintf (subpool, "%" SVN_REVNUM_T_FMT,
                                                   new_rev),
                                     NULL);

  proto_filename = svn_path_join_many (subpool, fs->fs_path,
                                       SVN_FS_FS__TXNS_DIR,
                                       apr_pstrcat (subpool, txn->id,
                                                    SVN_FS_FS__TXNS_EXT, NULL),
                                       SVN_FS_FS__REV, NULL);

  SVN_ERR (svn_io_copy_file (proto_filename, rev_filename, TRUE, subpool));

  /* Get a write handle on the proto revision file. */
  SVN_ERR (svn_io_file_open (&rev_file, rev_filename,
                             APR_WRITE | APR_APPEND, APR_OS_DEFAULT, subpool));

  /* Write out all the node-revisions and directory contents. */
  root_id = svn_fs__create_id ("0", "0",
                               apr_pstrcat (subpool, "t", txn->id, NULL),
                               subpool);
  SVN_ERR (write_final_rev (&new_root_id, rev_file, new_rev, fs, root_id,
                            start_node_id, start_copy_id, subpool));

  /* Write the changed-path information. */
  SVN_ERR (write_final_changed_path_info (&changed_path_offset, rev_file, fs,
                                          txn->id, subpool));

  /* Write the final line. */
  buf = apr_psprintf(subpool, "\n%" APR_OFF_T_FMT " %" APR_OFF_T_FMT "\n",
                     new_root_id->offset, changed_path_offset);
  SVN_ERR (svn_io_file_write_full (rev_file, buf, strlen (buf), NULL,
                                   subpool));
  
  SVN_ERR (svn_io_file_close (rev_file, subpool));

  /* Move the revision properties into place. */
  revprop_filename = svn_path_join_many (subpool, fs->fs_path,
                                         SVN_FS_FS__TXNS_DIR,
                                         apr_pstrcat (subpool, txn->id,
                                                      SVN_FS_FS__TXNS_EXT, NULL),
                                         SVN_FS_FS__PROPS, NULL);

  final_revprop = svn_path_join_many (subpool, fs->fs_path,
                                      SVN_FS_FS__REVPROPS_DIR,
                                      apr_psprintf (pool, "%" SVN_REVNUM_T_FMT,
                                                    new_rev),
                                      NULL);

  SVN_ERR (svn_io_copy_file (revprop_filename, final_revprop, TRUE, subpool));
  
  /* Update the 'current' file. */
  SVN_ERR (write_final_current (fs, txn->id, new_rev, start_node_id,
                                start_copy_id, pool));

  /* Remove this transaction directory. */

  /* Destroy our subpool and release the lock. */
  svn_pool_destroy (subpool);

  *new_rev_p = new_rev;
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_reserve_copy_id (const char **copy_id_p,
                            svn_fs_t *fs,
                            const char *txn_id,
                            apr_pool_t *pool)
{
  const char *cur_node_id, *cur_copy_id;
  char *copy_id;
  apr_size_t len;

  /* First read in the current next-ids file. */
  SVN_ERR (read_next_ids (&cur_node_id, &cur_copy_id, fs, txn_id, pool));

  copy_id = apr_pcalloc (pool, strlen (cur_copy_id) + 2);

  len = strlen(cur_copy_id);
  svn_fs__next_key (cur_copy_id, &len, copy_id);

  SVN_ERR (write_next_ids (fs, txn_id, cur_node_id, copy_id, pool));

  *copy_id_p = apr_pstrcat (pool, "_", cur_copy_id, NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_create (svn_fs_t *fs,
                   const char *path,
                   apr_pool_t *pool)
{
  char buffer [APR_UUID_FORMATTED_LENGTH + 1];
  apr_uuid_t uuid;
  
  SVN_ERR (svn_io_make_dir_recursively (svn_path_join (path,
                                                       SVN_FS_FS__REVS_DIR,
                                                       pool),
                                        pool));
  SVN_ERR (svn_io_make_dir_recursively (svn_path_join (path,
                                                       SVN_FS_FS__REVPROPS_DIR,
                                                       pool),
                                        pool));

  SVN_ERR (svn_io_make_dir_recursively (svn_path_join (path,
                                                       SVN_FS_FS__TXNS_DIR,
                                                       pool),
                                        pool));

  SVN_ERR (svn_io_file_create (svn_path_join (path,
                                              SVN_FS_FS__CURRENT, pool),
                               "0 1 1\n", pool));

  fs->fs_path = apr_pstrdup (pool, path);

  apr_uuid_get (&uuid);
  apr_uuid_format (buffer, &uuid);
  svn_fs__fs_set_uuid (fs, buffer, pool);
  
  SVN_ERR (svn_fs__dag_init_fs (fs));

  return SVN_NO_ERROR;
}

svn_error_t *svn_fs__fs_get_uuid (const char **uuid_p,
                                  svn_fs_t *fs,
                                  apr_pool_t *pool)
{
  apr_file_t *uuid_file;
  char buf [APR_UUID_FORMATTED_LENGTH + 2];
  apr_size_t limit;

  SVN_ERR (svn_io_file_open (&uuid_file,
                             svn_path_join (fs->fs_path, SVN_FS_FS__UUID,
                                            pool),
                             APR_READ, APR_OS_DEFAULT, pool));

  limit = sizeof (buf);
  SVN_ERR (svn_io_read_length_line (uuid_file, buf, &limit, pool));
  *uuid_p = apr_pstrdup (pool, buf);
  
  SVN_ERR (svn_io_file_close (uuid_file, pool));

  return SVN_NO_ERROR;
}

svn_error_t *svn_fs__fs_set_uuid (svn_fs_t *fs,
                                  const char *uuid,
                                  apr_pool_t *pool)
{
  apr_file_t *uuid_file;

  SVN_ERR (svn_io_file_open (&uuid_file,
                             svn_path_join (fs->fs_path, SVN_FS_FS__UUID,
                                            pool),
                             APR_WRITE | APR_CREATE | APR_TRUNCATE,
                             APR_OS_DEFAULT, pool));

  SVN_ERR (svn_io_file_write_full (uuid_file, uuid, strlen (uuid), NULL,
                                   pool));
  SVN_ERR (svn_io_file_write_full (uuid_file, "\n", 1, NULL, pool));

  SVN_ERR (svn_io_file_close (uuid_file, pool));
  
  return SVN_NO_ERROR;
}

svn_error_t *svn_fs__fs_write_revision_zero (svn_fs_t *fs)
{
  apr_pool_t *pool = fs->pool;
  const char *rev_filename;

  /* Create the revision 0 rev-file. */
  rev_filename = svn_path_join_many (pool, fs->fs_path, SVN_FS_FS__REVS_DIR,
                                     "0", NULL);

  SVN_ERR (svn_io_file_create (rev_filename, "PLAIN\nEND\nENDREP\n"
                               "id: 0.0.r0/17\n"
                               "type: dir\n"
                               "count: 0\n"
                               "text: 0 0 4 4 "
                               "2d2977d1c96f487abe4a1e202dd03b4e\n"
                               "cpath: /\n"
                               "\n\n17 107\n", pool));

  return SVN_NO_ERROR;
}
