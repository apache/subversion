/* fs_fs.c --- filesystem operations specific to fs_fs
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include <ctype.h>
#include <assert.h>

#include <apr_general.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_uuid.h>
#include <apr_md5.h>
#include <apr_thread_mutex.h>

#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_md5.h"
#include "svn_props.h"
#include "svn_sorts.h"
#include "svn_time.h"

#include "fs.h"
#include "err.h"
#include "tree.h"
#include "lock.h"
#include "key-gen.h"
#include "fs_fs.h"
#include "id.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* Following are defines that specify the textual elements of the
   native filesystem directories and revision files. */

/* Names of special files in the fs_fs filesystem. */
#define PATH_FORMAT        "format"        /* Contains format number */
#define PATH_UUID          "uuid"          /* Contains UUID */
#define PATH_CURRENT       "current"       /* Youngest revision */
#define PATH_LOCK_FILE     "write-lock"    /* Revision lock file */
#define PATH_REVS_DIR      "revs"          /* Directory of revisions */
#define PATH_REVPROPS_DIR  "revprops"      /* Directory of revprops */
#define PATH_TXNS_DIR      "transactions"  /* Directory of transactions */
#define PATH_LOCKS_DIR     "locks"         /* Directory of locks */

/* Names of special files and file extensions for transactions */
#define PATH_CHANGES       "changes"       /* Records changes made so far */
#define PATH_TXN_PROPS     "props"         /* Transaction properties */
#define PATH_NEXT_IDS      "next-ids"      /* Next temporary ID assignments */
#define PATH_REV           "rev"           /* Proto rev file */
#define PATH_REV_LOCK      "rev-lock"      /* Proto rev (write) lock file */
#define PATH_PREFIX_NODE   "node."         /* Prefix for node filename */
#define PATH_EXT_TXN       ".txn"          /* Extension of txn dir */
#define PATH_EXT_CHILDREN  ".children"     /* Extension for dir contents */
#define PATH_EXT_PROPS     ".props"        /* Extension for node props */

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

/* Notes:

To avoid opening and closing the rev-files all the time, it would
probably be advantageous to keep each rev-file open for the
lifetime of the transaction object.  I'll leave that as a later
optimization for now.

I didn't keep track of pool lifetimes at all in this code.  There
are likely some errors because of that.
   
*/

/* The vtable associated with an open transaction object. */
static txn_vtable_t txn_vtable = {
  svn_fs_fs__commit_txn,
  svn_fs_fs__abort_txn,
  svn_fs_fs__txn_prop,
  svn_fs_fs__txn_proplist,
  svn_fs_fs__change_txn_prop,
  svn_fs_fs__txn_root
};

/* Pathname helper functions */

static const char *
path_format(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_path_join(fs->path, PATH_FORMAT, pool);
}

static const char *
path_uuid(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_path_join(fs->path, PATH_UUID, pool);
}

static const char *
path_current(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_path_join(fs->path, PATH_CURRENT, pool);
}

static const char *
path_lock(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_path_join(fs->path, PATH_LOCK_FILE, pool);
}

const char *
svn_fs_fs__path_rev(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  return svn_path_join_many(pool, fs->path, PATH_REVS_DIR,
                            apr_psprintf(pool, "%ld", rev), NULL);
}

static const char *
path_revprops(svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool)
{
  return svn_path_join_many(pool, fs->path, PATH_REVPROPS_DIR,
                            apr_psprintf(pool, "%ld", rev), NULL);
}

static const char *
path_txn_dir(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return svn_path_join_many(pool, fs->path, PATH_TXNS_DIR,
                            apr_pstrcat(pool, txn_id, PATH_EXT_TXN, NULL),
                            NULL);
}

static const char *
path_txn_changes(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return svn_path_join(path_txn_dir(fs, txn_id, pool), PATH_CHANGES, pool);
}

static const char *
path_txn_props(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return svn_path_join(path_txn_dir(fs, txn_id, pool), PATH_TXN_PROPS, pool);
}

static const char *
path_txn_next_ids(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return svn_path_join(path_txn_dir(fs, txn_id, pool), PATH_NEXT_IDS, pool);
}

static const char *
path_txn_proto_rev(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return svn_path_join(path_txn_dir(fs, txn_id, pool), PATH_REV, pool);
}

static const char *
path_txn_proto_rev_lock(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return svn_path_join(path_txn_dir(fs, txn_id, pool), PATH_REV_LOCK, pool);
}

static const char *
path_txn_node_rev(svn_fs_t *fs, const svn_fs_id_t *id, apr_pool_t *pool)
{
  const char *txn_id = svn_fs_fs__id_txn_id(id);
  const char *node_id = svn_fs_fs__id_node_id(id);
  const char *copy_id = svn_fs_fs__id_copy_id(id);
  const char *name = apr_psprintf(pool, PATH_PREFIX_NODE "%s.%s",
                                  node_id, copy_id);

  return svn_path_join(path_txn_dir(fs, txn_id, pool), name, pool);
}

static const char *
path_txn_node_props(svn_fs_t *fs, const svn_fs_id_t *id, apr_pool_t *pool)
{
  return apr_pstrcat(pool, path_txn_node_rev(fs, id, pool), PATH_EXT_PROPS,
                     NULL);
}

static const char *
path_txn_node_children(svn_fs_t *fs, const svn_fs_id_t *id, apr_pool_t *pool)
{
  return apr_pstrcat(pool, path_txn_node_rev(fs, id, pool),
                     PATH_EXT_CHILDREN, NULL);
}



/* Functions for working with shared transaction data. */

/* Return the transaction object for transaction TXN_ID from the
   transaction list of filesystem FS (which must already be locked via the
   txn_list_lock mutex).  If the transaction does not exist in the list,
   then create a new transaction object and return it (if CREATE_NEW is
   true) or return NULL (otherwise). */
static fs_fs_shared_txn_data_t *
get_shared_txn(svn_fs_t *fs, const char *txn_id, svn_boolean_t create_new)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;
  fs_fs_shared_txn_data_t *txn;

  for (txn = ffsd->txns; txn; txn = txn->next)
    if (strcmp(txn->txn_id, txn_id) == 0)
      break;

  if (txn || !create_new)
    return txn;

  /* Use the transaction object from the (single-object) freelist,
     if one is available, or otherwise create a new object. */
  if (ffsd->free_txn)
    {
      txn = ffsd->free_txn;
      ffsd->free_txn = NULL;
    }
  else
    {
      apr_pool_t *subpool = svn_pool_create(ffsd->common_pool);
      txn = apr_palloc(subpool, sizeof(*txn));
      txn->pool = subpool;
    }

  assert(strlen(txn_id) < sizeof(txn->txn_id));
  strcpy(txn->txn_id, txn_id);
  txn->being_written = FALSE;

  /* Link this transaction into the head of the list.  We will typically
     be dealing with only one active transaction at a time, so it makes
     sense for searches through the transaction list to look at the
     newest transactions first.  */
  txn->next = ffsd->txns;
  ffsd->txns = txn;

  return txn;
}

/* Free the transaction object for transaction TXN_ID, and remove it
   from the transaction list of filesystem FS (which must already be
   locked via the txn_list_lock mutex).  Do nothing if the transaction
   does not exist. */
static void
free_shared_txn(svn_fs_t *fs, const char *txn_id)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;
  fs_fs_shared_txn_data_t *txn, *prev = NULL;

  for (txn = ffsd->txns; txn; prev = txn, txn = txn->next)
    if (strcmp(txn->txn_id, txn_id) == 0)
      break;

  if (!txn)
    return;

  if (prev)
    prev->next = txn->next;
  else
    ffsd->txns = txn->next;

  /* As we typically will be dealing with one transaction after another,
     we will maintain a single-object free list so that we can hopefully
     keep reusing the same transaction object. */
  if (!ffsd->free_txn)
    ffsd->free_txn = txn;
  else
    svn_pool_destroy(txn->pool);
}


/* Obtain a lock on the transaction list of filesystem FS, call BODY
   with FS, BATON, and POOL, and then unlock the transaction list.
   Return what BODY returned. */
static svn_error_t *
with_txnlist_lock(svn_fs_t *fs,
                  svn_error_t *(*body)(svn_fs_t *fs,
                                       void *baton,
                                       apr_pool_t *pool),
                  void *baton,
                  apr_pool_t *pool)
{
  svn_error_t *err;
#if APR_HAS_THREADS
  fs_fs_data_t *ffd = fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;
  apr_status_t apr_err;

  apr_err = apr_thread_mutex_lock(ffsd->txn_list_lock);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't grab FSFS txn list mutex"));
#endif

  err = body(fs, baton, pool);

#if APR_HAS_THREADS
  apr_err = apr_thread_mutex_unlock(ffsd->txn_list_lock);
  if (apr_err && !err)
    return svn_error_wrap_apr(apr_err, _("Can't ungrab FSFS txn list mutex"));
#endif

  return err;
}

/* A structure used by unlock_proto_rev() and unlock_proto_rev_body(),
   which see. */
struct unlock_proto_rev_baton
{
  const char *txn_id;
  void *lockcookie;
};

/* Callback used in the implementation of unlock_proto_rev(). */
static svn_error_t *
unlock_proto_rev_body(svn_fs_t *fs, void *baton, apr_pool_t *pool)
{
  struct unlock_proto_rev_baton *b = baton;
  const char *txn_id = b->txn_id;
  apr_file_t *lockfile = b->lockcookie;
  fs_fs_shared_txn_data_t *txn = get_shared_txn(fs, txn_id, FALSE);
  apr_status_t apr_err;

  if (!txn)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Can't unlock unknown transaction '%s'"),
                             txn_id);
  if (!txn->being_written)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Can't unlock nonlocked transaction '%s'"),
                             txn_id);

  apr_err = apr_file_unlock(lockfile);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err,
       _("Can't unlock prototype revision lockfile for transaction '%s'"),
       txn_id);
  apr_err = apr_file_close(lockfile);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err,
       _("Can't close prototype revision lockfile for transaction '%s'"),
       txn_id);

  txn->being_written = FALSE;

  return SVN_NO_ERROR;
}

/* Unlock the prototype revision file for transaction TXN_ID in filesystem
   FS using cookie LOCKCOOKIE.  The original prototype revision file must
   have been closed _before_ calling this function.

   Perform temporary allocations in POOL. */
static svn_error_t *
unlock_proto_rev(svn_fs_t *fs, const char *txn_id, void *lockcookie,
                 apr_pool_t *pool)
{
  struct unlock_proto_rev_baton b;

  b.txn_id = txn_id;
  b.lockcookie = lockcookie;
  return with_txnlist_lock(fs, unlock_proto_rev_body, &b, pool);
}

/* Same as unlock_proto_rev(), but requires that the transaction list
   lock is already held. */
static svn_error_t *
unlock_proto_rev_list_locked(svn_fs_t *fs, const char *txn_id,
                             void *lockcookie,
                             apr_pool_t *pool)
{
  struct unlock_proto_rev_baton b;

  b.txn_id = txn_id;
  b.lockcookie = lockcookie;
  return unlock_proto_rev_body(fs, &b, pool);
}

/* A structure used by get_writable_proto_rev() and
   get_writable_proto_rev_body(), which see. */
struct get_writable_proto_rev_baton
{
  apr_file_t **file;
  void **lockcookie;
  const char *txn_id;
};

/* Callback used in the implementation of get_writable_proto_rev(). */
static svn_error_t *
get_writable_proto_rev_body(svn_fs_t *fs, void *baton, apr_pool_t *pool)
{
  struct get_writable_proto_rev_baton *b = baton;
  apr_file_t **file = b->file;
  void **lockcookie = b->lockcookie;
  const char *txn_id = b->txn_id;
  svn_error_t *err;
  fs_fs_shared_txn_data_t *txn = get_shared_txn(fs, txn_id, TRUE);

  /* First, ensure that no thread in this process (including this one)
     is currently writing to this transaction's proto-rev file. */
  if (txn->being_written)
    return svn_error_createf(SVN_ERR_FS_TRANSACTION_NOT_MUTABLE, NULL,
                             _("Cannot write to the prototype revision file "
                               "of transaction '%s' because a previous "
                               "representation is currently being written by "
                               "this process"),
                             txn_id);


  /* We know that no thread in this process is writing to the proto-rev
     file, and by extension, that no thread in this process is holding a
     lock on the prototype revision lock file.  It is therefore safe
     for us to attempt to lock this file, to see if any other process
     is holding a lock. */

  {
    apr_file_t *lockfile;
    apr_status_t apr_err;
    const char *lockfile_path = path_txn_proto_rev_lock(fs, txn_id, pool);

    /* Open the proto-rev lockfile, creating it if necessary, as it may
       not exist if the transaction dates from before the lockfiles were
       introduced.

       ### We'd also like to use something like svn_io_file_lock2(), but
           that forces us to create a subpool just to be able to unlock
           the file, which seems a waste. */
    SVN_ERR(svn_io_file_open(&lockfile, lockfile_path,
                             APR_WRITE | APR_CREATE, APR_OS_DEFAULT, pool));

    apr_err = apr_file_lock(lockfile,
                            APR_FLOCK_EXCLUSIVE | APR_FLOCK_NONBLOCK);
    if (apr_err)
      {
        svn_error_clear(svn_io_file_close(lockfile, pool));

        if (APR_STATUS_IS_EAGAIN(apr_err))
          return svn_error_createf(SVN_ERR_FS_TRANSACTION_NOT_MUTABLE, NULL,
                                   _("Cannot write to the prototype revision "
                                     "file of transaction '%s' because a "
                                     "previous representation is currently "
                                     "being written by another process"),
                                   txn_id);

        return svn_error_wrap_apr(apr_err,
                                  _("Can't get exclusive lock on file '%s'"),
                                  svn_path_local_style(lockfile_path, pool));
      }

    *lockcookie = lockfile;
  }

  /* We've successfully locked the transaction; mark it as such. */
  txn->being_written = TRUE;


  /* Now open the prototype revision file and seek to the end. */
  err = svn_io_file_open(file, path_txn_proto_rev(fs, txn_id, pool),
                         APR_WRITE | APR_BUFFERED, APR_OS_DEFAULT, pool);

  /* You might expect that we could dispense with the following seek
     and achieve the same thing by opening the file using APR_APPEND.
     Unfortunately, APR's buffered file implementation unconditionally
     places its initial file pointer at the start of the file (even for
     files opened with APR_APPEND), so we need this seek to reconcile
     the APR file pointer to the OS file pointer (since we need to be
     able to read the current file position later). */
  if (!err)
    {
      apr_off_t offset = 0;
      err = svn_io_file_seek(*file, APR_END, &offset, 0);
    }

  if (err)
    {
      svn_error_clear(unlock_proto_rev_list_locked(fs, txn_id, *lockcookie,
                                                   pool));
      *lockcookie = NULL;
    }

  return err;
}

/* Get a handle to the prototype revision file for transaction TXN_ID in
   filesystem FS, and lock it for writing.  Return FILE, a file handle
   positioned at the end of the file, and LOCKCOOKIE, a cookie that
   should be passed to unlock_proto_rev() to unlock the file once FILE
   has been closed.

   If the prototype revision file is already locked, return error
   SVN_ERR_FS_TRANSACTION_NOT_MUTABLE.

   Perform all allocations in POOL. */
static svn_error_t *
get_writable_proto_rev(apr_file_t **file,
                       void **lockcookie,
                       svn_fs_t *fs, const char *txn_id,
                       apr_pool_t *pool)
{
  struct get_writable_proto_rev_baton b;

  b.file = file;
  b.lockcookie = lockcookie;
  b.txn_id = txn_id;

  return with_txnlist_lock(fs, get_writable_proto_rev_body, &b, pool);
}

/* Callback used in the implementation of purge_shared_txn(). */
static svn_error_t *
purge_shared_txn_body(svn_fs_t *fs, void *baton, apr_pool_t *pool)
{
  const char *txn_id = *(const char **)baton;

  free_shared_txn(fs, txn_id);
  return SVN_NO_ERROR;
}

/* Purge the shared data for transaction TXN_ID in filesystem FS.
   Perform all allocations in POOL. */
static svn_error_t *
purge_shared_txn(svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return with_txnlist_lock(fs, purge_shared_txn_body, &txn_id, pool);
}



/* Fetch the current offset of FILE into *OFFSET_P. */
static svn_error_t *
get_file_offset(apr_off_t *offset_p, apr_file_t *file, apr_pool_t *pool)
{
  apr_off_t offset;

  /* Note that, for buffered files, one (possibly surprising) side-effect
     of this call is to flush any unwritten data to disk. */
  offset = 0;
  SVN_ERR(svn_io_file_seek(file, APR_CUR, &offset, pool));
  *offset_p = offset;

  return SVN_NO_ERROR;
}


/* Read the format version from FILE and return it in *PFORMAT.
   Use POOL for temporary allocation. */
static svn_error_t *
read_format(int *pformat, const char *file, apr_pool_t *pool)
{
  svn_error_t *err = svn_io_read_version_file(pformat, file, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      /* Treat an absent format file as format 1.  Do not try to
         create the format file on the fly, because the repository
         might be read-only for us, or this might be a read-only
         operation, and the spirit of FSFS is to make no changes
         whatseover in read-only operations.  See thread starting at
         http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=97600
         for more. */
      svn_error_clear(err);
      err = SVN_NO_ERROR;
      *pformat = 1;
    }
  return err;
}

/* Return the error SVN_ERR_FS_UNSUPPORTED_FORMAT if FS's format
   number is not the same as the format number supported by this
   Subversion. */
