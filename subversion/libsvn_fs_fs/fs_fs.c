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
  SVN_ERR (svn_io_file_read (uuid_file, buffer,
                             &uuid_size, pool));

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
                             svn_path_join (fs->fs_path, SVN_FS_FS__CURRENT, pool),
                             APR_READ, APR_OS_DEFAULT, pool));

  len = sizeof(buf);
  SVN_ERR (svn_io_file_read (revision_file, buf, &len, pool));

  *youngest_p = atoi(buf);

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
      svn_boolean_t eof;
      apr_size_t i = 0, header_len;
      apr_size_t limit;
      char *local_name, *local_value;

      limit = sizeof(header_str);
      SVN_ERR (svn_io_read_length_line(file, header_str, &limit, pool));

      if (strlen(header_str)==0)
        break; /* end of header block */
      
      header_len = strlen(header_str);

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

      apr_hash_set (*headers, local_name,
                    APR_HASH_KEY_STRING,
                    local_value);
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

  rev_filename = apr_psprintf (pool, "r%" SVN_REVNUM_T_FMT, rev);

  SVN_ERR (svn_io_file_open (&rev_file,
                             svn_path_join (fs->fs_path,
                                            rev_filename, pool),
                             APR_READ, APR_OS_DEFAULT, pool));

  SVN_ERR (svn_io_file_seek (rev_file, APR_SET,
                             &offset, pool));

  *file = rev_file;

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

  noderev = apr_pcalloc (pool, sizeof(*noderev));

  /* Read the node-rev id. */
  value = apr_hash_get (headers, SVN_FS_FS__NODE_ID, APR_HASH_KEY_STRING);

  noderev->id = svn_fs_parse_id (value, strlen(value), pool);

  /* Read the type. */
  value = apr_hash_get (headers, SVN_FS_FS__KIND, APR_HASH_KEY_STRING);

  if ((value == NULL) ||
      (strcmp (value, SVN_FS_FS__FILE)!=0 && strcmp (value, SVN_FS_FS__DIR)))
    return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                             "Missing kind field in node-rev.");

  if (strcmp (value, SVN_FS_FS__FILE)==0)
    {
      noderev->kind = svn_node_file;
    } else {
      noderev->kind = svn_node_dir;
    }

  /* Read the 'count' field. */
  value = apr_hash_get (headers, SVN_FS_FS__COUNT, APR_HASH_KEY_STRING);

  if (value == NULL)
    {
      noderev->predecessor_count = 0;
    } else {
      noderev->predecessor_count = atoi (value);
    }

  /* Get the properties location. */
  value = apr_hash_get (headers, SVN_FS_FS__PROPS, APR_HASH_KEY_STRING);

  if (value == NULL)
    {
      noderev->prop_offset = -1;
    } else {
      char *str, *last_str;

      str = apr_strtok (value, " ", &last_str);
      if (str == NULL)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Malformed props line in node-rev");

      noderev->prop_revision = atoi (str);

      str = apr_strtok (NULL, " ", &last_str);
      if (str == NULL)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Malformed props line in node-rev");

      noderev->prop_offset = apr_atoi64 (str);
    }

  /* Get the data location. */
  value = apr_hash_get (headers, SVN_FS_FS__REP, APR_HASH_KEY_STRING);

  if (value == NULL)
    {
      noderev->data_offset = -1;
    } else {
      char *str, *last_str;

      str = apr_strtok (value, " ", &last_str);
      if (str == NULL)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Malformed rep line in node-rev");

      noderev->data_revision = atoi (str);

      str = apr_strtok (NULL, " ", &last_str);
      if (str == NULL)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Malformed rep line in node-rev");

      noderev->data_offset = apr_atoi64 (str);

      str = apr_strtok (NULL, " ", &last_str);
      if (str == NULL)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Malformed rep line in node-rev");

      noderev->data_size = apr_atoi64 (str);

      str = apr_strtok (NULL, " ", &last_str);
      if (str == NULL)
        return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                                 "Malformed rep line in node-rev");

      noderev->data_expanded_size = apr_atoi64 (str);
    }

  /* Get the created path. */
  value = apr_hash_get (headers, SVN_FS_FS__CPATH, APR_HASH_KEY_STRING);

  if (value == NULL)
    {
      return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                               "Missing cpath in node-rev");
    } else {
      noderev->created_path = apr_pstrdup (pool, value);
    }

  /* Get the copyroot. */
  value = apr_hash_get (headers, SVN_FS_FS__COPYROOT, APR_HASH_KEY_STRING);

  if (value == NULL)
    {
      noderev->copyroot = NULL;
    }
  else
    {
      noderev->copyroot = svn_fs_parse_id (value, strlen(value), pool);
    }

  /* Get the copyfrom. */
  value = apr_hash_get (headers, SVN_FS_FS__COPYFROM, APR_HASH_KEY_STRING);

  if (value == NULL)
    {
      noderev->copyfrom = NULL;
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

      noderev->copyroot = svn_fs_parse_id (str, strlen(str), pool);
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


svn_error_t *
svn_fs__fs_get_proplist (apr_hash_t **proplist_p,
                         svn_fs_t *fs,
                         svn_fs__node_revision_t *noderev,
                         apr_pool_t *pool)
{
  apr_hash_t *proplist;
  apr_file_t *rev_file;

  proplist = apr_hash_make (pool);

  if (noderev->prop_offset == -1)
    {
      *proplist_p = proplist;
      return SVN_NO_ERROR;
    }

  SVN_ERR (open_and_seek_revision (&rev_file,
                                   fs,
                                   noderev->prop_revision,
                                   noderev->prop_offset,
                                   pool));

  SVN_ERR (svn_hash_read (proplist, rev_file, pool));

  SVN_ERR (svn_io_file_close (rev_file, pool));

  *proplist_p = proplist;

  return SVN_NO_ERROR;
}

/* This structure is used to hold the information associated with a
   REP line. */
typedef struct
{
  svn_boolean_t delta_base;
  
  svn_revnum_t delta_revision;
  apr_off_t delta_offset;
  apr_size_t delta_length;
  
} rep_args_t;

/* Read the next line from file FILE and parse it as a text
   representation entry.  Return the parsed entry in REP_ARGS_P.
   Perform all allocations in POOL. */
svn_error_t *
read_rep_line (rep_args_t **rep_args_p,
               apr_file_t *file,
               apr_pool_t *pool)
{
  char buffer[80];
  char *space, *target_length;
  apr_size_t limit;
  svn_boolean_t delta_base = FALSE;
  rep_args_t *rep_args;
  
  limit = sizeof(buffer);
  SVN_ERR (svn_io_read_length_line (file, buffer, &limit, pool));

  rep_args = apr_pcalloc (pool, sizeof (*rep_args));
  rep_args->delta_base = FALSE;

  if (strcmp (buffer, "REP") == 0)
    {
      *rep_args_p = rep_args;
      return SVN_NO_ERROR;
    }

  abort ();

  *rep_args_p = rep_args;
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__fs_rep_contents_dir (apr_hash_t **entries_p,
                             svn_fs_t *fs,
                             svn_fs__node_revision_t *noderev,
                             apr_pool_t *pool)
{
  apr_file_t *rev_file;
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  rep_args_t *rep_args;

  entries = apr_hash_make (pool);
  /* If this node has no data representation. */
  if (noderev->data_offset == -1)
    {
      *entries_p = entries;
      return SVN_NO_ERROR;
    }

  SVN_ERR (open_and_seek_revision (&rev_file,
                                   fs,
                                   noderev->data_revision,
                                   noderev->data_offset,
                                   pool));

  /* Read in the REP line, for directories it should never
     reference a delta-base. */
  SVN_ERR (read_rep_line (&rep_args, rev_file, pool));

  if (rep_args->delta_base == TRUE)
    {
      return svn_error_create (SVN_ERR_FS_CORRUPT, NULL,
                               "Directory has corrupt text entry");
    }

  SVN_ERR (svn_hash_read (entries, rev_file, pool));

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
          dirent->kind == svn_node_dir;
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
  
  SVN_ERR (svn_io_file_seek (rev_file, APR_SET,
                             &offset, pool));

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

  *id_p=id;

  return SVN_NO_ERROR;
}


/* Given an open revision file REV_FILE, locate the trailer that
   specifies the offset to the root node-id.  Store this offset in
   *ROOT_OFFSET.  Allocate temporary variables from POOL. */
static svn_error_t *
get_root_offset (apr_off_t *root_offset,
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
  if (buf[num_bytes-1]!='\n')
    {
            return svn_error_createf
              (SVN_ERR_FS_CORRUPT, NULL,
               "Revision file lacks trailing newline.");
    }

  /* Look for the next previous newline. */
  for (i = num_bytes-2; i >= 0; i--)
    {
      if (buf[i] == '\n') break;
    }

  if (i < 0)
    {
            return svn_error_createf
              (SVN_ERR_FS_CORRUPT, NULL,
               "Final line in revision file longer than 64 characters.");
    }

  *root_offset = apr_atoi64 (&buf[i]);

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

  revision_filename = apr_psprintf (pool, "r%" SVN_REVNUM_T_FMT, rev);

  SVN_ERR (svn_io_file_open (&revision_file,
                             svn_path_join (fs->fs_path,
                                            revision_filename, pool),
                             APR_READ, APR_OS_DEFAULT, pool));

  SVN_ERR (get_root_offset (&root_offset, revision_file, pool));

  SVN_ERR (get_fs_id_at_offset (&root_id, revision_file,
                                root_offset, pool));

  SVN_ERR (svn_io_file_close (revision_file, pool));

  *root_id_p=root_id;
  
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

  revprop_filename = apr_psprintf (pool,
                                   "r%" SVN_REVNUM_T_FMT  SVN_FS_FS__REV_PROPS_EXT,
                                   rev);

  SVN_ERR (svn_io_file_open (&revprop_file,
                             svn_path_join (fs->fs_path,
                                            revprop_filename, pool),
                             APR_READ, APR_OS_DEFAULT, pool));

  proplist = apr_hash_make (pool);

  SVN_ERR (svn_hash_read (proplist, revprop_file, pool));

  SVN_ERR (svn_io_file_close (revprop_file, pool));

  *proplist_p = proplist;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_get_contents (svn_stream_t **contents_p,
                         svn_fs_t *fs,
                         svn_fs__node_revision_t *noderev,
                         apr_pool_t *pool)
{
  abort ();
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs__fs_file_length (svn_filesize_t *length,
                        svn_fs__node_revision_t *noderev,
                        apr_pool_t *pool)
{
  *length = noderev->data_expanded_size;

  return SVN_NO_ERROR;
}

svn_boolean_t
svn_fs__fs_noderev_same_prop_key (svn_fs__node_revision_t *a,
                                  svn_fs__node_revision_t *b)
{
  if (a->prop_offset != b->prop_offset) return FALSE;
  if (a->prop_revision != b->prop_revision) return FALSE;
  return TRUE;
}


svn_boolean_t
svn_fs__fs_noderev_same_data_key (svn_fs__node_revision_t *a,
                                  svn_fs__node_revision_t *b)
{
  if (a->data_offset != b->data_offset) return FALSE;
  if (a->data_revision != b->data_revision) return FALSE;
  return TRUE;
}
