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

#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_hash.h"
#include "id.h"
#include "fs.h"
#include "err.h"
#include "dag.h"
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

svn_error_t *
svn_fs__fs_open (svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  apr_file_t *uuid_file;
  apr_size_t uuid_size;
  char buffer[APR_UUID_FORMATTED_LENGTH + 1];

  /* Attempt to open the UUID file, which will tell us the UUID of
          this repository, otherwise there isn't much need for specific
          state associated with an open fs_fs repository. */

  fs->fs_path = apr_pstrdup (pool, path);

  SVN_ERR (svn_io_file_open (&uuid_file,
                             svn_path_join (path, SVN_FS_FS__UUID, pool),
                             APR_READ, APR_OS_DEFAULT, pool));

  /* Read in the uuid. */
  uuid_size = APR_UUID_FORMATTED_LENGTH;
  SVN_ERR (svn_io_file_read (uuid_file, buffer, &uuid_size, pool));

  buffer[APR_UUID_FORMATTED_LENGTH] = 0;

  fs->uuid = apr_pstrdup (pool, buffer);

  SVN_ERR (svn_io_file_close (uuid_file, pool));
  
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

/* Parse the four numbers from a text or prop representation
   descriptor in STRING.  The resulting revision, offset, size, and
   expanded size are stored in *REVISION_P, *OFFSET_P, *SIZE_P, and
   *EXPANDED_SIZE_P respectively. */
static svn_error_t *
read_rep_offsets (svn_fs__representation_t **rep_p,
                  char *string,
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
  
  SVN_ERR (open_and_seek_revision (&revision_file,
                                   fs,
                                   svn_fs__id_rev (id),
                                   svn_fs__id_offset (id),
                                   pool));

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
      SVN_ERR (read_rep_offsets (&noderev->prop_rep, value, pool));
    }

  /* Get the data location. */
  value = apr_hash_get (headers, SVN_FS_FS__TEXT, APR_HASH_KEY_STRING);
  if (value)
    {
      SVN_ERR (read_rep_offsets (&noderev->data_rep, value, pool));
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
      noderev->copyroot = NULL;
    }
  else
    {
      noderev->copyroot = svn_fs_parse_id (value, strlen (value), pool);
    }

  /* Get the copyfrom. */
  value = apr_hash_get (headers, SVN_FS_FS__COPYFROM, APR_HASH_KEY_STRING);
  if (value == NULL)
    {
      noderev->copyfrom_path = NULL;
    }
  else
    {
      char *str, *last_str;

      str = apr_strtok (value, " ", &last_str);
      if (str == NULL)
          return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                   "Malformed copyfrom line in node-rev");

      if (strcmp (str, SVN_FS_FS__SOFT) == 0)
        {
          noderev->copykind = svn_fs__copy_kind_soft;
        }
      else if (strcmp (str, SVN_FS_FS__HARD) == 0)
        {
          noderev->copykind = svn_fs__copy_kind_real;
        }
      else
        {
          return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                   "Malformed copyfrom line in node-rev");
        }

      str = apr_strtok (NULL, " ", &last_str);
      if (str == NULL)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Malformed copyfrom line in node-rev");
      noderev->copyfrom_rev = atoi (str);
      
      str = apr_strtok (NULL, " ", &last_str);
      if (str == NULL)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Malformed copyfrom line in node-rev");
      noderev->copyfrom_path = apr_pstrdup (pool, str);
    }

  *noderev_p = noderev;
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_put_node_revision (svn_fs_t *fs,
                              const svn_fs_id_t *id,
                              svn_fs__node_revision_t *noderev,
                              apr_pool_t *pool)
{
  abort();

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
                             APR_READ, APR_OS_DEFAULT, pool));

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
  SVN_ERR (open_and_seek_revision (&b->rep_file, fs, rep->revision, rep->offset,
                                   pool));

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
      b->wstream = svn_txdelta_parse_svndiff (handler, handler_baton, FALSE, pool);

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
      SVN_ERR (rep_read_get_baton (&rb, fs, rep, pool));
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
  SVN_ERR (get_representation_at_offset (contents_p, fs, noderev->data_rep,
                                         pool));
  
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

  SVN_ERR (get_representation_at_offset (&stream, fs, noderev->prop_rep, pool));
  
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