static svn_error_t *
check_format(int format)
{
  /* We support format 1 and 2 simultaneously */
  if (format == 1 && SVN_FS_FS__FORMAT_NUMBER == 2)
    return SVN_NO_ERROR;

  if (format != SVN_FS_FS__FORMAT_NUMBER)
    {
      return svn_error_createf 
        (SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
         _("Expected FS format '%d'; found format '%d'"), 
         SVN_FS_FS__FORMAT_NUMBER, format);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__open(svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_file_t *current_file, *uuid_file;
  int format;
  char buf[APR_UUID_FORMATTED_LENGTH + 2];
  apr_size_t limit;

  fs->path = apr_pstrdup(fs->pool, path);

  /* Attempt to open the 'current' file of this repository.  There
     isn't much need for specific state associated with an open fs_fs
     repository. */
  SVN_ERR(svn_io_file_open(&current_file, path_current(fs, pool),
                           APR_READ, APR_OS_DEFAULT, pool));
  SVN_ERR(svn_io_file_close(current_file, pool));

  /* Read the FS format number. */
  SVN_ERR(read_format(&format, path_format(fs, pool), pool));

  /* Now we've got a format number no matter what. */
  ffd->format = format;
  SVN_ERR(check_format(format));

  /* Read in and cache the repository uuid. */
  SVN_ERR(svn_io_file_open(&uuid_file, path_uuid(fs, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  limit = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(uuid_file, buf, &limit, pool));
  ffd->uuid = apr_pstrdup(fs->pool, buf);
  
  SVN_ERR(svn_io_file_close(uuid_file, pool));

  return SVN_NO_ERROR;
}

/* Find the youngest revision in a repository at path FS_PATH and
   return it in *YOUNGEST_P.  Perform temporary allocations in
   POOL. */
static svn_error_t *
get_youngest(svn_revnum_t *youngest_p,
             const char *fs_path,
             apr_pool_t *pool)
{
  apr_file_t *current_file;
  char buf[81];
  apr_size_t len;

  SVN_ERR(svn_io_file_open(&current_file,
                           svn_path_join(fs_path, PATH_CURRENT, pool),
                           APR_READ, APR_OS_DEFAULT, pool));

  len = sizeof(buf) - 1;
  SVN_ERR(svn_io_file_read(current_file, buf, &len, pool));
  buf[len] = '\0';
  
  *youngest_p = SVN_STR_TO_REV(buf);
  
  SVN_ERR(svn_io_file_close(current_file, pool));
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__hotcopy(const char *src_path,
                   const char *dst_path,
                   apr_pool_t *pool)
{
  const char *src_subdir, *dst_subdir;
  svn_revnum_t youngest, rev;
  apr_pool_t *iterpool;
  svn_node_kind_t kind;
  int format;

  /* Check format to be sure we know how to hotcopy this FS. */
  SVN_ERR(read_format(&format,
                      svn_path_join(src_path, PATH_FORMAT, pool),
                      pool));
  SVN_ERR(check_format(format));

  /* Copy the current file. */
  SVN_ERR(svn_io_dir_file_copy(src_path, dst_path, PATH_CURRENT, pool));

  /* Copy the uuid. */
  SVN_ERR(svn_io_dir_file_copy(src_path, dst_path, PATH_UUID, pool));

  /* Find the youngest revision from this current file. */
  SVN_ERR(get_youngest(&youngest, dst_path, pool));

  /* Copy the necessary rev files. */
  src_subdir = svn_path_join(src_path, PATH_REVS_DIR, pool);
  dst_subdir = svn_path_join(dst_path, PATH_REVS_DIR, pool);

  SVN_ERR(svn_io_make_dir_recursively(dst_subdir, pool));

  iterpool = svn_pool_create(pool);
  for (rev = 0; rev <= youngest; rev++)
    {
      SVN_ERR(svn_io_dir_file_copy(src_subdir, dst_subdir,
                                   apr_psprintf(iterpool, "%ld", rev),
                                   iterpool));
      svn_pool_clear(iterpool);
    }

  /* Copy the necessary revprop files. */
  src_subdir = svn_path_join(src_path, PATH_REVPROPS_DIR, pool);
  dst_subdir = svn_path_join(dst_path, PATH_REVPROPS_DIR, pool);

  SVN_ERR(svn_io_make_dir_recursively(dst_subdir, pool));

  for (rev = 0; rev <= youngest; rev++)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(svn_io_dir_file_copy(src_subdir, dst_subdir,
                                   apr_psprintf(iterpool, "%ld", rev),
                                   iterpool));
    }

  svn_pool_destroy(iterpool);

  /* Make an empty transactions directory for now.  Eventually some
     method of copying in progress transactions will need to be
     developed.*/
  dst_subdir = svn_path_join(dst_path, PATH_TXNS_DIR, pool);
  SVN_ERR(svn_io_make_dir_recursively(dst_subdir, pool));

  /* Now copy the locks tree. */
  src_subdir = svn_path_join(src_path, PATH_LOCKS_DIR, pool);
  SVN_ERR(svn_io_check_path(src_subdir, &kind, pool));
  if (kind == svn_node_dir)
    SVN_ERR(svn_io_copy_dir_recursively(src_subdir, dst_path,
                                        PATH_LOCKS_DIR, TRUE, NULL,
                                        NULL, pool));

  /* Hotcopied FS is complete. Stamp it with a format file. */
  SVN_ERR(svn_io_write_version_file 
          (svn_path_join(dst_path, PATH_FORMAT, pool), format, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__youngest_rev(svn_revnum_t *youngest_p,
                        svn_fs_t *fs,
                        apr_pool_t *pool)
{
  SVN_ERR(get_youngest(youngest_p, fs->path, pool));
  
  return SVN_NO_ERROR;
}

/* Given a revision file FILE that has been pre-positioned at the
   beginning of a Node-Rev header block, read in that header block and
   store it in the apr_hash_t HEADERS.  All allocations will be from
   POOL. */
static svn_error_t * read_header_block(apr_hash_t **headers,
                                       apr_file_t *file,
                                       apr_pool_t *pool)
{
  *headers = apr_hash_make(pool);
  
  while (1)
    {
      char header_str[1024];
      const char *name, *value;
      apr_size_t i = 0, header_len;
      apr_size_t limit;
      char *local_name, *local_value;

      limit = sizeof(header_str);
      SVN_ERR(svn_io_read_length_line(file, header_str, &limit, pool));

      if (strlen(header_str) == 0)
        break; /* end of header block */
      
      header_len = strlen(header_str);

      while (header_str[i] != ':')
        {
          if (header_str[i] == '\0')
            return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                    _("Found malformed header in "
                                      "revision file"));
          i++;
        }
      
      /* Create a 'name' string and point to it. */
      header_str[i] = '\0';
      name=header_str;

      /* Skip over the NULL byte and the space following it. */
      i += 2;

      if (i > header_len)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Found malformed header in "
                                  "revision file"));

      value = header_str + i;

      local_name = apr_pstrdup(pool, name);
      local_value = apr_pstrdup(pool, value);

      apr_hash_set(*headers, local_name, APR_HASH_KEY_STRING, local_value);
    }

  return SVN_NO_ERROR;
}

/* Open the revision file for revision REV in filesystem FS and store
   the newly opened file in FILE.  Seek to location OFFSET before
   returning.  Perform temporary allocations in POOL. */
static svn_error_t *
open_and_seek_revision(apr_file_t **file,
                       svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_off_t offset,
                       apr_pool_t *pool)
{
  apr_file_t *rev_file;

  SVN_ERR(svn_io_file_open(&rev_file, svn_fs_fs__path_rev(fs, rev, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  SVN_ERR(svn_io_file_seek(rev_file, APR_SET, &offset, pool));

  *file = rev_file;

  return SVN_NO_ERROR;
}

/* Open the representation for a node-revision in transaction TXN_ID
   in filesystem FS and store the newly opened file in FILE.  Seek to
   location OFFSET before returning.  Perform temporary allocations in
   POOL.  Only appropriate for file contents, nor props or directory
   contents. */
static svn_error_t *
open_and_seek_transaction(apr_file_t **file,
                          svn_fs_t *fs,
                          const char *txn_id,
                          representation_t *rep,
                          apr_pool_t *pool)
{
  apr_file_t *rev_file;
  apr_off_t offset;

  SVN_ERR(svn_io_file_open(&rev_file, path_txn_proto_rev(fs, txn_id, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  offset = rep->offset;
  SVN_ERR(svn_io_file_seek(rev_file, APR_SET, &offset, pool));

  *file = rev_file;

  return SVN_NO_ERROR;
}

/* Given a node-id ID, and a representation REP in filesystem FS, open
   the correct file and seek to the correction location.  Store this
   file in *FILE_P.  Perform any allocations in POOL. */
static svn_error_t *
open_and_seek_representation(apr_file_t **file_p,
                             svn_fs_t *fs,
                             representation_t *rep,
                             apr_pool_t *pool)
{
  if (! rep->txn_id)
    return open_and_seek_revision(file_p, fs, rep->revision, rep->offset,
                                  pool);
  else
    return open_and_seek_transaction(file_p, fs, rep->txn_id, rep, pool);
}

/* Parse the description of a representation from STRING and store it
   into *REP_P.  If the representation is mutable (the revision is
   given as -1), then use TXN_ID for the representation's txn_id
   field.  If MUTABLE_REP_TRUNCATED is true, then this representation
   is for property or directory contents, and no information will be
   expected except the "-1" revision number for a mutable
   representation.  Allocate *REP_P in POOL. */
static svn_error_t *
read_rep_offsets(representation_t **rep_p,
                 char *string,
                 const char *txn_id,
                 svn_boolean_t mutable_rep_truncated,
                 apr_pool_t *pool)
{
  representation_t *rep;
  char *str, *last_str;
  int i;

  rep = apr_pcalloc(pool, sizeof(*rep));
  *rep_p = rep;

  str = apr_strtok(string, " ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text rep offset line in node-rev"));


  rep->revision = SVN_STR_TO_REV(str);
  if (rep->revision == SVN_INVALID_REVNUM)
    {
      rep->txn_id = txn_id;
      if (mutable_rep_truncated)
        return SVN_NO_ERROR;
    }
  
  str = apr_strtok(NULL, " ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text rep offset line in node-rev"));
  
  rep->offset = apr_atoi64(str);
  
  str = apr_strtok(NULL, " ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text rep offset line in node-rev"));
  
  rep->size = apr_atoi64(str);
  
  str = apr_strtok(NULL, " ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text rep offset line in node-rev"));
  
  rep->expanded_size = apr_atoi64(str);

  /* Read in the MD5 hash. */
  str = apr_strtok(NULL, " ", &last_str);
  if ((str == NULL) || (strlen(str) != (APR_MD5_DIGESTSIZE * 2)))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text rep offset line in node-rev"));

  /* Parse the hex MD5 hash into digest form. */
  for (i = 0; i < APR_MD5_DIGESTSIZE; i++)
    {
      if ((! isxdigit(str[i * 2])) || (! isxdigit(str[i * 2 + 1])))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Malformed text rep offset line in node-rev"));

      str[i * 2] = tolower(str[i * 2]);
      rep->checksum[i] = (str[i * 2] -
                          ((str[i * 2] <= '9') ? '0' : ('a' - 10))) << 4;

      str[i * 2 + 1] = tolower(str[i * 2 + 1]);
      rep->checksum[i] |= (str[i * 2 + 1] -
                           ((str[i * 2 + 1] <= '9') ? '0' : ('a' - 10)));
    }
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_node_revision(node_revision_t **noderev_p,
                             svn_fs_t *fs,
                             const svn_fs_id_t *id,
                             apr_pool_t *pool)
{
  apr_file_t *revision_file;
  apr_hash_t *headers;
  node_revision_t *noderev;
  char *value;
  svn_error_t *err;
  
  if (svn_fs_fs__id_txn_id(id))
    {
      /* This is a transaction node-rev. */
      err = svn_io_file_open(&revision_file, path_txn_node_rev(fs, id, pool),
                             APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool);
    }
  else
    {
      /* This is a revision node-rev. */
      err = open_and_seek_revision(&revision_file, fs,
                                   svn_fs_fs__id_rev(id),
                                   svn_fs_fs__id_offset(id),
                                   pool);
    }

  if (err)
    {
      if (APR_STATUS_IS_ENOENT(err->apr_err))
        {
          svn_error_clear(err);
          return svn_fs_fs__err_dangling_id(fs, id);
        }
      
      return err;
    }
  
  SVN_ERR(read_header_block(&headers, revision_file, pool) );

  noderev = apr_pcalloc(pool, sizeof(*noderev));

  /* Read the node-rev id. */
  value = apr_hash_get(headers, HEADER_ID, APR_HASH_KEY_STRING);

  SVN_ERR(svn_io_file_close(revision_file, pool));

  noderev->id = svn_fs_fs__id_parse(value, strlen(value), pool);

  /* Read the type. */
  value = apr_hash_get(headers, HEADER_TYPE, APR_HASH_KEY_STRING);

  if ((value == NULL) ||
      (strcmp(value, KIND_FILE) != 0 && strcmp(value, KIND_DIR)))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Missing kind field in node-rev"));

  noderev->kind = (strcmp(value, KIND_FILE) == 0) ? svn_node_file
    : svn_node_dir;

  /* Read the 'count' field. */
  value = apr_hash_get(headers, HEADER_COUNT, APR_HASH_KEY_STRING);
  noderev->predecessor_count = (value == NULL) ? 0 : atoi(value);

  /* Get the properties location. */
  value = apr_hash_get(headers, HEADER_PROPS, APR_HASH_KEY_STRING);
  if (value)
    {
      SVN_ERR(read_rep_offsets(&noderev->prop_rep, value,
                               svn_fs_fs__id_txn_id(id), TRUE, pool));
    }

  /* Get the data location. */
  value = apr_hash_get(headers, HEADER_TEXT, APR_HASH_KEY_STRING);
  if (value)
    {
      SVN_ERR(read_rep_offsets(&noderev->data_rep, value,
                               svn_fs_fs__id_txn_id(id),
                               (noderev->kind == svn_node_dir), pool));
    }

  /* Get the created path. */
  value = apr_hash_get(headers, HEADER_CPATH, APR_HASH_KEY_STRING);
  if (value == NULL)
    {
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Missing cpath in node-rev"));
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
      char *str, *last_str;

      str = apr_strtok(value, " ", &last_str);
      if (str == NULL)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Malformed copyroot line in node-rev"));

      noderev->copyroot_rev = atoi(str);
      
      if (last_str == NULL)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Malformed copyroot line in node-rev"));
      noderev->copyroot_path = apr_pstrdup(pool, last_str);
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
      char *str, *last_str;

      str = apr_strtok(value, " ", &last_str);
      if (str == NULL)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Malformed copyfrom line in node-rev"));

      noderev->copyfrom_rev = atoi(str);
      
      if (last_str == NULL)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Malformed copyfrom line in node-rev"));
      noderev->copyfrom_path = apr_pstrdup(pool, last_str);
    }

  *noderev_p = noderev;
  
  return SVN_NO_ERROR;
}

/* Return a formatted string that represents the location of
   representation REP.  If MUTABLE_REP_TRUNCATED is given, the rep is
   for props or dir contents, and only a "-1" revision number will be
   given for a mutable rep.  Perform the allocation from POOL.  */
static const char *
representation_string(representation_t *rep,
                      svn_boolean_t mutable_rep_truncated, apr_pool_t *pool)
{
  if (rep->txn_id && mutable_rep_truncated)
    return "-1";
  else
    return apr_psprintf(pool, "%ld %" APR_OFF_T_FMT " %" SVN_FILESIZE_T_FMT
                        " %" SVN_FILESIZE_T_FMT " %s",
                        rep->revision, rep->offset, rep->size,
                        rep->expanded_size,
                        svn_md5_digest_to_cstring_display(rep->checksum,
                                                          pool));
}

/* Write the node-revision NODEREV into the file FILE.  Temporary
   allocations are from POOL. */
static svn_error_t *
write_noderev_txn(apr_file_t *file,
                  node_revision_t *noderev,
                  apr_pool_t *pool)
{
  svn_stream_t *outfile;

  outfile = svn_stream_from_aprfile(file, pool);

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
                                                    (noderev->kind
                                                     == svn_node_dir),
                                                    pool)));

  if (noderev->prop_rep)
    SVN_ERR(svn_stream_printf(outfile, pool, HEADER_PROPS ": %s\n",
                              representation_string(noderev->prop_rep, TRUE,
                                                    pool)));

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

  SVN_ERR(svn_stream_printf(outfile, pool, "\n"));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__put_node_revision(svn_fs_t *fs,
                             const svn_fs_id_t *id,
                             node_revision_t *noderev,
                             apr_pool_t *pool)
{
  apr_file_t *noderev_file;
  const char *txn_id = svn_fs_fs__id_txn_id(id);

  if (! txn_id)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Attempted to write to non-transaction"));

  SVN_ERR(svn_io_file_open(&noderev_file, path_txn_node_rev(fs, id, pool),
                           APR_WRITE | APR_CREATE | APR_TRUNCATE
                           | APR_BUFFERED, APR_OS_DEFAULT, pool));

  SVN_ERR(write_noderev_txn(noderev_file, noderev, pool));

  SVN_ERR(svn_io_file_close(noderev_file, pool));

  return SVN_NO_ERROR;
}


/* This structure is used to hold the information associated with a
   REP line. */
struct rep_args
{
  svn_boolean_t is_delta;
  svn_boolean_t is_delta_vs_empty;
  
  svn_revnum_t base_revision;
  apr_off_t base_offset;
  apr_size_t base_length;
};

/* Read the next line from file FILE and parse it as a text
   representation entry.  Return the parsed entry in *REP_ARGS_P.
   Perform all allocations in POOL. */
static svn_error_t *
read_rep_line(struct rep_args **rep_args_p,
              apr_file_t *file,
              apr_pool_t *pool)
{
  char buffer[160];
  apr_size_t limit;
  struct rep_args *rep_args;
  char *str, *last_str;
  
  limit = sizeof(buffer);
  SVN_ERR(svn_io_read_length_line(file, buffer, &limit, pool));

  rep_args = apr_pcalloc(pool, sizeof(*rep_args));
  rep_args->is_delta = FALSE;

  if (strcmp(buffer, REP_PLAIN) == 0)
    {
      *rep_args_p = rep_args;
      return SVN_NO_ERROR;
    }

  if (strcmp(buffer, REP_DELTA) == 0)
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
  str = apr_strtok(buffer, " ", &last_str);
  if (! str || (strcmp(str, REP_DELTA) != 0)) goto err;

  str = apr_strtok(NULL, " ", &last_str);
  if (! str) goto err;
  rep_args->base_revision = atol(str);

  str = apr_strtok(NULL, " ", &last_str);
  if (! str) goto err;
  rep_args->base_offset = (apr_off_t) apr_atoi64(str);

  str = apr_strtok(NULL, " ", &last_str);
  if (! str) goto err;
  rep_args->base_length = (apr_size_t) apr_atoi64(str);

  *rep_args_p = rep_args;
  return SVN_NO_ERROR;

 err:
  return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                          _("Malformed representation header"));
}

/* Given a revision file REV_FILE, find the Node-ID of the header
   located at OFFSET and store it in *ID_P.  Allocate temporary
   variables from POOL. */
static svn_error_t *
get_fs_id_at_offset(svn_fs_id_t **id_p,
                    apr_file_t *rev_file,
                    apr_off_t offset,
                    apr_pool_t *pool)
{
  svn_fs_id_t *id;
  apr_hash_t *headers;
  const char *node_id_str;
  
  SVN_ERR(svn_io_file_seek(rev_file, APR_SET, &offset, pool));

  SVN_ERR(read_header_block(&headers, rev_file, pool));

  node_id_str = apr_hash_get(headers, HEADER_ID, APR_HASH_KEY_STRING);

  if (node_id_str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Missing node-id in node-rev"));

  id = svn_fs_fs__id_parse(node_id_str, strlen(node_id_str), pool);

  if (id == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Corrupt node-id in node-rev"));

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
get_root_changes_offset(apr_off_t *root_offset,
                        apr_off_t *changes_offset,
                        apr_file_t *rev_file,
                        apr_pool_t *pool)
{
  apr_off_t offset;
  char buf[64];
  int i, num_bytes;
  apr_size_t len;
  
  /* We will assume that the last line containing the two offsets
     will never be longer than 64 characters. */
  offset = 0;
  SVN_ERR(svn_io_file_seek(rev_file, APR_END, &offset, pool));

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
                               _("Revision file lacks trailing newline"));
    }

  /* Look for the next previous newline. */
  for (i = num_bytes - 2; i >= 0; i--)
    {
      if (buf[i] == '\n') break;
    }

  if (i < 0)
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Final line in revision file longer than 64 "
                                 "characters"));
    }

  i++;

  if (root_offset)
    *root_offset = apr_atoi64(&buf[i]);

  /* find the next space */
  for ( ; i < (num_bytes - 2) ; i++)
    if (buf[i] == ' ') break;

  if (i == (num_bytes - 2))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Final line in revision file missing space"));

  i++;

  /* note that apr_atoi64() will stop reading as soon as it encounters
     the final newline. */
  if (changes_offset)
    *changes_offset = apr_atoi64(&buf[i]);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__rev_get_root(svn_fs_id_t **root_id_p,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *pool)
{
  apr_file_t *revision_file;
  apr_off_t root_offset;
  svn_fs_id_t *root_id;
  svn_error_t *err;

  err = svn_io_file_open(&revision_file, svn_fs_fs__path_rev(fs, rev, pool),
                         APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                               _("No such revision %ld"), rev);
    }
  else if (err)
    return err;
      

  SVN_ERR(get_root_changes_offset(&root_offset, NULL, revision_file, pool));

  SVN_ERR(get_fs_id_at_offset(&root_id, revision_file, root_offset, pool));

  SVN_ERR(svn_io_file_close(revision_file, pool));

  *root_id_p = root_id;
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_revision_proplist(svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_hash_t *proplist,
                                 apr_pool_t *pool)
{
  const char *final_path = path_revprops(fs, rev, pool);
  const char *tmp_path;
  apr_file_t *f;

  SVN_ERR(svn_io_open_unique_file2
          (&f, &tmp_path, final_path, ".tmp", svn_io_file_del_none, pool));
  SVN_ERR(svn_hash_write(proplist, f, pool));
  SVN_ERR(svn_io_file_close(f, pool));
  /* We use the rev file of this revision as the perms reference,
     because when setting revprops for the first time, the revprop
     file won't exist and therefore can't serve as its own reference.
     (Whereas the rev file should already exist at this point.) */
  SVN_ERR(svn_fs_fs__move_into_place(tmp_path, final_path,
                                     svn_fs_fs__path_rev(fs, rev, pool),
                                     pool));

  return SVN_NO_ERROR;
}  

svn_error_t *
svn_fs_fs__revision_proplist(apr_hash_t **proplist_p,
                             svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_pool_t *pool)
{
  apr_file_t *revprop_file;
  apr_hash_t *proplist;
  svn_error_t *err;

  err = svn_io_file_open(&revprop_file, path_revprops(fs, rev, pool),
                         APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                               _("No such revision %ld"), rev);
    }
  else if (err)
    return err;

  proplist = apr_hash_make(pool);

  SVN_ERR(svn_hash_read(proplist, revprop_file, pool));

  SVN_ERR(svn_io_file_close(revprop_file, pool));

  *proplist_p = proplist;

  return SVN_NO_ERROR;
}

/* Represents where in the current svndiff data block each
   representation is. */
struct rep_state
{
  apr_file_t *file;
  apr_off_t start;  /* The starting offset for the raw
                       svndiff/plaintext data minus header. */
  apr_off_t off;    /* The current offset into the file. */
  apr_off_t end;    /* The end offset of the raw data. */
  int ver;          /* If a delta, what svndiff version? */
  int chunk_index;
};

/* Read the rep args for REP in filesystem FS and create a rep_state
   for reading the representation.  Return the rep_state in *REP_STATE
   and the rep args in *REP_ARGS, both allocated in POOL. */
static svn_error_t *
create_rep_state(struct rep_state **rep_state,
                 struct rep_args **rep_args,
                 representation_t *rep,
                 svn_fs_t *fs,
                 apr_pool_t *pool)
{
  struct rep_state *rs = apr_pcalloc(pool, sizeof(*rs));
  struct rep_args *ra;
  unsigned char buf[4];

  SVN_ERR(open_and_seek_representation(&rs->file, fs, rep, pool));
  SVN_ERR(read_rep_line(&ra, rs->file, pool));
  SVN_ERR(get_file_offset(&rs->start, rs->file, pool));
  rs->off = rs->start;
  rs->end = rs->start + rep->size;
  *rep_state = rs;
  *rep_args = ra;

  if (ra->is_delta == FALSE)
    /* This is a plaintext, so just return the current rep_state. */
    return SVN_NO_ERROR;

  /* We are dealing with a delta, find out what version. */
  SVN_ERR(svn_io_file_read_full(rs->file, buf, sizeof(buf), NULL, pool));
  if (! ((buf[0] == 'S') && (buf[1] == 'V') && (buf[2] == 'N')))
    return svn_error_create
      (SVN_ERR_FS_CORRUPT, NULL,
       _("Malformed svndiff data in representation"));
  rs->ver = buf[3];
  rs->chunk_index = 0;
  rs->off += 4;

  return SVN_NO_ERROR;
}

/* Build an array of rep_state structures in *LIST giving the delta
   reps from first_rep to a plain-text or self-compressed rep.  Set
   *SRC_STATE to the plain-text rep we find at the end of the chain,
   or to NULL if the final delta representation is self-compressed.
   The representation to start from is designated by filesystem FS, id
   ID, and representation REP. */
static svn_error_t *
build_rep_list(apr_array_header_t **list,
               struct rep_state **src_state,
               svn_fs_t *fs,
               representation_t *first_rep,
               apr_pool_t *pool)
{
  representation_t rep;
  struct rep_state *rs;
  struct rep_args *rep_args;

  *list = apr_array_make(pool, 1, sizeof(struct rep_state *));
  rep = *first_rep;

  while (1)
    {
      SVN_ERR(create_rep_state(&rs, &rep_args, &rep, fs, pool));
      if (rep_args->is_delta == FALSE)
        {
          /* This is a plaintext, so just return the current rep_state. */
          *src_state = rs;
          return SVN_NO_ERROR;
        }

      /* Push this rep onto the list.  If it's self-compressed, we're done. */
      APR_ARRAY_PUSH(*list, struct rep_state *) = rs;
      if (rep_args->is_delta_vs_empty)
        {
          *src_state = NULL;
          return SVN_NO_ERROR;
        }

      rep.revision = rep_args->base_revision;
      rep.offset = rep_args->base_offset;
      rep.size = rep_args->base_length;
      rep.txn_id = NULL;
    }
}


struct rep_read_baton
{
  /* The FS from which we're reading. */
  svn_fs_t *fs;

  /* The state of all prior delta representations. */
  apr_array_header_t *rs_list;

  /* The plaintext state, if there is a plaintext. */
  struct rep_state *src_state;

  /* The index of the current delta chunk, if we are reading a delta. */
  int chunk_index;

  /* The buffer where we store undeltified data. */
  char *buf;
  apr_size_t buf_pos;
  apr_size_t buf_len;
  
  /* An MD5 context for summing the data read in order to verify it. */
  struct apr_md5_ctx_t md5_context;
  svn_boolean_t checksum_finalized;

  /* The stored checksum of the representation we are reading, its
     length, and the amount we've read so far.  Some of this
     information is redundant with rs_list and src_state, but it's
     convenient for the checksumming code to have it here. */
  unsigned char checksum[APR_MD5_DIGESTSIZE];
  svn_filesize_t len;
  svn_filesize_t off;

  /* Used for temporary allocations during the read. */
  apr_pool_t *pool;

  /* Pool used to store file handles and other data that is persistant
     for the entire stream read. */
  apr_pool_t *filehandle_pool;
};

/* Create a rep_read_baton structure for node revision NODEREV in
   filesystem FS and store it in *RB_P.  Perform all allocations in
   POOL.  If rep is mutable, it must be for file contents. */
static svn_error_t *
rep_read_get_baton(struct rep_read_baton **rb_p,
                   svn_fs_t *fs,
                   representation_t *rep,
                   apr_pool_t *pool)
{
  struct rep_read_baton *b;

  b = apr_pcalloc(pool, sizeof(*b));
  b->fs = fs;
  b->chunk_index = 0;
  b->buf = NULL;
  apr_md5_init(&(b->md5_context));
  b->checksum_finalized = FALSE;
  memcpy(b->checksum, rep->checksum, sizeof(b->checksum));
  b->len = rep->expanded_size;
  b->off = 0;
  b->pool = svn_pool_create(pool);
  b->filehandle_pool = svn_pool_create(pool);
  
  SVN_ERR(build_rep_list(&b->rs_list, &b->src_state, fs, rep,
                         b->filehandle_pool));

  /* Save our output baton. */
  *rb_p = b;

  return SVN_NO_ERROR;
}

/* Skip forwards to THIS_CHUNK in REP_STATE and then read the next delta
   window into *NWIN. */
static svn_error_t *
read_window(svn_txdelta_window_t **nwin, int this_chunk, struct rep_state *rs,
            apr_pool_t *pool)
{
  svn_stream_t *stream;

  assert(rs->chunk_index <= this_chunk);

  /* Skip windows to reach the current chunk if we aren't there yet. */
  while (rs->chunk_index < this_chunk)
    {
      SVN_ERR(svn_txdelta_skip_svndiff_window(rs->file, rs->ver, pool));
      rs->chunk_index++;
      SVN_ERR(get_file_offset(&rs->off, rs->file, pool));
      if (rs->off >= rs->end)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Reading one svndiff window read "
                                  "beyond the end of the "
                                  "representation"));
    }

  /* Read the next window. */
  stream = svn_stream_from_aprfile(rs->file, pool);
  SVN_ERR(svn_txdelta_read_svndiff_window(nwin, stream, rs->ver, pool));
  rs->chunk_index++;
  SVN_ERR(get_file_offset(&rs->off, rs->file, pool));

  if (rs->off > rs->end)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Reading one svndiff window read beyond "
                              "the end of the representation"));
  
  return SVN_NO_ERROR;
}  

