/* fs.h : interface to Subversion filesystem, private to libsvn_fs
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#include "db.h"                 /* Berkeley DB interface */
#include "apr_pools.h"
#include "apr_hash.h"
#include "svn_fs.h"
#include "apr_md5.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** The filesystem structure.  ***/

struct svn_fs_t 
{
  /* A pool managing this filesystem.  Freeing this pool must
     completely clean up the filesystem, including any database
     or system resources it holds.  */
  apr_pool_t *pool;

  /* The path to the repository's top-level directory. */
  char *path;

  /* A Berkeley DB environment for all the filesystem's databases.
     This establishes the scope of the filesystem's transactions.  */
  DB_ENV *env;

  /* The filesystem's various tables.  See `structure' for details.  */
  DB *nodes, *revisions, *transactions, *representations, *strings;

  /* A callback function for printing warning messages, and a baton to
     pass through to it.  */
  svn_fs_warning_callback_t warning;
  void *warning_baton;

  /* A kludge for handling errors noticed by APR pool cleanup functions.

     The APR pool cleanup functions can only return an apr_status_t
     value, not a full svn_error_t value.  This makes it difficult to
     propagate errors detected by fs_cleanup to someone who can handle
     them.

     If FS->cleanup_error is non-zero, it points to a location where
     fs_cleanup should store a pointer to an svn_error_t object, if it
     generates one.  Normally, it's zero, but if the cleanup is
     invoked by code prepared to deal with an svn_error_t object in
     some helpful way, it can create its own svn_error_t *, set it to
     zero, set cleanup_error to point to it, free the pool (thus
     invoking the cleanup), and then check its svn_error_t to see if
     anything went wrong.

     Of course, if multiple errors occur, this will only report one of
     them, but it's better than nothing.  In the case of a cascade,
     the first error message is probably the most helpful, so
     fs_cleanup won't overwrite a pointer to an existing svn_error_t
     if it finds one.  */
  svn_error_t **cleanup_error;
};


/*** Filesystem Revision ***/
typedef struct
{
  /* id of the root node. */
  svn_fs_id_t *id;

  /* property list (const char * name, svn_string_t * value) 
     may be NULL if there are no properies.  */
  apr_hash_t *proplist; 

} svn_fs__revision_t;


/*** Filesystem Transaction ***/
typedef struct
{
  /* id of the root node */
  svn_fs_id_t *root_id;

  /* id of the revision root node upon which this txn is base */
  svn_fs_id_t *base_root_id;

  /* property list (const char * name, svn_string_t * value).
     may be NULL if there are no properties.  */
  apr_hash_t *proplist;

} svn_fs__transaction_t;


/*** Node-Revision ***/
typedef struct
{
  /* node kind */
  svn_node_kind_t kind;

  /* revision in which this node was committed (< 1 here means this node
     is mutable -- not yet committed */
  svn_revnum_t revision;

  /* ancestor path/revision */
  const char *ancestor_path;
  svn_revnum_t ancestor_rev;

  /* representation key for this node's properties.  may be NULL if
     there are no properties.  */
  const char *prop_key;

  /* representation key for this node's text data (files) or entries
     list (dirs).  may be NULL if there are no contents.  */
  const char *data_key;

  /* representation key for this node's text-data-in-progess (files
     only).  NULL if no edits are currently in-progress. */
  const char *edit_data_key;

} svn_fs__node_revision_t;


/*** Representation Kind ***/
typedef enum
{
  svn_fs__rep_kind_fulltext = 1, /* fulltext */
  svn_fs__rep_kind_delta         /* delta */

} svn_fs__rep_kind_t;


/*** "Delta" Offset/Window Chunk ***/
typedef struct 
{
  /* starting offset of the data represented by this chunk */
  apr_size_t offset;

  /* string-key to which this representation points. */
  const char *string_key; 

  /* size of the fulltext data represented by this delta window. */
  apr_size_t size;

  /* MD5 checksum of the data */
  unsigned char checksum[MD5_DIGESTSIZE];

  /* represenatation-key to use when needed source data for
     undeltification. */
  const char *rep_key;

  /* apr_off_t rep_offset;  ### not implemented */

} svn_fs__rep_delta_chunk_t;


/*** Representation ***/
typedef struct
{
  /* representation kind */
  svn_fs__rep_kind_t kind;

  /* is this representation mutable? */
  int is_mutable;

  /* kind-specific stuff */
  union 
  {
    /* fulltext stuff */
    struct
    {
      /* string-key which holds the fulltext data */
      const char *string_key;

    } fulltext;

    /* delta stuff */
    struct
    {
      /* an array of svn_fs__rep_delta_chunk_t * chunks of delta
         information */
      apr_array_header_t *chunks;

    } delta;
  } contents;
} svn_fs__representation_t;


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_FS_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
