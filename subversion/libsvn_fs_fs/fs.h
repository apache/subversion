/* fs.h : interface to Subversion filesystem, private to libsvn_fs
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

#ifndef SVN_LIBSVN_FS_FS_H
#define SVN_LIBSVN_FS_FS_H

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_md5.h>
#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** The filesystem structure.  ***/

typedef struct
{
  /* A cache of the last directory opened within the filesystem. */
  svn_fs_id_t *dir_cache_id;
  apr_hash_t *dir_cache;
  apr_pool_t *dir_cache_pool;
} fs_fs_data_t;


/* Return a canonicalized version of a filesystem PATH, allocated in
   POOL.  While the filesystem API is pretty flexible about the
   incoming paths (they must be UTF-8 with '/' as separators, but they
   don't have to begin with '/', and multiple contiguous '/'s are
   ignored) we want any paths that are physically stored in the
   underlying database to look consistent.  Specifically, absolute
   filesystem paths should begin with '/', and all redundant and trailing '/'
   characters be removed.  */
const char *
svn_fs_fs__canonicalize_abspath (const char *path, apr_pool_t *pool);


/*** Filesystem Revision ***/
typedef struct
{
  /* id of the transaction that was committed to create this
     revision. */
  const char *txn_id;

} revision_t;


/*** Transaction Kind ***/
typedef enum
{
  transaction_kind_normal = 1,  /* normal, uncommitted */
  transaction_kind_committed,   /* committed */
  transaction_kind_dead         /* uncommitted and dead */

} transaction_kind_t;


/*** Filesystem Transaction ***/
typedef struct
{
  /* kind of transaction. */
  transaction_kind_t kind;

  /* property list (const char * name, svn_string_t * value).
     may be NULL if there are no properties.  */
  apr_hash_t *proplist;

  /* node revision id of the root node.  */
  const svn_fs_id_t *root_id;

  /* node revision id of the node which is the root of the revision
     upon which this txn is base.  (unfinished only) */
  const svn_fs_id_t *base_id;

  /* copies list (const char * copy_ids), or NULL if there have been
     no copies in this transaction.  */
  apr_array_header_t *copies;

} transaction_t;

/*** Copy Kind ***/
typedef enum
{
  copy_kind_real = 1, /* real copy */
  copy_kind_soft      /* soft copy */

} copy_kind_t;


/*** Representation ***/
typedef struct
{
  /* MD5 checksum for the contents produced by this representation.
     This checksum is for the contents the rep shows to consumers,
     regardless of how the rep stores the data under the hood.  It is
     independent of the storage (fulltext, delta, whatever). 

     If all the bytes are 0, then for compatibility behave as though
     this checksum matches the expected checksum. */
  unsigned char checksum[APR_MD5_DIGESTSIZE];

  /* Revision where this representation is located. */
  svn_revnum_t revision;

  /* Offset into the revision file where it is located. */
  apr_off_t offset;

  /* The size of the representation in bytes as seen in the revision
     file. */
  svn_filesize_t size;

  /* The size of the fulltext of the representation. */
  svn_filesize_t expanded_size;

  /* Is this representation a transaction? */
  const char *txn_id;

} representation_t;


/*** Node-Revision ***/
typedef struct
{
  /* node kind */
  svn_node_kind_t kind;

  /* The node-id for this node-rev. */
  const svn_fs_id_t *id;

  /* predecessor node revision id, or NULL if there is no predecessor
     for this node revision */
  const svn_fs_id_t *predecessor_id;

  /* If this node-rev is a copy, where was it copied from? */
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;

  /* Helper for history tracing, root of the parent tree from whence
     this node-rev was copied. */
  svn_revnum_t copyroot_rev;
  const char *copyroot_path;
  
  /* number of predecessors this node revision has (recursively), or
     -1 if not known (for backward compatibility). */
  int predecessor_count;

  /* representation key for this node's properties.  may be NULL if
     there are no properties.  */
  representation_t *prop_rep;

  /* representation for this node's data.  may be NULL if there is
     no data. */
  representation_t *data_rep;
  
  /* path at which this node first came into existence.  */
  const char *created_path;

} node_revision_t;


/*** Representation Kind ***/
typedef enum
{
  rep_kind_fulltext = 1, /* fulltext */
  rep_kind_delta         /* delta */

} rep_kind_t;


/*** "Delta" Offset/Window Chunk ***/
typedef struct 
{
  /* diff format version number ### at this point, "svndiff" is the
     only format used. */
  apr_byte_t version;

  /* starting offset of the data represented by this chunk */
  svn_filesize_t offset;

  /* string-key to which this representation points. */
  const char *string_key; 

  /* size of the fulltext data represented by this delta window. */
  apr_size_t size;

  /* represenatation-key to use when needed source data for
     undeltification. */
  const char *rep_key;

  /* apr_off_t rep_offset;  ### not implemented */

} rep_delta_chunk_t;




/*** Copy ***/
typedef struct
{
  /* What kind of copy occurred. */
  copy_kind_t kind;

  /* Path of copy source. */
  const char *src_path;

  /* Transaction id of copy source. */
  const char *src_txn_id;

  /* Node-revision of copy destination. */
  const svn_fs_id_t *dst_noderev_id;

} copy_t;


/*** Change ***/
typedef struct
{
  /* Path of the change. */
  const char *path;

  /* Node revision ID of the change. */
  const svn_fs_id_t *noderev_id;

  /* The kind of change. */
  svn_fs_path_change_kind_t kind;

  /* Text or property mods? */
  svn_boolean_t text_mod;
  svn_boolean_t prop_mod;

  /* Copyfrom revision and path. */
  svn_revnum_t copyfrom_rev;
  const char * copyfrom_path;

} change_t;


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_FS_H */