/* Get one delta window that is a result of combining all but the last deltas
   from the current desired representation identified in *RB, to its
   final base representation.  Store the window in *RESULT. */
static svn_error_t *
get_combined_window(svn_txdelta_window_t **result,
                    struct rep_read_baton *rb)
{
  apr_pool_t *pool, *new_pool;
  int i;
  svn_txdelta_window_t *window, *nwin;
  struct rep_state *rs;

  assert(rb->rs_list->nelts >= 2);

  pool = svn_pool_create(rb->pool);

  /* Read the next window from the original rep. */
  rs = APR_ARRAY_IDX(rb->rs_list, 0, struct rep_state *);
  SVN_ERR(read_window(&window, rb->chunk_index, rs, pool));

  /* Combine in the windows from the other delta reps, if needed. */
  for (i = 1; i < rb->rs_list->nelts - 1; i++)
    {
      if (window->src_ops == 0)
        break;

      rs = APR_ARRAY_IDX(rb->rs_list, i, struct rep_state *);

      SVN_ERR(read_window(&nwin, rb->chunk_index, rs, pool));

      /* Combine this window with the current one.  Cycles pools so that we
         only need to hold three windows at a time. */
      new_pool = svn_pool_create(rb->pool);
      window = svn_txdelta_compose_windows(nwin, window, new_pool);
      svn_pool_destroy(pool);
      pool = new_pool;
    }

  *result = window;
  return SVN_NO_ERROR;
}

static svn_error_t *
rep_read_contents_close(void *baton)
{
  struct rep_read_baton *rb = baton;
  
  svn_pool_destroy(rb->pool);
  svn_pool_destroy(rb->filehandle_pool);
  
  return SVN_NO_ERROR;
}

/* Return the next *LEN bytes of the rep and store them in *BUF. */
static svn_error_t *
get_contents(struct rep_read_baton *rb,
             char *buf,
             apr_size_t *len)
{
  apr_size_t copy_len, remaining = *len, tlen;
  char *sbuf, *tbuf, *cur = buf;
  struct rep_state *rs;
  svn_txdelta_window_t *cwindow, *lwindow;

  /* Special case for when there are no delta reps, only a plain
     text. */
  if (rb->rs_list->nelts == 0)
    {
      copy_len = remaining;
      rs = rb->src_state;
      if (((apr_off_t) copy_len) > rs->end - rs->off)
        copy_len = (apr_size_t) (rs->end - rs->off);
      SVN_ERR(svn_io_file_read_full(rs->file, cur, copy_len, NULL,
                                    rb->pool));
      rs->off += copy_len;
      *len = copy_len;
      return SVN_NO_ERROR;
    }

  while (remaining > 0)
    {
      /* If we have buffered data from a previous chunk, use that. */
      if (rb->buf)
        {
          /* Determine how much to copy from the buffer. */
          copy_len = rb->buf_len - rb->buf_pos;
          if (copy_len > remaining)
            copy_len = remaining;

          /* Actually copy the data. */
          memcpy(cur, rb->buf + rb->buf_pos, copy_len);
          rb->buf_pos += copy_len;
          cur += copy_len;
          remaining -= copy_len;

          /* If the buffer is all used up, clear it and empty the
             local pool. */
          if (rb->buf_pos == rb->buf_len)
            {
              svn_pool_clear(rb->pool);
              rb->buf = NULL;
            }
        }
      else
        {
          
          rs = APR_ARRAY_IDX(rb->rs_list, 0, struct rep_state *);
          if (rs->off == rs->end)
            break;
          
          /* Get more buffered data by evaluating a chunk. */
          if (rb->rs_list->nelts > 1)
            SVN_ERR(get_combined_window(&cwindow, rb));
          else
            cwindow = NULL;
          if (!cwindow || cwindow->src_ops > 0)
            {
              rs = APR_ARRAY_IDX(rb->rs_list, rb->rs_list->nelts - 1,
                                 struct rep_state *);
              /* Read window from last representation in list. */
              /* We apply this window directly instead of combining it with the
                 others.  We do this because vdelta is used for deltas against
                 the empty stream, which will trigger quadratic behaviour in
                 the delta combiner. */
              SVN_ERR(read_window(&lwindow, rb->chunk_index, rs, rb->pool));

              if (lwindow->src_ops > 0)
                {
                  if (! rb->src_state)
                    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                            _("svndiff data requested "
                                              "non-existent source"));
                  rs = rb->src_state;
                  sbuf = apr_palloc(rb->pool, lwindow->sview_len);
                  if (! ((rs->start + lwindow->sview_offset) < rs->end))
                    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                            _("svndiff requested position "
                                              "beyond end of stream"));
                  if ((rs->start + lwindow->sview_offset) != rs->off)
                    {
                      rs->off = rs->start + lwindow->sview_offset;
                      SVN_ERR(svn_io_file_seek(rs->file, APR_SET, &rs->off,
                                               rb->pool));
                    }
                  SVN_ERR(svn_io_file_read_full(rs->file, sbuf,
                                                lwindow->sview_len,
                                                NULL, rb->pool));
                  rs->off += lwindow->sview_len;
                }
              else
                sbuf = NULL;

              /* Apply lwindow to source. */
              tlen = lwindow->tview_len;
              tbuf = apr_palloc(rb->pool, tlen);
              svn_txdelta_apply_instructions(lwindow, sbuf, tbuf,
                                             &tlen);
              if (tlen != lwindow->tview_len)
                return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                        _("svndiff window length is "
                                          "corrupt"));
              sbuf = tbuf;
            }
          else
            sbuf = NULL;

          rb->chunk_index++;

          if (cwindow)
            {
              rb->buf_len = cwindow->tview_len;
              rb->buf = apr_palloc(rb->pool, rb->buf_len);
              svn_txdelta_apply_instructions(cwindow, sbuf, rb->buf,
                                             &rb->buf_len);
              if (rb->buf_len != cwindow->tview_len)
                return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                        _("svndiff window length is "
                                          "corrupt"));
            }
          else
            {
              rb->buf_len = lwindow->tview_len;
              rb->buf = sbuf;
            }
          
          rb->buf_pos = 0;
        }
    }

  *len = cur - buf;

  return SVN_NO_ERROR;
}

/* BATON is of type `rep_read_baton'; read the next *LEN bytes of the
   representation and store them in *BUF.  Sum as we read and verify
   the MD5 sum at the end. */
static svn_error_t *
rep_read_contents(void *baton,
                  char *buf,
                  apr_size_t *len)
{
  struct rep_read_baton *rb = baton;

  /* Get the next block of data. */
  SVN_ERR(get_contents(rb, buf, len));

  /* Perform checksumming.  We want to check the checksum as soon as
     the last byte of data is read, in case the caller never performs
     a short read, but we don't want to finalize the MD5 context
     twice. */
  if (!rb->checksum_finalized)
    {
      apr_md5_update(&rb->md5_context, buf, *len);
      rb->off += *len;
      if (rb->off == rb->len)
        {
          unsigned char checksum[APR_MD5_DIGESTSIZE];

          rb->checksum_finalized = TRUE;
          apr_md5_final(checksum, &rb->md5_context);
          if (! svn_md5_digests_match(checksum, rb->checksum))
            return svn_error_createf
              (SVN_ERR_FS_CORRUPT, NULL,
               _("Checksum mismatch while reading representation:\n"
                 "   expected:  %s\n"
                 "     actual:  %s\n"),
               svn_md5_digest_to_cstring_display(rb->checksum, rb->pool),
               svn_md5_digest_to_cstring_display(checksum, rb->pool));
        }
    }
  return SVN_NO_ERROR;
}

/* Return a stream in *CONTENTS_P that will read the contents of a
   representation stored at the location given by REP.  Appropriate
   for any kind of immutable representation, but only for file
   contents (not props or directory contents) in mutable
   representations.

   If REP is NULL, the representation is assumed to be empty, and the
   empty stream is returned.
*/
static svn_error_t *
read_representation(svn_stream_t **contents_p,
                    svn_fs_t *fs,
                    representation_t *rep,
                    apr_pool_t *pool)
{
  struct rep_read_baton *rb;

  if (! rep)
    {
      *contents_p = svn_stream_empty(pool);
    }
  else
    {
      SVN_ERR(rep_read_get_baton(&rb, fs, rep, pool));
      *contents_p = svn_stream_create(rb, pool);
      svn_stream_set_read(*contents_p, rep_read_contents);
      svn_stream_set_close(*contents_p, rep_read_contents_close);
    }
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_contents(svn_stream_t **contents_p,
                        svn_fs_t *fs,
                        node_revision_t *noderev,
                        apr_pool_t *pool)
{
  return read_representation(contents_p, fs, noderev->data_rep, pool);
}

/* Baton used when reading delta windows. */
struct delta_read_baton
{
  struct rep_state *rs;
  unsigned char checksum[APR_MD5_DIGESTSIZE];
};

/* This implements the svn_txdelta_next_window_fn_t interface. */
static svn_error_t *
delta_read_next_window(svn_txdelta_window_t **window, void *baton,
                       apr_pool_t *pool)
{
  struct delta_read_baton *drb = baton;

  if (drb->rs->off == drb->rs->end)
    {
      *window = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(read_window(window, drb->rs->chunk_index, drb->rs, pool));

  return SVN_NO_ERROR;
}

/* This implements the svn_txdelta_md5_digest_fn_t interface. */
static const unsigned char *
delta_read_md5_digest(void *baton)
{
  struct delta_read_baton *drb = baton;

  return drb->checksum;
}

svn_error_t *
svn_fs_fs__get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                                 svn_fs_t *fs,
                                 node_revision_t *source,
                                 node_revision_t *target,
                                 apr_pool_t *pool)
{
  svn_stream_t *source_stream, *target_stream;

  /* Try a shortcut: if the target is stored as a delta against the source,
     then just use that delta. */
  if (source && source->data_rep && target->data_rep)
    {
      struct rep_state *rep_state;
      struct rep_args *rep_args;

      /* Read target's base rep if any. */
      SVN_ERR(create_rep_state(&rep_state, &rep_args, target->data_rep,
                               fs, pool));
      /* If that matches source, then use this delta as is. */
      if (rep_args->is_delta
          && (rep_args->is_delta_vs_empty
              || (rep_args->base_revision == source->data_rep->revision
                  && rep_args->base_offset == source->data_rep->offset)))
        {
          /* Create the delta read baton. */
          struct delta_read_baton *drb = apr_pcalloc(pool, sizeof(*drb));
          drb->rs = rep_state;
          memcpy(drb->checksum, target->data_rep->checksum,
                 sizeof(drb->checksum));
          *stream_p = svn_txdelta_stream_create(drb, delta_read_next_window,
                                                delta_read_md5_digest, pool);
          return SVN_NO_ERROR;
        }
      else
        SVN_ERR(svn_io_file_close(rep_state->file, pool));
    }

  /* Read both fulltexts and construct a delta. */
  if (source)
    SVN_ERR(read_representation(&source_stream, fs, source->data_rep, pool));
  else
    source_stream = svn_stream_empty(pool);
  SVN_ERR(read_representation(&target_stream, fs, target->data_rep, pool));
  svn_txdelta(stream_p, source_stream, target_stream, pool);

  return SVN_NO_ERROR;
}


/* Fetch the contents of a directory into ENTRIES.  Values are stored
   as filename to string mappings; further conversion is necessary to
   convert them into svn_fs_dirent_t values. */
static svn_error_t *
get_dir_contents(apr_hash_t *entries,
                 svn_fs_t *fs,
                 node_revision_t *noderev,
                 apr_pool_t *pool)
{
  svn_stream_t *contents;
  
  if (noderev->data_rep && noderev->data_rep->txn_id)
    {
      apr_file_t *dir_file;
      const char *filename = path_txn_node_children(fs, noderev->id, pool);

      /* The representation is mutable.  Read the old directory
         contents from the mutable children file, followed by the
         changes we've made in this transaction. */
      SVN_ERR(svn_io_file_open(&dir_file, filename, APR_READ | APR_BUFFERED,
                               APR_OS_DEFAULT, pool));
      contents = svn_stream_from_aprfile(dir_file, pool);
      SVN_ERR(svn_hash_read2(entries, contents, SVN_HASH_TERMINATOR, pool));
      SVN_ERR(svn_hash_read_incremental(entries, contents, NULL, pool));
      SVN_ERR(svn_io_file_close(dir_file, pool));
    }
  else if (noderev->data_rep)
    {
      /* The representation is immutable.  Read it normally. */
      SVN_ERR(read_representation(&contents, fs, noderev->data_rep, pool));
      SVN_ERR(svn_hash_read2(entries, contents, SVN_HASH_TERMINATOR, pool));
      SVN_ERR(svn_stream_close(contents));
    }
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__rep_contents_dir(apr_hash_t **entries_p,
                            svn_fs_t *fs,
                            node_revision_t *noderev,
                            apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  unsigned int hid;
  
  /* Calculate an index into the dir entries cache */
  hid = DIR_CACHE_ENTRIES_MASK(svn_fs_fs__id_rev(noderev->id));

  /* If we have this directory cached, return it. */
  if (ffd->dir_cache_id[hid] && svn_fs_fs__id_eq(ffd->dir_cache_id[hid],
                                                 noderev->id))
    {
      *entries_p = ffd->dir_cache[hid];
      return SVN_NO_ERROR;
    }

  /* Read in the directory hash. */
  entries = apr_hash_make(pool);
  SVN_ERR(get_dir_contents(entries, fs, noderev, pool));

  /* Prepare to cache this directory. */
  ffd->dir_cache_id[hid] = NULL;
  if (ffd->dir_cache_pool[hid])
    svn_pool_clear(ffd->dir_cache_pool[hid]);
  else
    ffd->dir_cache_pool[hid] = svn_pool_create(fs->pool);
  ffd->dir_cache[hid] = apr_hash_make(ffd->dir_cache_pool[hid]);

  /* Translate the string dir entries into real entries in the dir cache. */
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      char *str_val;
      char *str, *last_str;
      svn_fs_dirent_t *dirent = apr_pcalloc(ffd->dir_cache_pool[hid],
                                            sizeof(*dirent));

      apr_hash_this(hi, &key, NULL, &val);
      str_val = apr_pstrdup(pool, *((char **)val));
      dirent->name = apr_pstrdup(ffd->dir_cache_pool[hid], key);

      str = apr_strtok(str_val, " ", &last_str);
      if (str == NULL)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Directory entry corrupt"));
      
      if (strcmp(str, KIND_FILE) == 0)
        {
          dirent->kind = svn_node_file;
        }
      else if (strcmp(str, KIND_DIR) == 0)
        {
          dirent->kind = svn_node_dir;
        }
      else
        {
          return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                  _("Directory entry corrupt"));
        }

      str = apr_strtok(NULL, " ", &last_str);
      if (str == NULL)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Directory entry corrupt"));
      
      dirent->id = svn_fs_fs__id_parse(str, strlen(str),
                                       ffd->dir_cache_pool[hid]);

      apr_hash_set(ffd->dir_cache[hid], dirent->name, APR_HASH_KEY_STRING, dirent);
    }

  /* Mark which directory we've cached and return it. */
  ffd->dir_cache_id[hid] = svn_fs_fs__id_copy(noderev->id, ffd->dir_cache_pool[hid]);
  *entries_p = ffd->dir_cache[hid];
  return SVN_NO_ERROR;
}

apr_hash_t *
svn_fs_fs__copy_dir_entries(apr_hash_t *entries,
                            apr_pool_t *pool)
{
  apr_hash_t *new_entries = apr_hash_make(pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      void *val;
      svn_fs_dirent_t *dirent, *new_dirent;

      apr_hash_this(hi, NULL, NULL, &val);
      dirent = val;
      new_dirent = apr_palloc(pool, sizeof(*new_dirent));
      new_dirent->name = apr_pstrdup(pool, dirent->name);
      new_dirent->kind = dirent->kind;
      new_dirent->id = svn_fs_fs__id_copy(dirent->id, pool);
      apr_hash_set(new_entries, new_dirent->name, APR_HASH_KEY_STRING,
                   new_dirent);
    }
  return new_entries;
}

svn_error_t *
svn_fs_fs__get_proplist(apr_hash_t **proplist_p,
                        svn_fs_t *fs,
                        node_revision_t *noderev,
                        apr_pool_t *pool)
{
  apr_hash_t *proplist;
  svn_stream_t *stream;

  proplist = apr_hash_make(pool);

  if (noderev->prop_rep && noderev->prop_rep->txn_id)
    {
      apr_file_t *props_file;
      const char *filename = path_txn_node_props(fs, noderev->id, pool);

      SVN_ERR(svn_io_file_open(&props_file, filename,
                               APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                               pool));
      stream = svn_stream_from_aprfile(props_file, pool);
      SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, pool));
      SVN_ERR(svn_io_file_close(props_file, pool));
    }
  else if (noderev->prop_rep)
    {
      SVN_ERR(read_representation(&stream, fs, noderev->prop_rep, pool));
      SVN_ERR(svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, pool));
      SVN_ERR(svn_stream_close(stream));
    }

  *proplist_p = proplist;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__file_length(svn_filesize_t *length,
                       node_revision_t *noderev,
                       apr_pool_t *pool)
{
  if (noderev->data_rep)
    *length = noderev->data_rep->expanded_size;
  else
    *length = 0;

  return SVN_NO_ERROR;
}

svn_boolean_t
svn_fs_fs__noderev_same_rep_key(representation_t *a,
                                representation_t *b)
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
svn_fs_fs__file_checksum(unsigned char digest[],
                         node_revision_t *noderev,
                         apr_pool_t *pool)
{
  if (noderev->data_rep)
    memcpy(digest, noderev->data_rep->checksum, APR_MD5_DIGESTSIZE);
  else
    memset(digest, 0, APR_MD5_DIGESTSIZE);

  return SVN_NO_ERROR;
}

representation_t *
svn_fs_fs__rep_copy(representation_t *rep,
                    apr_pool_t *pool)
{
  representation_t *rep_new;
  
  if (rep == NULL)
    return NULL;
    
  rep_new = apr_pcalloc(pool, sizeof(*rep_new));
  
  memcpy(rep_new, rep, sizeof(*rep_new));
  
  return rep_new;
}

/* Merge the internal-use-only CHANGE into a hash of public-FS
   svn_fs_path_change_t CHANGES, collapsing multiple changes into a
   single summarical (is that real word?) change per path.  Also keep
   the COPYFROM_HASH up to date with new adds and replaces.  */
static svn_error_t *
fold_change(apr_hash_t *changes,
            const change_t *change,
            apr_hash_t *copyfrom_hash)
{
  apr_pool_t *pool = apr_hash_pool_get(changes);
  apr_pool_t *copyfrom_pool = apr_hash_pool_get(copyfrom_hash);
  svn_fs_path_change_t *old_change, *new_change;
  const char *path, *copyfrom_string, *copyfrom_path = NULL;

  if ((old_change = apr_hash_get(changes, change->path, APR_HASH_KEY_STRING)))
    {
      /* This path already exists in the hash, so we have to merge
         this change into the already existing one. */

      /* Get the existing copyfrom entry for this path. */
      copyfrom_string = apr_hash_get(copyfrom_hash, change->path,
                                     APR_HASH_KEY_STRING);

      /* If this entry existed in the copyfrom hash, we don't need to
         copy it. */
      if (copyfrom_string)
        copyfrom_path = change->path;

      /* Since the path already exists in the hash, we don't have to
         dup the allocation for the path itself. */
      path = change->path;
      /* Sanity check:  only allow NULL node revision ID in the
         `reset' case. */
      if ((! change->noderev_id) && (change->kind != svn_fs_path_change_reset))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Missing required node revision ID"));

      /* Sanity check: we should be talking about the same node
         revision ID as our last change except where the last change
         was a deletion. */
      if (change->noderev_id
          && (! svn_fs_fs__id_eq(old_change->node_rev_id, change->noderev_id))
          && (old_change->change_kind != svn_fs_path_change_delete))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: new node revision ID "
             "without delete"));

      /* Sanity check: an add, replacement, or reset must be the first
         thing to follow a deletion. */
      if ((old_change->change_kind == svn_fs_path_change_delete)
          && (! ((change->kind == svn_fs_path_change_replace)
                 || (change->kind == svn_fs_path_change_reset)
                 || (change->kind == svn_fs_path_change_add))))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: non-add change on deleted path"));

      /* Now, merge that change in. */
      switch (change->kind)
        {
        case svn_fs_path_change_reset:
          /* A reset here will simply remove the path change from the
             hash. */
          old_change = NULL;
          copyfrom_string = NULL;
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
          copyfrom_string = NULL;
          break;

        case svn_fs_path_change_add:
        case svn_fs_path_change_replace:
          /* An add at this point must be following a previous delete,
             so treat it just like a replace. */
          old_change->change_kind = svn_fs_path_change_replace;
          old_change->node_rev_id = svn_fs_fs__id_copy(change->noderev_id,
                                                       pool);
          old_change->text_mod = change->text_mod;
          old_change->prop_mod = change->prop_mod;
          if (change->copyfrom_rev == SVN_INVALID_REVNUM)
            copyfrom_string = apr_pstrdup(copyfrom_pool, "");
          else
            {
              copyfrom_string = apr_psprintf(copyfrom_pool,
                                             "%ld %s",
                                             change->copyfrom_rev,
                                             change->copyfrom_path);
            }
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
      new_change = apr_pcalloc(pool, sizeof(*new_change));
      new_change->node_rev_id = svn_fs_fs__id_copy(change->noderev_id, pool);
      new_change->change_kind = change->kind;
      new_change->text_mod = change->text_mod;
      new_change->prop_mod = change->prop_mod;
      if (change->copyfrom_rev != SVN_INVALID_REVNUM)
        {
          copyfrom_string = apr_psprintf(copyfrom_pool, "%ld %s",
                                         change->copyfrom_rev,
                                         change->copyfrom_path);
        }
      else
        copyfrom_string = apr_pstrdup(copyfrom_pool, "");
      path = apr_pstrdup(pool, change->path);
    }

  /* Add (or update) this path. */
  apr_hash_set(changes, path, APR_HASH_KEY_STRING, new_change);

  /* If copyfrom_path is non-NULL, the key is already present in the
     hash, so we don't need to duplicate it in the copyfrom pool. */
  if (! copyfrom_path)
    {
      /* If copyfrom_string is NULL, the hash entry will be deleted,
         so we don't need to duplicate the key in the copyfrom
         pool. */
      copyfrom_path = copyfrom_string ? apr_pstrdup(copyfrom_pool, path)
        : path;
    }
  
  apr_hash_set(copyfrom_hash, copyfrom_path, APR_HASH_KEY_STRING,
               copyfrom_string);

  return SVN_NO_ERROR;
}


/* Read the next entry in the changes record from file FILE and store
   the resulting change in *CHANGE_P.  If there is no next record,
   store NULL there.  Perform all allocations from POOL. */
static svn_error_t *
read_change(change_t **change_p,
            apr_file_t *file,
            apr_pool_t *pool)
{
  char buf[4096];
  apr_size_t len = sizeof(buf);
  change_t *change;
  char *str, *last_str;
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
      return err;
    }

  change = apr_pcalloc(pool, sizeof(*change));

  /* Get the node-id of the change. */
  str = apr_strtok(buf, " ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  change->noderev_id = svn_fs_fs__id_parse(str, strlen(str), pool);

  /* Get the change type. */
  str = apr_strtok(NULL, " ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

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
  str = apr_strtok(NULL, " ", &last_str);
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
  str = apr_strtok(NULL, " ", &last_str);
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
      str = apr_strtok(buf, " ", &last_str);
      if (! str)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid changes line in rev-file"));
      change->copyfrom_rev = atol(str);

      if (! last_str)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid changes line in rev-file"));

      change->copyfrom_path = apr_pstrdup(pool, last_str);
    }
  
  *change_p = change;

  return SVN_NO_ERROR;
}

/* Fetch all the changed path entries from FILE and store then in
   *CHANGED_PATHS.  Folding is done to remove redundant or unnecessary
   *data.  Store a hash of paths to copyfrom revisions/paths in
   COPYFROM_HASH if it is non-NULL.  If PREFOLDED is true, assume that
   the changed-path entries have already been folded (by
   write_final_changed_path_info) and may be out of order, so we shouldn't
   remove children of replaced or deleted directories.  Do all
   allocations in POOL. */
static svn_error_t *
fetch_all_changes(apr_hash_t *changed_paths,
                  apr_hash_t *copyfrom_hash,
                  apr_file_t *file,
                  svn_boolean_t prefolded,
                  apr_pool_t *pool)
{
  change_t *change;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_t *my_hash;

  /* If we are passed a NULL copyfrom hash, manufacture one for the
     duration of this call. */
  my_hash = copyfrom_hash ? copyfrom_hash : apr_hash_make(pool);
  
  /* Read in the changes one by one, folding them into our local hash
     as necessary. */
  
  SVN_ERR(read_change(&change, file, iterpool));

  while (change)
    {
      SVN_ERR(fold_change(changed_paths, change, my_hash));

      /* Now, if our change was a deletion or replacement, we have to
         blow away any changes thus far on paths that are (or, were)
         children of this path.
         ### i won't bother with another iteration pool here -- at
         most we talking about a few extra dups of paths into what
         is already a temporary subpool.
      */

      if (((change->kind == svn_fs_path_change_delete)
           || (change->kind == svn_fs_path_change_replace))
          && ! prefolded)
        {
          apr_hash_index_t *hi;

          for (hi = apr_hash_first(iterpool, changed_paths);
               hi;
               hi = apr_hash_next(hi))
            {
              /* KEY is the path. */
              const void *hashkey;
              apr_ssize_t klen;
              apr_hash_this(hi, &hashkey, &klen, NULL);

              /* If we come across our own path, ignore it. */
              if (strcmp(change->path, hashkey) == 0)
                continue;

              /* If we come across a child of our path, remove it. */
              if (svn_path_is_child(change->path, hashkey, iterpool))
                apr_hash_set(changed_paths, hashkey, klen, NULL);
            }
        }

      /* Clear the per-iteration subpool. */
      svn_pool_clear(iterpool);

      SVN_ERR(read_change(&change, file, iterpool));
    }

  /* Destroy the per-iteration subpool. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__txn_changes_fetch(apr_hash_t **changed_paths_p,
                             svn_fs_t *fs,
                             const char *txn_id,
                             apr_hash_t *copyfrom_cache,
                             apr_pool_t *pool)
{
  apr_file_t *file;
  apr_hash_t *changed_paths = apr_hash_make(pool);

  SVN_ERR(svn_io_file_open(&file, path_txn_changes(fs, txn_id, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  SVN_ERR(fetch_all_changes(changed_paths, copyfrom_cache, file, FALSE,
                            pool));

  SVN_ERR(svn_io_file_close(file, pool));

  *changed_paths_p = changed_paths;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__paths_changed(apr_hash_t **changed_paths_p,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_hash_t *copyfrom_cache,
                         apr_pool_t *pool)
{
  apr_off_t changes_offset;
  apr_hash_t *changed_paths;
  apr_file_t *revision_file;
  
  SVN_ERR(svn_io_file_open(&revision_file, svn_fs_fs__path_rev(fs, rev, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  SVN_ERR(get_root_changes_offset(NULL, &changes_offset, revision_file,
                                  pool));

  SVN_ERR(svn_io_file_seek(revision_file, APR_SET, &changes_offset, pool));

  changed_paths = apr_hash_make(pool);

  SVN_ERR(fetch_all_changes(changed_paths, copyfrom_cache, revision_file,
                            TRUE, pool));
  
  /* Close the revision file. */
  SVN_ERR(svn_io_file_close(revision_file, pool));

  *changed_paths_p = changed_paths;

  return SVN_NO_ERROR;
}

/* Copy a revision node-rev SRC into the current transaction TXN_ID in
   the filesystem FS.  Allocations are from POOL.  */
static svn_error_t *
create_new_txn_noderev_from_rev(svn_fs_t *fs,
                                const char *txn_id,
                                svn_fs_id_t *src,
                                apr_pool_t *pool)
{
  node_revision_t *noderev;
  const char *node_id, *copy_id;

  SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, src, pool));

  if (svn_fs_fs__id_txn_id(noderev->id))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Copying from transactions not allowed"));

  noderev->predecessor_id = noderev->id;
  noderev->predecessor_count++;
  noderev->copyfrom_path = NULL;
  noderev->copyfrom_rev = SVN_INVALID_REVNUM;

  /* For the transaction root, the copyroot never changes. */

  node_id = svn_fs_fs__id_node_id(noderev->id);
  copy_id = svn_fs_fs__id_copy_id(noderev->id);
  noderev->id = svn_fs_fs__id_txn_create(node_id, copy_id, txn_id, pool);

  SVN_ERR(svn_fs_fs__put_node_revision(fs, noderev->id, noderev, pool));

  return SVN_NO_ERROR;
}

/* Create a unique directory for a transaction in FS based on revision
   REV.  Return the ID for this transaction in *ID_P. */
static svn_error_t *
create_txn_dir(const char **id_p, svn_fs_t *fs, svn_revnum_t rev,
               apr_pool_t *pool)
{
  unsigned int i;
  apr_pool_t *subpool;
  const char *unique_path, *name, *prefix;

  /* Try to create directories named "<txndir>/<rev>-<uniquifier>.txn". */
  prefix = svn_path_join_many(pool, fs->path, PATH_TXNS_DIR,
                              apr_psprintf(pool, "%ld", rev), NULL);

  subpool = svn_pool_create(pool);
  for (i = 1; i <= 99999; i++)
    {
      svn_error_t *err;

      svn_pool_clear(subpool);
      unique_path = apr_psprintf(subpool, "%s-%u" PATH_EXT_TXN, prefix, i);
      err = svn_io_dir_make(unique_path, APR_OS_DEFAULT, subpool);
      if (! err)
        {
          /* We succeeded.  Return the basename minus the ".txn" extension. */
          name = svn_path_basename(unique_path, subpool);
          *id_p = apr_pstrndup(pool, name,
                               strlen(name) - strlen(PATH_EXT_TXN));
          svn_pool_destroy(subpool);
          return SVN_NO_ERROR;
        }
      if (! APR_STATUS_IS_EEXIST(err->apr_err))
        return err;
      svn_error_clear(err);
    }

  return svn_error_createf(SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED,
                           NULL,
                           _("Unable to create transaction directory "
                             "in '%s' for revision %ld"),
                           fs->path, rev);
}

svn_error_t *
svn_fs_fs__create_txn(svn_fs_txn_t **txn_p,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  svn_fs_txn_t *txn;
  svn_fs_id_t *root_id;

  txn = apr_pcalloc(pool, sizeof(*txn));

  /* Get the txn_id. */
  SVN_ERR(create_txn_dir(&txn->id, fs, rev, pool));

  txn->fs = fs;
  txn->base_rev = rev;

  txn->vtable = &txn_vtable;
  txn->fsap_data = NULL;
  *txn_p = txn;
  
  /* Create a new root node for this transaction. */
  SVN_ERR(svn_fs_fs__rev_get_root(&root_id, fs, rev, pool));
  SVN_ERR(create_new_txn_noderev_from_rev(fs, txn->id, root_id, pool));

  /* Create an empty rev file. */
  SVN_ERR(svn_io_file_create(path_txn_proto_rev(fs, txn->id, pool), "",
                             pool));

  /* Create an empty rev-lock file. */
  SVN_ERR(svn_io_file_create(path_txn_proto_rev_lock(fs, txn->id, pool), "",
                             pool));

  /* Create an empty changes file. */
  SVN_ERR(svn_io_file_create(path_txn_changes(fs, txn->id, pool), "",
                             pool));

  /* Create the next-ids file. */
  SVN_ERR(svn_io_file_create(path_txn_next_ids(fs, txn->id, pool), "0 0\n",
                             pool));

  return SVN_NO_ERROR;
}

/* Store the property list for transaction TXN_ID in PROPLIST.
   Perform temporary allocations in POOL. */
static svn_error_t *
get_txn_proplist(apr_hash_t *proplist,
                 svn_fs_t *fs,
                 const char *txn_id,
                 apr_pool_t *pool)
{
  apr_file_t *txn_prop_file;

  /* Open the transaction properties file. */
  SVN_ERR(svn_io_file_open(&txn_prop_file, path_txn_props(fs, txn_id, pool),
                           APR_READ | APR_CREATE | APR_BUFFERED,
                           APR_OS_DEFAULT, pool));

  /* Read in the property list. */
  SVN_ERR(svn_hash_read(proplist, txn_prop_file, pool));

  SVN_ERR(svn_io_file_close(txn_prop_file, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__change_txn_prop(svn_fs_txn_t *txn,
                           const char *name,
                           const svn_string_t *value,
                           apr_pool_t *pool)
{
  apr_file_t *txn_prop_file;
  apr_hash_t *txn_prop = apr_hash_make(pool);

  SVN_ERR(get_txn_proplist(txn_prop, txn->fs, txn->id, pool));

  apr_hash_set(txn_prop, name, APR_HASH_KEY_STRING, value);

  /* Create a new version of the file and write out the new props. */
  /* Open the transaction properties file. */
  SVN_ERR(svn_io_file_open(&txn_prop_file,
                           path_txn_props(txn->fs, txn->id, pool),
                           APR_WRITE | APR_CREATE | APR_TRUNCATE
                           | APR_BUFFERED, APR_OS_DEFAULT, pool));

  SVN_ERR(svn_hash_write(txn_prop, txn_prop_file, pool));

  SVN_ERR(svn_io_file_close(txn_prop_file, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_txn(transaction_t **txn_p,
                   svn_fs_t *fs,
                   const char *txn_id,
                   apr_pool_t *pool)
{
  transaction_t *txn;
  node_revision_t *noderev;
  svn_fs_id_t *root_id;

  txn = apr_pcalloc(pool, sizeof(*txn));
  txn->proplist = apr_hash_make(pool);

  SVN_ERR(get_txn_proplist(txn->proplist, fs, txn_id, pool));
  root_id = svn_fs_fs__id_txn_create("0", "0", txn_id, pool);

  SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, root_id, pool));

  txn->root_id = svn_fs_fs__id_copy(noderev->id, pool);
  txn->base_id = svn_fs_fs__id_copy(noderev->predecessor_id, pool);
  txn->copies = NULL;

  txn->kind = transaction_kind_normal;

  *txn_p = txn;

  return SVN_NO_ERROR;
}

/* Write out the currently available next node_id NODE_ID and copy_id
   COPY_ID for transaction TXN_ID in filesystem FS.  Perform temporary
   allocations in POOL. */
static svn_error_t *
write_next_ids(svn_fs_t *fs,
               const char *txn_id,
               const char *node_id,
               const char *copy_id,
               apr_pool_t *pool)
{
  apr_file_t *file;
  svn_stream_t *out_stream;

  SVN_ERR(svn_io_file_open(&file, path_txn_next_ids(fs, txn_id, pool),
                           APR_WRITE | APR_TRUNCATE,
                           APR_OS_DEFAULT, pool));

  out_stream = svn_stream_from_aprfile(file, pool);

  SVN_ERR(svn_stream_printf(out_stream, pool, "%s %s\n", node_id, copy_id));

  SVN_ERR(svn_stream_close(out_stream));
  SVN_ERR(svn_io_file_close(file, pool));

  return SVN_NO_ERROR;
}

/* Find out what the next unique node-id and copy-id are for
   transaction TXN_ID in filesystem FS.  Store the results in *NODE_ID
   and *COPY_ID.  Perform all allocations in POOL. */
static svn_error_t *
read_next_ids(const char **node_id,
              const char **copy_id,
              svn_fs_t *fs,
              const char *txn_id,
              apr_pool_t *pool)
{
  apr_file_t *file;
  char buf[MAX_KEY_SIZE*2+3];
  apr_size_t limit;
  char *str, *last_str;

  SVN_ERR(svn_io_file_open(&file, path_txn_next_ids(fs, txn_id, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  limit = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(file, buf, &limit, pool));

  SVN_ERR(svn_io_file_close(file, pool));

  /* Parse this into two separate strings. */

  str = apr_strtok(buf, " ", &last_str);
  if (! str)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("next-id file corrupt"));

  *node_id = apr_pstrdup(pool, str);

  str = apr_strtok(NULL, " ", &last_str);
  if (! str)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("next-id file corrupt"));

  *copy_id = apr_pstrdup(pool, str);

  return SVN_NO_ERROR;
}

/* Get a new and unique to this transaction node-id for transaction
   TXN_ID in filesystem FS.  Store the new node-id in *NODE_ID_P.
   Perform all allocations in POOL. */
static svn_error_t *
get_new_txn_node_id(const char **node_id_p,
                    svn_fs_t *fs,
                    const char *txn_id,
                    apr_pool_t *pool)
{
  const char *cur_node_id, *cur_copy_id;
  char *node_id;
  apr_size_t len;

  /* First read in the current next-ids file. */
  SVN_ERR(read_next_ids(&cur_node_id, &cur_copy_id, fs, txn_id, pool));

  node_id = apr_pcalloc(pool, strlen(cur_node_id) + 2);

  len = strlen(cur_node_id);
  svn_fs_fs__next_key(cur_node_id, &len, node_id);

  SVN_ERR(write_next_ids(fs, txn_id, node_id, cur_copy_id, pool));

  *node_id_p = apr_pstrcat(pool, "_", cur_node_id, NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__create_node(const svn_fs_id_t **id_p,
                       svn_fs_t *fs,
                       node_revision_t *noderev,
                       const char *copy_id,
                       const char *txn_id,
                       apr_pool_t *pool)
{
  const char *node_id;
  const svn_fs_id_t *id;

  /* Get a new node-id for this node. */
  SVN_ERR(get_new_txn_node_id(&node_id, fs, txn_id, pool));

  id = svn_fs_fs__id_txn_create(node_id, copy_id, txn_id, pool);

  noderev->id = id;

  SVN_ERR(svn_fs_fs__put_node_revision(fs, noderev->id, noderev, pool));

  *id_p = id;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__purge_txn(svn_fs_t *fs,
                     const char *txn_id,
                     apr_pool_t *pool)
{
  /* Remove the shared transaction object associated with this transaction. */
  SVN_ERR(purge_shared_txn(fs, txn_id, pool));
  /* Remove the directory associated with this transaction. */
  return svn_io_remove_dir(path_txn_dir(fs, txn_id, pool), pool);
}


svn_error_t *
svn_fs_fs__abort_txn(svn_fs_txn_t *txn,
                     apr_pool_t *pool)
{
  fs_fs_data_t *ffd; 

  SVN_ERR(svn_fs_fs__check_fs(txn->fs));

  /* Clean out the directory cache. */
  ffd = txn->fs->fsap_data;
  memset(&ffd->dir_cache_id, 0, 
         sizeof(apr_hash_t *) * NUM_DIR_CACHE_ENTRIES);

  /* Now, purge the transaction. */
  SVN_ERR_W(svn_fs_fs__purge_txn(txn->fs, txn->id, pool),
            _("Transaction cleanup failed"));

  return SVN_NO_ERROR;
}


static const char *
unparse_dir_entry(svn_node_kind_t kind, const svn_fs_id_t *id,
                  apr_pool_t *pool)
{
  return apr_psprintf(pool, "%s %s",
                      (kind == svn_node_file) ? KIND_FILE : KIND_DIR,
                      svn_fs_fs__id_unparse(id, pool)->data);
}

/* Given a hash ENTRIES of dirent structions, return a hash in
 *STR_ENTRIES_P, that has svn_string_t as the values in the format
 specified by the fs_fs directory contents file.  Perform
 allocations in POOL. */
static svn_error_t *
unparse_dir_entries(apr_hash_t **str_entries_p,
                    apr_hash_t *entries,
                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  *str_entries_p = apr_hash_make(pool);

  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_fs_dirent_t *dirent;
      const char *new_val;

      apr_hash_this(hi, &key, &klen, &val);
      dirent = val;
      new_val = unparse_dir_entry(dirent->kind, dirent->id, pool);
      apr_hash_set(*str_entries_p, key, klen,
                   svn_string_create(new_val, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__set_entry(svn_fs_t *fs,
                     const char *txn_id,
                     node_revision_t *parent_noderev,
                     const char *name,
                     const svn_fs_id_t *id,
                     svn_node_kind_t kind,
                     apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  representation_t *rep = parent_noderev->data_rep;
  const char *filename = path_txn_node_children(fs, parent_noderev->id, pool);
  apr_file_t *file;
  svn_stream_t *out;
  svn_boolean_t have_cached;
  unsigned int hid;

  if (!rep || !rep->txn_id)
    {
      apr_hash_t *entries;

      /* Before we can modify the directory, we need to dump its old
         contents into a mutable representation file. */
      SVN_ERR(svn_fs_fs__rep_contents_dir(&entries, fs, parent_noderev,
                                          pool));
      SVN_ERR(unparse_dir_entries(&entries, entries, pool));
      SVN_ERR(svn_io_file_open(&file, filename,
                               APR_WRITE | APR_CREATE | APR_BUFFERED,
                               APR_OS_DEFAULT, pool));
      out = svn_stream_from_aprfile(file, pool);
      SVN_ERR(svn_hash_write2(entries, out, SVN_HASH_TERMINATOR, pool));

      /* Mark the node-rev's data rep as mutable. */
      rep = apr_pcalloc(pool, sizeof(*rep));
      rep->revision = SVN_INVALID_REVNUM;
      rep->txn_id = txn_id;
      parent_noderev->data_rep = rep;
      SVN_ERR(svn_fs_fs__put_node_revision(fs, parent_noderev->id,
                                           parent_noderev, pool));
    }
  else
    {
      /* The directory rep is already mutable, so just open it for append. */
      SVN_ERR(svn_io_file_open(&file, filename, APR_WRITE | APR_APPEND,
                               APR_OS_DEFAULT, pool));
      out = svn_stream_from_aprfile(file, pool);
    }

  /* Calculate an index into the dir entries cache.  */
  hid = DIR_CACHE_ENTRIES_MASK(svn_fs_fs__id_rev(parent_noderev->id));

  /* Make a note if we have this directory cached. */
  have_cached = (ffd->dir_cache_id[hid]
                 && svn_fs_fs__id_eq(ffd->dir_cache_id[hid], parent_noderev->id));

  /* Append an incremental hash entry for the entry change, and update
     the cached directory if necessary. */
  if (id)
    {
      const char *val = unparse_dir_entry(kind, id, pool);

      SVN_ERR(svn_stream_printf(out, pool, "K %" APR_SIZE_T_FMT "\n%s\n"
                                "V %" APR_SIZE_T_FMT "\n%s\n",
                                strlen(name), name,
                                strlen(val), val));
      if (have_cached)
        {
          svn_fs_dirent_t *dirent;

          dirent = apr_palloc(ffd->dir_cache_pool[hid], sizeof(*dirent));
          dirent->name = apr_pstrdup(ffd->dir_cache_pool[hid], name);
          dirent->kind = kind;
          dirent->id = svn_fs_fs__id_copy(id, ffd->dir_cache_pool[hid]);
          apr_hash_set(ffd->dir_cache[hid], dirent->name, APR_HASH_KEY_STRING,
                       dirent);
        }
    }
  else
    {
      SVN_ERR(svn_stream_printf(out, pool, "D %" APR_SIZE_T_FMT "\n%s\n",
                                strlen(name), name));
      if (have_cached)
        apr_hash_set(ffd->dir_cache[hid], name, APR_HASH_KEY_STRING, NULL);
    }

  SVN_ERR(svn_io_file_close(file, pool));
  return SVN_NO_ERROR;
}

/* Write a single change entry, path PATH, change CHANGE, and copyfrom
   string COPYFROM, into the file specified by FILE.  All temporary
   allocations are in POOL. */
static svn_error_t *
write_change_entry(apr_file_t *file,
                   const char *path,
                   svn_fs_path_change_t *change,
                   const char *copyfrom,
                   apr_pool_t *pool)
{
  const char *idstr, *buf;
  const char *change_string = NULL;
  
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
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Invalid change type"));
    }

  if (change->node_rev_id)
    idstr = svn_fs_fs__id_unparse(change->node_rev_id, pool)->data;
  else
      idstr = ACTION_RESET;
  
  buf = apr_psprintf(pool, "%s %s %s %s %s\n",
                     idstr, change_string,
                     change->text_mod ? FLAG_TRUE : FLAG_FALSE,
                     change->prop_mod ? FLAG_TRUE : FLAG_FALSE,
                     path);

  SVN_ERR(svn_io_file_write_full(file, buf, strlen(buf), NULL, pool));

  if (copyfrom)
    {
      SVN_ERR(svn_io_file_write_full(file, copyfrom, strlen(copyfrom),
                                     NULL, pool));
    }

  SVN_ERR(svn_io_file_write_full(file, "\n", 1, NULL, pool));
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__add_change(svn_fs_t *fs,
                      const char *txn_id,
                      const char *path,
                      const svn_fs_id_t *id,
                      svn_fs_path_change_kind_t change_kind,
                      svn_boolean_t text_mod,
                      svn_boolean_t prop_mod,
                      svn_revnum_t copyfrom_rev,
                      const char *copyfrom_path,
                      apr_pool_t *pool)
{
  apr_file_t *file;
  const char *copyfrom;
  svn_fs_path_change_t *change = apr_pcalloc(pool, sizeof(*change));

  SVN_ERR(svn_io_file_open(&file, path_txn_changes(fs, txn_id, pool),
                           APR_APPEND | APR_WRITE | APR_CREATE
                           | APR_BUFFERED, APR_OS_DEFAULT, pool));

  if (copyfrom_rev != SVN_INVALID_REVNUM)
    copyfrom = apr_psprintf(pool, "%ld %s", copyfrom_rev, copyfrom_path);
  else
    copyfrom = "";

  change->node_rev_id = id;
  change->change_kind = change_kind;
  change->text_mod = text_mod;
  change->prop_mod = prop_mod;
  
  SVN_ERR(write_change_entry(file, path, change, copyfrom, pool));

  SVN_ERR(svn_io_file_close(file, pool));

  return SVN_NO_ERROR;
}

/* This baton is used by the representation writing streams.  It keeps
   track of the checksum information as well as the total size of the
   representation so far. */
struct rep_write_baton
{
  /* The FS we are writing to. */
  svn_fs_t *fs;

  /* Actual file to which we are writing. */
  svn_stream_t *rep_stream;

  /* A stream from the delta combiner.  Data written here gets
     deltified, then eventually written to rep_stream. */
  svn_stream_t *delta_stream;

  /* Where is this representation header stored. */
  apr_off_t rep_offset;

  /* Start of the actual data. */
  apr_off_t delta_start;

  /* How many bytes have been written to this rep already. */
  svn_filesize_t rep_size;

  /* The node revision for which we're writing out info. */
  node_revision_t *noderev;

  /* Actual output file. */
  apr_file_t *file;
  /* Lock 'cookie' used to unlock the output file once we've finished
     writing to it. */
  void *lockcookie;

  struct apr_md5_ctx_t md5_context;

  apr_pool_t *pool;

  apr_pool_t *parent_pool;
};

/* Handler for the write method of the representation writable stream.
   BATON is a rep_write_baton, DATA is the data to write, and *LEN is
   the length of this data. */
static svn_error_t *
rep_write_contents(void *baton,
                   const char *data,
                   apr_size_t *len)
{
  struct rep_write_baton *b = baton;

  apr_md5_update(&b->md5_context, data, *len);
  b->rep_size += *len;

  /* If we are writing a delta, use that stream. */
  if (b->delta_stream)
    {
      SVN_ERR(svn_stream_write(b->delta_stream, data, len));
    }
  else
    {
      SVN_ERR(svn_stream_write(b->rep_stream, data, len));
    }
  
  return SVN_NO_ERROR;
}

/* Given a node-revision NODEREV in filesystem FS, return the
   representation in *REP to use as the base for a text representation
   delta.  Perform temporary allocations in *POOL. */
static svn_error_t *
choose_delta_base(representation_t **rep,
                  svn_fs_t *fs,
                  node_revision_t *noderev,
                  apr_pool_t *pool)
{
  int count;
  node_revision_t *base;

  /* If we have no predecessors, then use the empty stream as a
     base. */
  if (! noderev->predecessor_count)
    {
      *rep = NULL;
      return SVN_NO_ERROR;
    }

  /* Flip the rightmost '1' bit of the predecessor count to determine
     which file rev (counting from 0) we want to use.  (To see why
     count & (count - 1) unsets the rightmost set bit, think about how
     you decrement a binary number.) */
  count = noderev->predecessor_count;
  count = count & (count - 1);

  /* Walk back a number of predecessors equal to the difference
     between count and the original predecessor count.  (For example,
     if noderev has ten predecessors and we want the eighth file rev,
     walk back two predecessors.) */
  base = noderev;
  while ((count++) < noderev->predecessor_count)
    SVN_ERR(svn_fs_fs__get_node_revision(&base, fs,
                                         base->predecessor_id, pool));

  *rep = base->data_rep;
  
  return SVN_NO_ERROR;
}

/* Get a rep_write_baton and store it in *WB_P for the representation
   indicated by NODEREV in filesystem FS.  Perform allocations in
   POOL.  Only appropriate for file contents, not for props or
   directory contents. */
static svn_error_t *
rep_write_get_baton(struct rep_write_baton **wb_p,
                    svn_fs_t *fs,
                    node_revision_t *noderev,
                    apr_pool_t *pool)
{
  struct rep_write_baton *b;
  apr_file_t *file;
  representation_t *base_rep;
  svn_stream_t *source;
  const char *header;
  svn_txdelta_window_handler_t wh;
  void *whb;
  fs_fs_data_t *ffd = fs->fsap_data;

  b = apr_pcalloc(pool, sizeof(*b));

  apr_md5_init(&(b->md5_context));

  b->fs = fs;
  b->parent_pool = pool;
  b->pool = svn_pool_create(pool);
  b->rep_size = 0;
  b->noderev = noderev;

  /* Open the prototype rev file and seek to its end. */
  SVN_ERR(get_writable_proto_rev(&file, &b->lockcookie,
                                 fs, svn_fs_fs__id_txn_id(noderev->id),
                                 b->pool));

  b->file = file;
  b->rep_stream = svn_stream_from_aprfile(file, b->pool);

  SVN_ERR(get_file_offset(&b->rep_offset, file, b->pool));

  /* Get the base for this delta. */
  SVN_ERR(choose_delta_base(&base_rep, fs, noderev, b->pool));
  SVN_ERR(read_representation(&source, fs, base_rep, b->pool));

  /* Write out the rep header. */
  if (base_rep)
    {
      header = apr_psprintf(b->pool, REP_DELTA " %ld %" APR_OFF_T_FMT " %"
                            SVN_FILESIZE_T_FMT "\n",
                            base_rep->revision, base_rep->offset,
                            base_rep->size);
    }
  else
    {
      header = REP_DELTA "\n";
    }
  SVN_ERR(svn_io_file_write_full(file, header, strlen(header), NULL,
                                 b->pool));

  /* Now determine the offset of the actual svndiff data. */
  SVN_ERR(get_file_offset(&b->delta_start, file, b->pool));

  /* Prepare to write the svndiff data. */
  if (ffd->format >= SVN_FS_FS__MIN_SVNDIFF1_FORMAT)
    svn_txdelta_to_svndiff2(&wh, &whb, b->rep_stream, 1, pool);
  else
    svn_txdelta_to_svndiff2(&wh, &whb, b->rep_stream, 0, pool);

  b->delta_stream = svn_txdelta_target_push(wh, whb, source, b->pool);
      
  *wb_p = b;

  return SVN_NO_ERROR;
}

/* Close handler for the representation write stream.  BATON is a
   rep_write_baton.  Writes out a new node-rev that correctly
   references the representation we just finished writing. */
static svn_error_t *
rep_write_contents_close(void *baton)
{
  struct rep_write_baton *b = baton;
  representation_t *rep;
  apr_off_t offset;

  rep = apr_pcalloc(b->parent_pool, sizeof(*rep));
  rep->offset = b->rep_offset;

  /* Close our delta stream so the last bits of svndiff are written
     out. */
  if (b->delta_stream)
    SVN_ERR(svn_stream_close(b->delta_stream));

  /* Determine the length of the svndiff data. */
  SVN_ERR(get_file_offset(&offset, b->file, b->pool));
  rep->size = offset - b->delta_start;

  /* Fill in the rest of the representation field. */
  rep->expanded_size = b->rep_size;
  rep->txn_id = svn_fs_fs__id_txn_id(b->noderev->id);
  rep->revision = SVN_INVALID_REVNUM;

  /* Finalize the MD5 checksum. */
  apr_md5_final(rep->checksum, &b->md5_context);

  /* Write out our cosmetic end marker. */
  SVN_ERR(svn_stream_printf(b->rep_stream, b->pool, "ENDREP\n"));
  
  b->noderev->data_rep = rep;

  /* Write out the new node-rev information. */
  SVN_ERR(svn_fs_fs__put_node_revision(b->fs, b->noderev->id, b->noderev,
                                       b->pool));

  SVN_ERR(svn_io_file_close(b->file, b->pool));
  SVN_ERR(unlock_proto_rev(b->fs, rep->txn_id, b->lockcookie, b->pool));
  svn_pool_destroy(b->pool);

  return SVN_NO_ERROR;
}

/* Store a writable stream in *CONTENTS_P that will receive all data
   written and store it as the file data representation referenced by
   NODEREV in filesystem FS.  Perform temporary allocations in
   POOL.  Only appropriate for file data, not props or directory
   contents. */
static svn_error_t *
set_representation(svn_stream_t **contents_p,
                   svn_fs_t *fs,
                   node_revision_t *noderev,
                   apr_pool_t *pool)
{
  struct rep_write_baton *wb;

  if (! svn_fs_fs__id_txn_id(noderev->id))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Attempted to write to non-transaction"));

  SVN_ERR(rep_write_get_baton(&wb, fs, noderev, pool));

  *contents_p = svn_stream_create(wb, pool);
  svn_stream_set_write(*contents_p, rep_write_contents);
  svn_stream_set_close(*contents_p, rep_write_contents_close);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_contents(svn_stream_t **stream,
                        svn_fs_t *fs,
                        node_revision_t *noderev,
                        apr_pool_t *pool)
{
  if (noderev->kind != svn_node_file)
    return svn_error_create(SVN_ERR_FS_NOT_FILE, NULL,
                            _("Can't set text contents of a directory"));

  return set_representation(stream, fs, noderev, pool);
}

svn_error_t *
svn_fs_fs__create_successor(const svn_fs_id_t **new_id_p,
                            svn_fs_t *fs,
                            const svn_fs_id_t *old_idp,
                            node_revision_t *new_noderev,
                            const char *copy_id,
                            const char *txn_id,
                            apr_pool_t *pool)
{
  const svn_fs_id_t *id;

  if (! copy_id)
    copy_id = svn_fs_fs__id_copy_id(old_idp);
  id = svn_fs_fs__id_txn_create(svn_fs_fs__id_node_id(old_idp), copy_id,
                                txn_id, pool);

  new_noderev->id = id;

  if (! new_noderev->copyroot_path)
    {
      new_noderev->copyroot_path = apr_pstrdup(pool,
                                               new_noderev->created_path);
      new_noderev->copyroot_rev = svn_fs_fs__id_rev(new_noderev->id);
    }

  SVN_ERR(svn_fs_fs__put_node_revision(fs, new_noderev->id, new_noderev,
                                       pool));

  *new_id_p = id;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_proplist(svn_fs_t *fs,
                        node_revision_t *noderev,
                        apr_hash_t *proplist,
                        apr_pool_t *pool)
{
  const char *filename = path_txn_node_props(fs, noderev->id, pool);
  apr_file_t *file;
  svn_stream_t *out;

  /* Dump the property list to the mutable property file. */
  SVN_ERR(svn_io_file_open(&file, filename,
                           APR_WRITE | APR_CREATE | APR_TRUNCATE
                           | APR_BUFFERED, APR_OS_DEFAULT, pool));
  out = svn_stream_from_aprfile(file, pool);
  SVN_ERR(svn_hash_write2(proplist, out, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  /* Mark the node-rev's prop rep as mutable, if not already done. */
  if (!noderev->prop_rep || !noderev->prop_rep->txn_id)
    {
      noderev->prop_rep = apr_pcalloc(pool, sizeof(*noderev->prop_rep));
      noderev->prop_rep->txn_id = svn_fs_fs__id_txn_id(noderev->id);
      SVN_ERR(svn_fs_fs__put_node_revision(fs, noderev->id, noderev, pool));
    }
  
  return SVN_NO_ERROR;
}

/* Read the 'current' file for filesystem FS and store the next
   available node id in *NODE_ID, and the next available copy id in
   *COPY_ID.  Allocations are performed from POOL. */
static svn_error_t *
get_next_revision_ids(const char **node_id,
                      const char **copy_id,
                      svn_fs_t *fs,
                      apr_pool_t *pool)
{
  apr_file_t *revision_file;
  char buf[80];
  apr_size_t len;
  char *str, *last_str;

  SVN_ERR(svn_io_file_open(&revision_file, path_current(fs, pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT, pool));

  len = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(revision_file, buf, &len, pool));

  str = apr_strtok(buf, " ", &last_str);
  if (! str)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Corrupt current file"));

  str = apr_strtok(NULL, " ", &last_str);
  if (! str)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Corrupt current file"));

  *node_id = apr_pstrdup(pool, str);

  str = apr_strtok(NULL, " ", &last_str);
  if (! str)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Corrupt current file"));

  *copy_id = apr_pstrdup(pool, str);

  SVN_ERR(svn_io_file_close(revision_file, pool));

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
write_hash_handler(void *baton,
                   const char *data,
                   apr_size_t *len)
{
  struct write_hash_baton *whb = baton;

  apr_md5_update(&whb->md5_context, data, *len);

  SVN_ERR(svn_stream_write(whb->stream, data, len));
  whb->size += *len;

  return SVN_NO_ERROR;
}

/* Write out the hash HASH as a text representation to file FILE.  In
   the process, record the total size of the dump in *SIZE, and the
   md5 digest in CHECKSUM.  Perform temporary allocations in POOL. */
static svn_error_t *
write_hash_rep(svn_filesize_t *size,
               unsigned char checksum[APR_MD5_DIGESTSIZE],
               apr_file_t *file,
               apr_hash_t *hash,
               apr_pool_t *pool)
{
  svn_stream_t *stream;
  struct write_hash_baton *whb;

  whb = apr_pcalloc(pool, sizeof(*whb));

  whb->stream = svn_stream_from_aprfile(file, pool);
  whb->size = 0;
  apr_md5_init(&(whb->md5_context));

  stream = svn_stream_create(whb, pool);
  svn_stream_set_write(stream, write_hash_handler);

  SVN_ERR(svn_stream_printf(whb->stream, pool, "PLAIN\n"));
  
  SVN_ERR(svn_hash_write2(hash, stream, SVN_HASH_TERMINATOR, pool));

  /* Store the results. */
  apr_md5_final(checksum, &whb->md5_context);
  *size = whb->size;

  SVN_ERR(svn_stream_printf(whb->stream, pool, "ENDREP\n"));
  
  return SVN_NO_ERROR;
}

/* Copy a node-revision specified by id ID in fileystem FS from a
   transaction into the permanent rev-file FILE.  Return the offset of
   the new node-revision in *OFFSET.  If this is a directory, all
   children are copied as well.  START_NODE_ID and START_COPY_ID are
   the first available node and copy ids for this filesystem.
   Temporary allocations are from POOL. */
static svn_error_t *
write_final_rev(const svn_fs_id_t **new_id_p,
                apr_file_t *file,
                svn_revnum_t rev,
                svn_fs_t *fs,
                const svn_fs_id_t *id,
                const char *start_node_id,
                const char *start_copy_id,
                apr_pool_t *pool)
{
  node_revision_t *noderev;
  apr_off_t my_offset;
  char my_node_id[MAX_KEY_SIZE + 2];
  char my_copy_id[MAX_KEY_SIZE + 2];
  const svn_fs_id_t *new_id;
  const char *node_id, *copy_id;

  *new_id_p = NULL;
  
  /* Check to see if this is a transaction node. */
  if (! svn_fs_fs__id_txn_id(id))
    return SVN_NO_ERROR;

  SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, id, pool));

  if (noderev->kind == svn_node_dir)
    {
      apr_pool_t *subpool;
      apr_hash_t *entries, *str_entries;
      svn_fs_dirent_t *dirent;
      void *val;
      apr_hash_index_t *hi;
      
      /* This is a directory.  Write out all the children first. */
      subpool = svn_pool_create(pool);

      SVN_ERR(svn_fs_fs__rep_contents_dir(&entries, fs, noderev, pool));
      entries = svn_fs_fs__copy_dir_entries(entries, pool);

      for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
        {
          svn_pool_clear(subpool);
          apr_hash_this(hi, NULL, NULL, &val);
          dirent = val;
          SVN_ERR(write_final_rev(&new_id, file, rev, fs, dirent->id,
                                  start_node_id, start_copy_id, subpool));
          if (new_id && (svn_fs_fs__id_rev(new_id) == rev))
            dirent->id = svn_fs_fs__id_copy(new_id, pool);
        }
      svn_pool_destroy(subpool);

      if (noderev->data_rep && noderev->data_rep->txn_id)
        {
          /* Write out the contents of this directory as a text rep. */
          SVN_ERR(unparse_dir_entries(&str_entries, entries, pool));

          noderev->data_rep->txn_id = NULL;
          noderev->data_rep->revision = rev;
          SVN_ERR(get_file_offset(&noderev->data_rep->offset, file, pool));
          SVN_ERR(write_hash_rep(&noderev->data_rep->size,
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
      apr_hash_t *proplist;
      
      SVN_ERR(svn_fs_fs__get_proplist(&proplist, fs, noderev, pool));
      SVN_ERR(get_file_offset(&noderev->prop_rep->offset, file, pool));
      SVN_ERR(write_hash_rep(&noderev->prop_rep->size,
                             noderev->prop_rep->checksum, file,
                             proplist, pool));
                               
      noderev->prop_rep->txn_id = NULL;
      noderev->prop_rep->revision = rev;
    }

  
  /* Convert our temporary ID into a permanent revision one. */
  SVN_ERR(get_file_offset(&my_offset, file, pool));

  node_id = svn_fs_fs__id_node_id(noderev->id);
  if (*node_id == '_')
    svn_fs_fs__add_keys(start_node_id, node_id + 1, my_node_id);
  else
    strcpy(my_node_id, node_id);

  copy_id = svn_fs_fs__id_copy_id(noderev->id);
  if (*copy_id == '_')
    svn_fs_fs__add_keys(start_copy_id, copy_id + 1, my_copy_id);
  else
    strcpy(my_copy_id, copy_id);

  if (noderev->copyroot_rev == SVN_INVALID_REVNUM)
    noderev->copyroot_rev = rev;

  new_id = svn_fs_fs__id_rev_create(my_node_id, my_copy_id, rev, my_offset,
                                    pool);

  noderev->id = new_id;

  /* Write out our new node-revision. */
  SVN_ERR(write_noderev_txn(file, noderev, pool));

  SVN_ERR(svn_fs_fs__put_node_revision(fs, id, noderev, pool));

  /* Return our ID that references the revision file. */
  *new_id_p = noderev->id;

  return SVN_NO_ERROR;
}

/* Write the changed path info from transaction TXN_ID in filesystem
   FS to the permanent rev-file FILE.  *OFFSET_P is set the to offset
   in the file of the beginning of this information.  Perform
   temporary allocations in POOL. */
static svn_error_t *
write_final_changed_path_info(apr_off_t *offset_p,
                              apr_file_t *file,
                              svn_fs_t *fs,
                              const char *txn_id,
                              apr_pool_t *pool)
{
  const char *copyfrom;
  apr_hash_t *changed_paths, *copyfrom_cache = apr_hash_make(pool);
  apr_off_t offset;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  SVN_ERR(get_file_offset(&offset, file, pool));

  SVN_ERR(svn_fs_fs__txn_changes_fetch(&changed_paths, fs, txn_id,
                                       copyfrom_cache, pool));
  
  /* Iterate through the changed paths one at a time, and convert the
     temporary node-id into a permanent one for each change entry. */
  for (hi = apr_hash_first(pool, changed_paths); hi; hi = apr_hash_next(hi))
    {
      node_revision_t *noderev;
      const svn_fs_id_t *id;
      svn_fs_path_change_t *change;
      const void *key;
      void *val;
      apr_ssize_t keylen;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, &keylen, &val);
      change = val;
      
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

      /* Find the cached copyfrom information. */
      copyfrom = apr_hash_get(copyfrom_cache, key, APR_HASH_KEY_STRING);

      /* Write out the new entry into the final rev-file. */
      SVN_ERR(write_change_entry(file, key, change, copyfrom, iterpool));
    }

  svn_pool_destroy(iterpool);

  *offset_p = offset;
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__dup_perms(const char *filename,
                     const char *perms_reference,
                     apr_pool_t *pool)
{
#ifndef WIN32
  apr_status_t status;
  apr_finfo_t finfo;
  const char *filename_apr, *perms_reference_apr;
  
  SVN_ERR(svn_path_cstring_from_utf8(&filename_apr, filename, pool));
  SVN_ERR(svn_path_cstring_from_utf8(&perms_reference_apr, perms_reference,
                                     pool));

  status = apr_stat(&finfo, perms_reference_apr, APR_FINFO_PROT, pool);
  if (status)
    return svn_error_wrap_apr(status, _("Can't stat '%s'"),
                              svn_path_local_style(perms_reference, pool));
  status = apr_file_perms_set(filename_apr, finfo.protection);
  if (status)
    return svn_error_wrap_apr(status, _("Can't chmod '%s'"),
                              svn_path_local_style(filename, pool));
#endif
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__move_into_place(const char *old_filename, 
                           const char *new_filename,
                           const char *perms_reference, 
                           apr_pool_t *pool)
{
  svn_error_t *err;

  SVN_ERR(svn_fs_fs__dup_perms(old_filename, perms_reference, pool));

  /* Move the file into place. */
  err = svn_io_file_rename(old_filename, new_filename, pool);
  if (err && APR_STATUS_IS_EXDEV(err->apr_err))
    {
      apr_file_t *file;

      /* Can't rename across devices; fall back to copying. */
      svn_error_clear(err);
      SVN_ERR(svn_io_copy_file(old_filename, new_filename, TRUE, pool));

      /* Flush the target of the copy to disk. */
      SVN_ERR(svn_io_file_open(&file, new_filename, APR_READ,
                               APR_OS_DEFAULT, pool));
      SVN_ERR(svn_io_file_flush_to_disk(file, pool));
      SVN_ERR(svn_io_file_close(file, pool));
    }

#ifdef __linux__
  {
    /* Linux has the unusual feature that fsync() on a file is not
       enough to ensure that a file's directory entries have been
       flushed to disk; you have to fsync the directory as well.
       On other operating systems, we'd only be asking for trouble
       by trying to open and fsync a directory. */
    const char *dirname;
    apr_file_t *file;

    dirname = svn_path_dirname(new_filename, pool);
    SVN_ERR(svn_io_file_open(&file, dirname, APR_READ, APR_OS_DEFAULT,
                             pool));
    SVN_ERR(svn_io_file_flush_to_disk(file, pool));
    SVN_ERR(svn_io_file_close(file, pool));
  }
#endif

  return err;
}

/* Update the current file to hold the correct next node and copy_ids
   from transaction TXN_ID in filesystem FS.  The current revision is
   set to REV.  Perform temporary allocations in POOL. */
static svn_error_t *
write_final_current(svn_fs_t *fs,
                    const char *txn_id,
                    svn_revnum_t rev,
                    const char *start_node_id,
                    const char *start_copy_id,
                    apr_pool_t *pool)
{
  const char *txn_node_id, *txn_copy_id;
  char new_node_id[MAX_KEY_SIZE + 2];
  char new_copy_id[MAX_KEY_SIZE + 2];
  char *buf;
  const char *tmp_name, *name;
  apr_file_t *file;
  
  /* To find the next available ids, we add the id that used to be in
     the current file, to the next ids from the transaction file. */
  SVN_ERR(read_next_ids(&txn_node_id, &txn_copy_id, fs, txn_id, pool));

  svn_fs_fs__add_keys(start_node_id, txn_node_id, new_node_id);
  svn_fs_fs__add_keys(start_copy_id, txn_copy_id, new_copy_id);

  /* Now we can just write out this line. */
  buf = apr_psprintf(pool, "%ld %s %s\n", rev, new_node_id,
                     new_copy_id);

  name = path_current(fs, pool);
  SVN_ERR(svn_io_open_unique_file2(&file, &tmp_name, name, ".tmp",
                                   svn_io_file_del_none, pool));

  SVN_ERR(svn_io_file_write_full(file, buf, strlen(buf), NULL, pool));

  SVN_ERR(svn_io_file_flush_to_disk(file, pool));

  SVN_ERR(svn_io_file_close(file, pool));

  SVN_ERR(svn_fs_fs__move_into_place(tmp_name, name, name, pool));

  return SVN_NO_ERROR;
}

/* Get a write lock in FS, creating it in POOL. */
static svn_error_t *
get_write_lock(svn_fs_t *fs,
               apr_pool_t *pool)
{
  const char *lock_filename;
  svn_node_kind_t kind;
  
  lock_filename = path_lock(fs, pool);

  /* svn 1.1.1 and earlier deferred lock file creation to the first
     commit.  So in case the repository was created by an earlier
     version of svn, check the lock file here. */
  SVN_ERR(svn_io_check_path(lock_filename, &kind, pool));
  if ((kind == svn_node_unknown) || (kind == svn_node_none))
    SVN_ERR(svn_io_file_create(lock_filename, "", pool));
  
  SVN_ERR(svn_io_file_lock2(lock_filename, TRUE, FALSE, pool));
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__with_write_lock(svn_fs_t *fs,
                           svn_error_t *(*body)(void *baton,
                                                apr_pool_t *pool),
                           void *baton,
                           apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_error_t *err;

#if APR_HAS_THREADS
  fs_fs_data_t *ffd = fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;
  apr_status_t status;

  /* POSIX fcntl locks are per-process, so we need to serialize locks
     within the process. */
  status = apr_thread_mutex_lock(ffsd->fs_write_lock);
  if (status)
    return svn_error_wrap_apr(status, _("Can't grab FSFS repository mutex"));
#endif

  err = get_write_lock(fs, subpool);

  if (!err)
    err = body(baton, subpool);

  svn_pool_destroy(subpool);

#if APR_HAS_THREADS
  status = apr_thread_mutex_unlock(ffsd->fs_write_lock);
  if (status && !err)
    return svn_error_wrap_apr(status,
                              _("Can't ungrab FSFS repository mutex"));
#endif

  return err;
}

/* Verify that the user registed with FS has all the locks necessary to
   permit all the changes associate with TXN_NAME.
   The FS write lock is assumed to be held by the caller. */
static svn_error_t *
verify_locks(svn_fs_t *fs,
             const char *txn_name,
             apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *changes;
  apr_hash_index_t *hi;
  apr_array_header_t *changed_paths;
  svn_stringbuf_t *last_recursed = NULL;
  int i;

  /* Fetch the changes for this transaction. */
  SVN_ERR(svn_fs_fs__txn_changes_fetch(&changes, fs, txn_name, NULL, pool));

  /* Make an array of the changed paths, and sort them depth-first-ily.  */
  changed_paths = apr_array_make(pool, apr_hash_count(changes) + 1, 
                                 sizeof(const char *));
  for (hi = apr_hash_first(pool, changes); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_hash_this(hi, &key, NULL, NULL);
      APR_ARRAY_PUSH(changed_paths, const char *) = key;
    }
  qsort(changed_paths->elts, changed_paths->nelts,
        changed_paths->elt_size, svn_sort_compare_paths);
  
  /* Now, traverse the array of changed paths, verify locks.  Note
     that if we need to do a recursive verification a path, we'll skip
     over children of that path when we get to them. */
  for (i = 0; i < changed_paths->nelts; i++)
    {
      const char *path;
      svn_fs_path_change_t *change;
      svn_boolean_t recurse = TRUE;

      svn_pool_clear(subpool);
      path = APR_ARRAY_IDX(changed_paths, i, const char *);

      /* If this path has already been verified as part of a recursive
         check of one of its parents, no need to do it again.  */
      if (last_recursed 
          && svn_path_is_child(last_recursed->data, path, subpool))
        continue;

      /* Fetch the change associated with our path.  */
      change = apr_hash_get(changes, path, APR_HASH_KEY_STRING);

      /* What does it mean to succeed at lock verification for a given
         path?  For an existing file or directory getting modified
         (text, props), it means we hold the lock on the file or
         directory.  For paths being added or removed, we need to hold
         the locks for that path and any children of that path.

         WHEW!  We have no reliable way to determine the node kind
         of deleted items, but fortunately we are going to do a
         recursive check on deleted paths regardless of their kind.  */
      if (change->change_kind == svn_fs_path_change_modify)
        recurse = FALSE;
      SVN_ERR(svn_fs_fs__allow_locked_operation(path, fs, recurse, TRUE,
                                                subpool));

      /* If we just did a recursive check, remember the path we
         checked (so children can be skipped).  */
      if (recurse)
        {
          if (! last_recursed)
            last_recursed = svn_stringbuf_create(path, pool);
          else
            svn_stringbuf_set(last_recursed, path);
        }
    }
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Baton used for commit_body below. */
struct commit_baton {
  svn_revnum_t *new_rev_p;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
};

/* The work-horse for svn_fs_fs__commit, called with the FS write lock.
   This implements the svn_fs_fs__with_write_lock() 'body' callback
   type.  BATON is a 'struct commit_baton *'. */
static svn_error_t *
commit_body(void *baton, apr_pool_t *pool)
{
  struct commit_baton *cb = baton;
  const char *old_rev_filename, *rev_filename, *proto_filename;
  const char *revprop_filename, *final_revprop;
  const svn_fs_id_t *root_id, *new_root_id;
  const char *start_node_id, *start_copy_id;
  svn_revnum_t old_rev, new_rev;
  apr_file_t *proto_file;
  void *proto_file_lockcookie;
  apr_off_t changed_path_offset;
  char *buf;
  apr_hash_t *txnprops;
  svn_string_t date;

  /* Get the current youngest revision. */
  SVN_ERR(svn_fs_fs__youngest_rev(&old_rev, cb->fs, pool));

  /* Check to make sure this transaction is based off the most recent
     revision. */
  if (cb->txn->base_rev != old_rev)
    return svn_error_create(SVN_ERR_FS_TXN_OUT_OF_DATE, NULL,
                            _("Transaction out of date"));

  /* Locks may have been added (or stolen) between the calling of
     previous svn_fs.h functions and svn_fs_commit_txn(), so we need
     to re-examine every changed-path in the txn and re-verify all
     discovered locks. */
  SVN_ERR(verify_locks(cb->fs, cb->txn->id, pool));

  /* Get the next node_id and copy_id to use. */
  SVN_ERR(get_next_revision_ids(&start_node_id, &start_copy_id, cb->fs,
                                pool));

  /* We are going to be one better than this puny old revision. */
  new_rev = old_rev + 1;

  /* Get a write handle on the proto revision file. */
  SVN_ERR(get_writable_proto_rev(&proto_file, &proto_file_lockcookie,
                                 cb->fs, cb->txn->id, pool));

  /* Write out all the node-revisions and directory contents. */
  root_id = svn_fs_fs__id_txn_create("0", "0", cb->txn->id, pool);
  SVN_ERR(write_final_rev(&new_root_id, proto_file, new_rev, cb->fs, root_id,
                          start_node_id, start_copy_id, pool));

  /* Write the changed-path information. */
  SVN_ERR(write_final_changed_path_info(&changed_path_offset, proto_file,
                                        cb->fs, cb->txn->id, pool));

  /* Write the final line. */
  buf = apr_psprintf(pool, "\n%" APR_OFF_T_FMT " %" APR_OFF_T_FMT "\n",
                     svn_fs_fs__id_offset(new_root_id),
                     changed_path_offset);
  SVN_ERR(svn_io_file_write_full(proto_file, buf, strlen(buf), NULL,
                                 pool));

  SVN_ERR(svn_io_file_flush_to_disk(proto_file, pool));
  
  SVN_ERR(svn_io_file_close(proto_file, pool));
  /* We don't unlock the prototype revision file immediately to avoid a
     race with another caller writing to the prototype revision file
     before we commit it. */

  /* Remove any temporary txn props representing 'flags'. */
  SVN_ERR(svn_fs_fs__txn_proplist(&txnprops, cb->txn, pool));
  if (txnprops)
    {
      if (apr_hash_get(txnprops, SVN_FS_PROP_TXN_CHECK_OOD,
                       APR_HASH_KEY_STRING))
        SVN_ERR(svn_fs_fs__change_txn_prop 
                (cb->txn, SVN_FS_PROP_TXN_CHECK_OOD,
                 NULL, pool));
      
      if (apr_hash_get(txnprops, SVN_FS_PROP_TXN_CHECK_LOCKS,
                       APR_HASH_KEY_STRING))
        SVN_ERR(svn_fs_fs__change_txn_prop 
                (cb->txn, SVN_FS_PROP_TXN_CHECK_LOCKS,
                 NULL, pool));
    }

  /* Move the finished rev file into place. */
  old_rev_filename = svn_fs_fs__path_rev(cb->fs, old_rev, pool);
  rev_filename = svn_fs_fs__path_rev(cb->fs, new_rev, pool);
  proto_filename = path_txn_proto_rev(cb->fs, cb->txn->id, pool);
  SVN_ERR(svn_fs_fs__move_into_place(proto_filename, rev_filename, 
                                     old_rev_filename, pool));

  /* Now that we've moved the prototype revision file out of the way,
     we can unlock it (since further attempts to write to the file
     will fail as it no longer exists).  We must do this so that we can
     remove the transaction directory later. */
  SVN_ERR(unlock_proto_rev(cb->fs, cb->txn->id, proto_file_lockcookie, pool));

  /* Update commit time to ensure that svn:date revprops remain ordered. */
  date.data = svn_time_to_cstring(apr_time_now(), pool);
  date.len = strlen(date.data);

  SVN_ERR(svn_fs_fs__change_txn_prop(cb->txn, SVN_PROP_REVISION_DATE,
                                     &date, pool));

  /* Move the revprops file into place. */
  revprop_filename = path_txn_props(cb->fs, cb->txn->id, pool);
  final_revprop = path_revprops(cb->fs, new_rev, pool);
  SVN_ERR(svn_fs_fs__move_into_place(revprop_filename, final_revprop, 
                                     old_rev_filename, pool));
  
  /* Update the 'current' file. */
  SVN_ERR(write_final_current(cb->fs, cb->txn->id, new_rev, start_node_id,
                              start_copy_id, pool));

  /* Remove this transaction directory. */
  SVN_ERR(svn_fs_fs__purge_txn(cb->fs, cb->txn->id, pool));
  
  *cb->new_rev_p = new_rev;
  
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__commit(svn_revnum_t *new_rev_p,
                  svn_fs_t *fs,
                  svn_fs_txn_t *txn,
                  apr_pool_t *pool)
{
  struct commit_baton cb;

  cb.new_rev_p = new_rev_p;
  cb.fs = fs;
  cb.txn = txn;
  return svn_fs_fs__with_write_lock(fs, commit_body, &cb, pool);
}

svn_error_t *
svn_fs_fs__reserve_copy_id(const char **copy_id_p,
                           svn_fs_t *fs,
                           const char *txn_id,
                           apr_pool_t *pool)
{
  const char *cur_node_id, *cur_copy_id;
  char *copy_id;
  apr_size_t len;

  /* First read in the current next-ids file. */
  SVN_ERR(read_next_ids(&cur_node_id, &cur_copy_id, fs, txn_id, pool));

  copy_id = apr_pcalloc(pool, strlen(cur_copy_id) + 2);

  len = strlen(cur_copy_id);
  svn_fs_fs__next_key(cur_copy_id, &len, copy_id);

  SVN_ERR(write_next_ids(fs, txn_id, cur_node_id, copy_id, pool));

  *copy_id_p = apr_pstrcat(pool, "_", cur_copy_id, NULL);

  return SVN_NO_ERROR;
}

/* Write out the zeroth revision for filesystem FS. */
static svn_error_t *
write_revision_zero(svn_fs_t *fs)
{
  apr_hash_t *proplist;
  svn_string_t date;

  /* Write out a rev file for revision 0. */
  SVN_ERR(svn_io_file_create(svn_fs_fs__path_rev(fs, 0, fs->pool),
                             "PLAIN\nEND\nENDREP\n"
                             "id: 0.0.r0/17\n"
                             "type: dir\n"
                             "count: 0\n"
                             "text: 0 0 4 4 "
                             "2d2977d1c96f487abe4a1e202dd03b4e\n"
                             "cpath: /\n"
                             "\n\n17 107\n", fs->pool));

  /* Set a date on revision 0. */
  date.data = svn_time_to_cstring(apr_time_now(), fs->pool);
  date.len = strlen(date.data);
  proplist = apr_hash_make(fs->pool);
  apr_hash_set(proplist, SVN_PROP_REVISION_DATE, APR_HASH_KEY_STRING, &date);
  return svn_fs_fs__set_revision_proplist(fs, 0, proplist, fs->pool);
}

svn_error_t *
svn_fs_fs__create(svn_fs_t *fs,
                  const char *path,
                  apr_pool_t *pool)
{
  int format = SVN_FS_FS__FORMAT_NUMBER;
  
  fs->path = apr_pstrdup(pool, path);

  SVN_ERR(svn_io_make_dir_recursively(svn_path_join(path, PATH_REVS_DIR,
                                                    pool),
                                      pool));
  SVN_ERR(svn_io_make_dir_recursively(svn_path_join(path, PATH_REVPROPS_DIR,
                                                    pool),
                                      pool));
  SVN_ERR(svn_io_make_dir_recursively(svn_path_join(path, PATH_TXNS_DIR,
                                                    pool),
                                      pool));

  SVN_ERR(svn_io_file_create(path_current(fs, pool), "0 1 1\n", pool));
  SVN_ERR(svn_io_file_create(path_lock(fs, pool), "", pool));
  SVN_ERR(svn_fs_fs__set_uuid(fs, svn_uuid_generate(pool), pool));

  /* See if we had an explicitly requested pre 1.4 compatible.  */
  if (fs->config && apr_hash_get(fs->config, SVN_FS_CONFIG_PRE_1_4_COMPATIBLE,
                                 APR_HASH_KEY_STRING))
    format = 1;
  
  SVN_ERR(write_revision_zero(fs));

  /* This filesystem is ready.  Stamp it with a format number. */
  SVN_ERR(svn_io_write_version_file
          (path_format(fs, pool), format, pool));
  ((fs_fs_data_t *) fs->fsap_data)->format = format;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_uuid(svn_fs_t *fs,
                    const char **uuid_p,
                    apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  *uuid_p = apr_pstrdup(pool, ffd->uuid);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_uuid(svn_fs_t *fs,
                    const char *uuid,
                    apr_pool_t *pool)
{
  apr_file_t *uuid_file;
  const char *tmp_path;
  const char *uuid_path = path_uuid(fs, pool);
  fs_fs_data_t *ffd = fs->fsap_data;

  SVN_ERR(svn_io_open_unique_file2(&uuid_file, &tmp_path, uuid_path,
                                    ".tmp", svn_io_file_del_none, pool));

  SVN_ERR(svn_io_file_write_full(uuid_file, uuid, strlen(uuid), NULL,
                                 pool));
  SVN_ERR(svn_io_file_write_full(uuid_file, "\n", 1, NULL, pool));

  SVN_ERR(svn_io_file_close(uuid_file, pool));
  
  /* We use the permissions of the 'current' file, because the 'uuid'
     file does not exist during repository creation. */
  SVN_ERR(svn_fs_fs__move_into_place(tmp_path, uuid_path,
                                     path_current(fs, pool), pool));

  ffd->uuid = apr_pstrdup(fs->pool, uuid);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__list_transactions(apr_array_header_t **names_p,
                             svn_fs_t *fs,
                             apr_pool_t *pool)
{
  const char *txn_dir;
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  apr_array_header_t *names;
  apr_size_t ext_len = strlen(PATH_EXT_TXN);

  names = apr_array_make(pool, 1, sizeof(const char *));
  
  /* Get the transactions directory. */
  txn_dir = svn_path_join(fs->path, PATH_TXNS_DIR, pool);

  /* Now find a listing of this directory. */
  SVN_ERR(svn_io_get_dirents2(&dirents, txn_dir, pool));

  /* Loop through all the entries and return anything that ends with '.txn'. */
  for (hi = apr_hash_first(pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      const char *name, *id;
      apr_ssize_t klen;

      apr_hash_this(hi, &key, &klen, NULL);
      name = key;

      /* The name must end with ".txn" to be considered a transaction. */
      if ((apr_size_t) klen <= ext_len
          || (strcmp(name + klen - ext_len, PATH_EXT_TXN)) != 0)
        continue;

      /* Truncate the ".txn" extension and store the ID. */
      id = apr_pstrndup(pool, name, strlen(name) - ext_len);
      APR_ARRAY_PUSH(names, const char *) = id;
    }

  *names_p = names;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__open_txn(svn_fs_txn_t **txn_p,
                    svn_fs_t *fs,
                    const char *name,
                    apr_pool_t *pool)
{
  svn_fs_txn_t *txn;
  svn_node_kind_t kind;
  transaction_t *local_txn;

  /* First check to see if the directory exists. */
  SVN_ERR(svn_io_check_path(path_txn_dir(fs, name, pool), &kind, pool));

  /* Did we find it? */
  if (kind != svn_node_dir)
    return svn_error_create(SVN_ERR_FS_NO_SUCH_TRANSACTION, NULL,
                            _("No such transaction"));
      
  txn = apr_pcalloc(pool, sizeof(*txn));

  /* Read in the root node of this transaction. */
  txn->id = apr_pstrdup(pool, name);
  txn->fs = fs;

  SVN_ERR(svn_fs_fs__get_txn(&local_txn, fs, name, pool));

  txn->base_rev = svn_fs_fs__id_rev(local_txn->base_id);

  txn->vtable = &txn_vtable;
  txn->fsap_data = NULL;
  *txn_p = txn;

  return SVN_NO_ERROR;
}
  
svn_error_t *
svn_fs_fs__txn_proplist(apr_hash_t **table_p,
                        svn_fs_txn_t *txn,
                        apr_pool_t *pool)
{
  apr_hash_t *proplist = apr_hash_make(pool);
  SVN_ERR(get_txn_proplist(proplist, txn->fs, txn->id, pool));
  *table_p = proplist;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__delete_node_revision(svn_fs_t *fs,
                                const svn_fs_id_t *id,
                                apr_pool_t *pool)
{
  node_revision_t *noderev;

  SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, id, pool));

  /* Delete any mutable property representation. */
  if (noderev->prop_rep && noderev->prop_rep->txn_id)
    SVN_ERR(svn_io_remove_file(path_txn_node_props(fs, id, pool), pool));

  /* Delete any mutable data representation. */
  if (noderev->data_rep && noderev->data_rep->txn_id
      && noderev->kind == svn_node_dir)
    SVN_ERR(svn_io_remove_file(path_txn_node_children(fs, id, pool), pool));

  return svn_io_remove_file(path_txn_node_rev(fs, id, pool), pool);
}



/*** Revisions ***/

svn_error_t *
svn_fs_fs__revision_prop(svn_string_t **value_p,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         const char *propname,
                         apr_pool_t *pool)
{
  apr_hash_t *table;

  SVN_ERR(svn_fs_fs__check_fs(fs));
  SVN_ERR(svn_fs_fs__revision_proplist(&table, fs, rev, pool));

  *value_p = apr_hash_get(table, propname, APR_HASH_KEY_STRING);

  return SVN_NO_ERROR;
}


/* Baton used for change_rev_prop_body below. */
struct change_rev_prop_baton {
  svn_fs_t *fs;
  svn_revnum_t rev;
  const char *name;
  const svn_string_t *value;
};

/* The work-horse for svn_fs_fs__change_rev_prop, called with the FS
   write lock.  This implements the svn_fs_fs__with_write_lock()
   'body' callback type.  BATON is a 'struct change_rev_prop_baton *'. */

static svn_error_t *
change_rev_prop_body(void *baton, apr_pool_t *pool)
{
  struct change_rev_prop_baton *cb = baton;
  apr_hash_t *table;

  SVN_ERR(svn_fs_fs__revision_proplist(&table, cb->fs, cb->rev, pool));

  apr_hash_set(table, cb->name, APR_HASH_KEY_STRING, cb->value);

  SVN_ERR(svn_fs_fs__set_revision_proplist(cb->fs, cb->rev, table, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__change_rev_prop(svn_fs_t *fs,
                           svn_revnum_t rev,
                           const char *name,
                           const svn_string_t *value,
                           apr_pool_t *pool)
{
  struct change_rev_prop_baton cb;

  SVN_ERR(svn_fs_fs__check_fs(fs));

  cb.fs = fs;
  cb.rev = rev;
  cb.name = name;
  cb.value = value;

  return svn_fs_fs__with_write_lock(fs, change_rev_prop_body, &cb, pool);
}



/*** Transactions ***/

svn_error_t *
svn_fs_fs__get_txn_ids(const svn_fs_id_t **root_id_p,
                       const svn_fs_id_t **base_root_id_p,
                       svn_fs_t *fs,
                       const char *txn_name,
                       apr_pool_t *pool)
{
  transaction_t *txn;
  SVN_ERR(svn_fs_fs__get_txn(&txn, fs, txn_name, pool));
  *root_id_p = txn->root_id;
  *base_root_id_p = txn->base_id;
  return SVN_NO_ERROR;
}


/* Generic transaction operations.  */

svn_error_t *
svn_fs_fs__txn_prop(svn_string_t **value_p,
                    svn_fs_txn_t *txn,
                    const char *propname,
                    apr_pool_t *pool)
{
  apr_hash_t *table;
  svn_fs_t *fs = txn->fs;

  SVN_ERR(svn_fs_fs__check_fs(fs));

  SVN_ERR(svn_fs_fs__txn_proplist(&table, txn, pool));

  /* And then the prop from that list (if there was a list). */
  *value_p = NULL;
  if (table)
    *value_p = apr_hash_get(table, propname, APR_HASH_KEY_STRING);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__begin_txn(svn_fs_txn_t **txn_p,
                     svn_fs_t *fs,
                     svn_revnum_t rev,
                     apr_uint32_t flags,
                     apr_pool_t *pool)
{
  svn_string_t date;

  SVN_ERR(svn_fs_fs__check_fs(fs));

  SVN_ERR(svn_fs_fs__create_txn(txn_p, fs, rev, pool));

  /* Put a datestamp on the newly created txn, so we always know
     exactly how old it is.  (This will help sysadmins identify
     long-abandoned txns that may need to be manually removed.)  When
     a txn is promoted to a revision, this property will be
     automatically overwritten with a revision datestamp. */
  date.data = svn_time_to_cstring(apr_time_now(), pool);
  date.len = strlen(date.data);
  SVN_ERR(svn_fs_fs__change_txn_prop(*txn_p, SVN_PROP_REVISION_DATE, 
                                     &date, pool));
  
  /* Set temporary txn props that represent the requested 'flags'
     behaviors. */
  if (flags & SVN_FS_TXN_CHECK_OOD)
    SVN_ERR(svn_fs_fs__change_txn_prop 
            (*txn_p, SVN_FS_PROP_TXN_CHECK_OOD,
             svn_string_create("true", pool), pool));
  
  if (flags & SVN_FS_TXN_CHECK_LOCKS)
    SVN_ERR(svn_fs_fs__change_txn_prop 
            (*txn_p, SVN_FS_PROP_TXN_CHECK_LOCKS,
             svn_string_create("true", pool), pool));
             
  return SVN_NO_ERROR;
}
