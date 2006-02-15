/*
 * reporter.c : `reporter' vtable routines for updates.
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

#include "svn_path.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_pools.h"
#include "svn_md5.h"
#include "svn_props.h"
#include "repos.h"
#include "svn_private_config.h"

#define NUM_CACHED_SOURCE_ROOTS 4

/* Theory of operation: we write report operations out to a temporary
   file as we receive them.  When the report is finished, we read the
   operations back out again, using them to guide the progression of
   the delta between the source and target revs.

   Temporary file format: we use a simple ad-hoc format to store the
   report operations.  Each report operation is the concatention of
   the following ("+/-" indicates the single character '+' or '-';
   <length> and <revnum> are written out as decimal strings):

     +/-                      '-' marks the end of the report
     If previous is +:
       <length>:<bytes>       Length-counted path string
       +/-                    '+' indicates the presence of link_path
       If previous is +:
         <length>:<bytes>     Length-counted link_path string
       +/-                    '+' indicates presence of revnum
       If previous is +:
         <revnum>:            Revnum of set_path or link_path
       +/-                    '+' indicates start_empty field set
       +/-                    '+' indicates presence of lock_token field.
       If previous is +:
         <length>:<bytes>     Length-counted lock_token string

   Terminology: for brevity, this file frequently uses the prefixes
   "s_" for source, "t_" for target, and "e_" for editor.  Also, to
   avoid overloading the word "target", we talk about the source
   "anchor and operand", rather than the usual "anchor and target". */

/* Describes the state of a working copy subtree, as given by a
   report.  Because we keep a lookahead pathinfo, we need to allocate
   each one of these things in a subpool of the report baton and free
   it when done. */
typedef struct path_info_t
{
  const char *path;            /* path, munged to be anchor-relative */
  const char *link_path;       /* NULL for set_path or delete_path */
  svn_revnum_t rev;            /* SVN_INVALID_REVNUM for delete_path */
  svn_boolean_t start_empty;   /* Meaningless for delete_path */
  const char *lock_token;      /* NULL if no token */
  apr_pool_t *pool;            /* Container pool */
} path_info_t;

/* A structure used by the routines within the `reporter' vtable,
   driven by the client as it describes its working copy revisions. */
typedef struct report_baton_t
{
  /* Parameters remembered from svn_repos_begin_report */
  svn_repos_t *repos;
  const char *fs_base;         /* FS path corresponding to wc anchor */
  const char *s_operand;       /* Anchor-relative wc target (may be empty) */
  svn_revnum_t t_rev;          /* Revnum which the edit will bring the wc to */
  const char *t_path;          /* FS path the edit will bring the wc to */
  svn_boolean_t text_deltas;   /* Whether to report text deltas */
  svn_boolean_t recurse;
  svn_boolean_t ignore_ancestry;
  svn_boolean_t is_switch;
  const svn_delta_editor_t *editor;
  void *edit_baton; 
  svn_repos_authz_func_t authz_read_func;
  void *authz_read_baton;

  /* The temporary file in which we are stashing the report. */
  apr_file_t *tempfile;

  /* For the actual editor drive, we'll need a lookahead path info
     entry, a cache of FS roots, and a pool to store them. */
  path_info_t *lookahead;
  svn_fs_root_t *t_root;
  svn_fs_root_t *s_roots[NUM_CACHED_SOURCE_ROOTS];
  apr_pool_t *pool;
} report_baton_t;

/* The type of a function that accepts changes to an object's property
   list.  OBJECT is the object whose properties are being changed.
   NAME is the name of the property to change.  VALUE is the new value
   for the property, or zero if the property should be deleted. */
typedef svn_error_t *proplist_change_fn_t(report_baton_t *b, void *object,
                                          const char *name,
                                          const svn_string_t *value,
                                          apr_pool_t *pool);

static svn_error_t *delta_dirs(report_baton_t *b, svn_revnum_t s_rev,
                               const char *s_path, const char *t_path,
                               void *dir_baton, const char *e_path,
                               svn_boolean_t start_empty, apr_pool_t *pool);

/* --- READING PREVIOUSLY STORED REPORT INFORMATION --- */

static svn_error_t *
read_number(apr_uint64_t *num, apr_file_t *temp, apr_pool_t *pool)
{
  char c;

  *num = 0;
  while (1)
    {
      SVN_ERR(svn_io_file_getc(&c, temp, pool));
      if (c == ':')
        break;
      *num = *num * 10 + (c - '0');
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
read_string(const char **str, apr_file_t *temp, apr_pool_t *pool)
{
  apr_uint64_t len;
  char *buf;

  SVN_ERR(read_number(&len, temp, pool));
  buf = apr_palloc(pool, len + 1);
  SVN_ERR(svn_io_file_read_full(temp, buf, len, NULL, pool));
  buf[len] = 0;
  *str = buf;
  return SVN_NO_ERROR;
}

static svn_error_t *
read_rev(svn_revnum_t *rev, apr_file_t *temp, apr_pool_t *pool)
{
  char c;
  apr_uint64_t num;

  SVN_ERR(svn_io_file_getc(&c, temp, pool));
  if (c == '+')
    {
      SVN_ERR(read_number(&num, temp, pool));
      *rev = num;
    }
  else
    *rev = SVN_INVALID_REVNUM;
  return SVN_NO_ERROR;
}

/* Read a report operation *PI out of TEMP.  Set *PI to NULL if we
   have reached the end of the report. */
static svn_error_t *
read_path_info(path_info_t **pi, apr_file_t *temp, apr_pool_t *pool)
{
  char c;

  SVN_ERR(svn_io_file_getc(&c, temp, pool));
  if (c == '-')
    {
      *pi = NULL;
      return SVN_NO_ERROR;
    }

  *pi = apr_palloc(pool, sizeof(**pi));
  SVN_ERR(read_string(&(*pi)->path, temp, pool));
  SVN_ERR(svn_io_file_getc(&c, temp, pool));
  if (c == '+')
    SVN_ERR(read_string(&(*pi)->link_path, temp, pool));
  else
    (*pi)->link_path = NULL;
  SVN_ERR(read_rev(&(*pi)->rev, temp, pool));
  SVN_ERR(svn_io_file_getc(&c, temp, pool));
  (*pi)->start_empty = (c == '+');
  SVN_ERR(svn_io_file_getc(&c, temp, pool));
  if (c == '+')
    SVN_ERR(read_string(&(*pi)->lock_token, temp, pool));
  else
    (*pi)->lock_token = NULL;
  (*pi)->pool = pool;
  return SVN_NO_ERROR;
}

/* Return true if PI's path is a child of PREFIX (which has length PLEN). */
static svn_boolean_t 
relevant(path_info_t *pi, const char *prefix, apr_size_t plen)
{
  return (pi && strncmp(pi->path, prefix, plen) == 0 &&
          (!*prefix || pi->path[plen] == '/'));
}

/* Fetch the next pathinfo from B->tempfile for a descendent of
   PREFIX.  If the next pathinfo is for an immediate child of PREFIX,
   set *ENTRY to the path component of the report information and
   *INFO to the path information for that entry.  If the next pathinfo
   is for a grandchild or other more remote descendent of PREFIX, set
   *ENTRY to the immediate child corresponding to that descendent and
   set *INFO to NULL.  If the next pathinfo is not for a descendent of
   PREFIX, or if we reach the end of the report, set both *ENTRY and
   *INFO to NULL.

   At all times, B->lookahead is presumed to be the next pathinfo not
   yet returned as an immediate child, or NULL if we have reached the
   end of the report.  Because we use a lookahead element, we can't
   rely on the usual nested pool lifetimes, so allocate each pathinfo
   in a subpool of the report baton's pool.  The caller should delete
   (*INFO)->pool when it is done with the information. */
static svn_error_t *
fetch_path_info(report_baton_t *b, const char **entry, path_info_t **info,
                const char *prefix, apr_pool_t *pool)
{
  apr_size_t plen = strlen(prefix);
  const char *relpath, *sep;
  apr_pool_t *subpool;

  if (!relevant(b->lookahead, prefix, plen))
    {
      /* No more entries relevant to prefix. */
      *entry = NULL;
      *info = NULL;
    }
  else
    {
      /* Take a look at the prefix-relative part of the path. */
      relpath = b->lookahead->path + (*prefix ? plen + 1 : 0);
      sep = strchr(relpath, '/');
      if (sep)
        {
          /* Return the immediate child part; do not advance. */
          *entry = apr_pstrmemdup(pool, relpath, sep - relpath);
          *info = NULL;
        }
      else
        {
          /* This is an immediate child; return it and advance. */
          *entry = relpath;
          *info = b->lookahead;
          subpool = svn_pool_create(b->pool);
          SVN_ERR(read_path_info(&b->lookahead, b->tempfile, subpool));
        }
    }
  return SVN_NO_ERROR;
}

/* Skip all path info entries relevant to *PREFIX.  Call this when the
   editor drive skips a directory. */
static svn_error_t *
skip_path_info(report_baton_t *b, const char *prefix)
{
  apr_size_t plen = strlen(prefix);
  apr_pool_t *subpool;

  while (relevant(b->lookahead, prefix, plen))
    {
      svn_pool_destroy(b->lookahead->pool);
      subpool = svn_pool_create(b->pool);
      SVN_ERR(read_path_info(&b->lookahead, b->tempfile, subpool));
    }
  return SVN_NO_ERROR;
}

/* Return true if there is at least one path info entry relevant to *PREFIX. */
static svn_boolean_t
any_path_info(report_baton_t *b, const char *prefix)
{
  return relevant(b->lookahead, prefix, strlen(prefix));
}

/* --- DRIVING THE EDITOR ONCE THE REPORT IS FINISHED --- */

/* While driving the editor, the target root will remain constant, but
   we may have to jump around between source roots depending on the
   state of the working copy.  If we were to open a root each time we
   revisit a rev, we would get no benefit from node-id caching; on the
   other hand, if we hold open all the roots we ever visit, we'll use
   an unbounded amount of memory.  As a compromise, we maintain a
   fixed-size LRU cache of source roots.  get_source_root retrieves a
   root from the cache, using POOL to allocate the new root if
   necessary.  Be careful not to hold onto the root for too long,
   particularly after recursing, since another call to get_source_root
   can close it. */
static svn_error_t *
get_source_root(report_baton_t *b, svn_fs_root_t **s_root, svn_revnum_t rev)
{
  int i;
  svn_fs_root_t *root, *prev = NULL;

  /* Look for the desired root in the cache, sliding all the unmatched
     entries backwards a slot to make room for the right one. */
  for (i = 0; i < NUM_CACHED_SOURCE_ROOTS; i++)
    {
      root = b->s_roots[i];
      b->s_roots[i] = prev;
      if (root && svn_fs_revision_root_revision(root) == rev)
        break;
      prev = root;
    }

  /* If we didn't find it, throw out the oldest root and open a new one. */
  if (i == NUM_CACHED_SOURCE_ROOTS)
    {
      if (prev)
        svn_fs_close_root(prev);
      SVN_ERR(svn_fs_revision_root(&root, b->repos->fs, rev, b->pool));
    }

  /* Assign the desired root to the first cache slot and hand it back. */
  b->s_roots[0] = root;
  *s_root = root;
  return SVN_NO_ERROR;
}

/* Call the directory property-setting function of B->editor to set
   the property NAME to VALUE on DIR_BATON. */
static svn_error_t *
change_dir_prop(report_baton_t *b, void *dir_baton, const char *name, 
                const svn_string_t *value, apr_pool_t *pool)
{
  return b->editor->change_dir_prop(dir_baton, name, value, pool);
}

/* Call the file property-setting function of B->editor to set the
   property NAME to VALUE on FILE_BATON. */
static svn_error_t *
change_file_prop(report_baton_t *b, void *file_baton, const char *name, 
                 const svn_string_t *value, apr_pool_t *pool)
{
  return b->editor->change_file_prop(file_baton, name, value, pool);
}

/* Generate the appropriate property editing calls to turn the
   properties of S_REV/S_PATH into those of B->t_root/T_PATH.  If
   S_PATH is NULL, this is an add, so assume the target starts with no
   properties.  Pass OBJECT on to the editor function wrapper
   CHANGE_FN. */
static svn_error_t *
delta_proplists(report_baton_t *b, svn_revnum_t s_rev, const char *s_path,
                const char *t_path, const char *lock_token,
                proplist_change_fn_t *change_fn,
                void *object, apr_pool_t *pool)
{
  svn_fs_root_t *s_root;
  apr_hash_t *s_props, *t_props, *r_props;
  apr_array_header_t *prop_diffs;
  int i;
  svn_revnum_t crev;
  const char *uuid;
  svn_string_t *cr_str, *cdate, *last_author;
  svn_boolean_t changed;
  const svn_prop_t *pc;
  svn_lock_t *lock;

  /* Fetch the created-rev and send entry props. */
  SVN_ERR(svn_fs_node_created_rev(&crev, b->t_root, t_path, pool));
  if (SVN_IS_VALID_REVNUM(crev))
    {
      /* Transmit the committed-rev. */
      cr_str = svn_string_createf(pool, "%ld", crev);
      SVN_ERR(change_fn(b, object,
                        SVN_PROP_ENTRY_COMMITTED_REV, cr_str, pool));

      SVN_ERR(svn_fs_revision_proplist(&r_props, b->repos->fs, crev, pool));

      /* Transmit the committed-date. */
      cdate = apr_hash_get(r_props, SVN_PROP_REVISION_DATE,
                           APR_HASH_KEY_STRING);
      if (cdate || s_path)
        SVN_ERR(change_fn(b, object, SVN_PROP_ENTRY_COMMITTED_DATE, 
                          cdate, pool));

      /* Transmit the last-author. */
      last_author = apr_hash_get(r_props, SVN_PROP_REVISION_AUTHOR,
                                 APR_HASH_KEY_STRING);
      if (last_author || s_path)
        SVN_ERR(change_fn(b, object, SVN_PROP_ENTRY_LAST_AUTHOR,
                          last_author, pool));

      /* Transmit the UUID. */
      SVN_ERR(svn_fs_get_uuid(b->repos->fs, &uuid, pool));
      if (uuid || s_path)
        SVN_ERR(change_fn(b, object, SVN_PROP_ENTRY_UUID,
                          svn_string_create(uuid, pool), pool));
    }

  /* Update lock properties. */
  if (lock_token)
    {
      SVN_ERR(svn_fs_get_lock(&lock, b->repos->fs, t_path, pool));

      /* Delete a defunct lock. */
      if (! lock || strcmp(lock_token, lock->token) != 0)
        SVN_ERR(change_fn(b, object, SVN_PROP_ENTRY_LOCK_TOKEN,
                          NULL, pool));
    }

  if (s_path)
    {
      SVN_ERR(get_source_root(b, &s_root, s_rev));

      /* Is this deltification worth our time? */
      SVN_ERR(svn_fs_props_changed(&changed, b->t_root, t_path, s_root,
                                   s_path, pool));
      if (! changed)
        return SVN_NO_ERROR;

      /* If so, go ahead and get the source path's properties. */
      SVN_ERR(svn_fs_node_proplist(&s_props, s_root, s_path, pool));
    }
  else
    s_props = apr_hash_make(pool);

  /* Get the target path's properties */
  SVN_ERR(svn_fs_node_proplist(&t_props, b->t_root, t_path, pool));

  /* Now transmit the differences. */
  SVN_ERR(svn_prop_diffs(&prop_diffs, t_props, s_props, pool));
  for (i = 0; i < prop_diffs->nelts; i++)
    {
      pc = &APR_ARRAY_IDX(prop_diffs, i, svn_prop_t);
      SVN_ERR(change_fn(b, object, pc->name, pc->value, pool));
    }

  return SVN_NO_ERROR;
}

/* Make the appropriate edits on FILE_BATON to change its contents and
   properties from those in S_REV/S_PATH to those in B->t_root/T_PATH,
   possibly using LOCK_TOKEN to determine if the client's lock on the file
   is defunct. */
static svn_error_t *
delta_files(report_baton_t *b, void *file_baton, svn_revnum_t s_rev,
            const char *s_path, const char *t_path, const char *lock_token,
            apr_pool_t *pool)
{
  svn_boolean_t changed;
  svn_fs_root_t *s_root = NULL;
  svn_txdelta_stream_t *dstream = NULL;
  unsigned char s_digest[APR_MD5_DIGESTSIZE];
  const char *s_hex_digest = NULL;
  svn_txdelta_window_handler_t dhandler;
  void *dbaton;

  /* Compare the files' property lists.  */
  SVN_ERR(delta_proplists(b, s_rev, s_path, t_path, lock_token,
                          change_file_prop, file_baton, pool));

  if (s_path)
    {
      SVN_ERR(get_source_root(b, &s_root, s_rev));

      /* Is this delta calculation worth our time?  If we are ignoring
         ancestry, then our editor implementor isn't concerned by the
         theoretical differences between "has contents which have not
         changed with respect to" and "has the same actual contents
         as".  We'll do everything we can to avoid transmitting even
         an empty text-delta in that case.  */
      if (b->ignore_ancestry)
        SVN_ERR(svn_repos__compare_files(&changed, b->t_root, t_path,
                                         s_root, s_path, pool));
      else
        SVN_ERR(svn_fs_contents_changed(&changed, b->t_root, t_path, s_root,
                                        s_path, pool));
      if (!changed)
        return SVN_NO_ERROR;

      SVN_ERR(svn_fs_file_md5_checksum(s_digest, s_root, s_path, pool));
      s_hex_digest = svn_md5_digest_to_cstring(s_digest, pool);
    }

  /* Send the delta stream if desired, or just a NULL window if not. */
  SVN_ERR(b->editor->apply_textdelta(file_baton, s_hex_digest, pool,
                                     &dhandler, &dbaton));
  if (b->text_deltas)
    {
      SVN_ERR(svn_fs_get_file_delta_stream(&dstream, s_root, s_path,
                                           b->t_root, t_path, pool));
      return svn_txdelta_send_txstream(dstream, dhandler, dbaton, pool);
    }
  else
    return dhandler(NULL, dbaton);
}

/* Determine if the user is authorized to view B->t_root/PATH. */
static svn_error_t *
check_auth(report_baton_t *b, svn_boolean_t *allowed, const char *path,
           apr_pool_t *pool)
{
  if (b->authz_read_func)
    return b->authz_read_func(allowed, b->t_root, path,
                              b->authz_read_baton, pool);
  *allowed = TRUE;
  return SVN_NO_ERROR;
}

/* Create a dirent in *ENTRY for the given ROOT and PATH.  We use this to
   replace the source or target dirent when a report pathinfo tells us to
   change paths or revisions. */
static svn_error_t *
fake_dirent(const svn_fs_dirent_t **entry, svn_fs_root_t *root,
            const char *path, apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_fs_dirent_t *ent;

  SVN_ERR(svn_fs_check_path(&kind, root, path, pool));
  if (kind == svn_node_none)
    *entry = NULL;
  else
    {
      ent = apr_palloc(pool, sizeof(**entry));
      ent->name = svn_path_basename(path, pool);
      SVN_ERR(svn_fs_node_id(&ent->id, root, path, pool));
      ent->kind = kind;
      *entry = ent;
    }
  return SVN_NO_ERROR;
}


/* Emit a series of editing operations to transform a source entry to
   a target entry.

   S_REV and S_PATH specify the source entry.  S_ENTRY contains the
   already-looked-up information about the node-revision existing at
   that location.  S_PATH and S_ENTRY may be NULL if the entry does
   not exist in the source.  S_PATH may be non-NULL and S_ENTRY may be
   NULL if the caller expects INFO to modify the source to an existing
   location.

   B->t_root and T_PATH specify the target entry.  T_ENTRY contains
   the already-looked-up information about the node-revision existing
   at that location.  T_PATH and T_ENTRY may be NULL if the entry does
   not exist in the target.

   DIR_BATON and E_PATH contain the parameters which should be passed
   to the editor calls--DIR_BATON for the parent directory baton and
   E_PATH for the pathname.  (E_PATH is the anchor-relative working
   copy pathname, which may differ from the source and target
   pathnames if the report contains a link_path.)

   INFO contains the report information for this working copy path, or
   NULL if there is none.  This function will internally modify the
   source and target entries as appropriate based on the report
   information.

   If RECURSE is not set, avoid operating on directories.  (Normally
   RECURSE is simply taken from B->recurse, but drive() needs to force
   us to recurse into the target even if that flag is not set.) */
static svn_error_t *
update_entry(report_baton_t *b, svn_revnum_t s_rev, const char *s_path,
             const svn_fs_dirent_t *s_entry, const char *t_path,
             const svn_fs_dirent_t *t_entry, void *dir_baton,
             const char *e_path, path_info_t *info, svn_boolean_t recurse,
             apr_pool_t *pool)
{
  svn_fs_root_t *s_root;
  svn_boolean_t allowed, related;
  void *new_baton;
  unsigned char digest[APR_MD5_DIGESTSIZE];
  const char *hex_digest;
  int distance;

  /* For non-switch operations, follow link_path in the target. */
  if (info && info->link_path && !b->is_switch)
    {
      t_path = info->link_path;
      SVN_ERR(fake_dirent(&t_entry, b->t_root, t_path, pool));
    }

  if (info && !SVN_IS_VALID_REVNUM(info->rev))
    {
      /* Delete this entry in the source. */
      s_path = NULL;
      s_entry = NULL;
    }
  else if (info && s_path)
    {
      /* Follow the rev and possibly path in this entry. */
      s_path = (info->link_path) ? info->link_path : s_path;
      s_rev = info->rev;
      SVN_ERR(get_source_root(b, &s_root, s_rev));
      SVN_ERR(fake_dirent(&s_entry, s_root, s_path, pool));
    }

  /* Don't let the report carry us somewhere nonexistent. */
  if (s_path && !s_entry)
    return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                             _("Working copy path '%s' does not exist in "
                               "repository"), e_path);

  if (!recurse && ((s_entry && s_entry->kind == svn_node_dir)
                   || (t_entry && t_entry->kind == svn_node_dir)))
    return skip_path_info(b, e_path);

  /* If the source and target both exist and are of the same kind,
     then find out whether they're related.  If they're exactly the
     same, then we don't have to do anything (unless the report has
     changes to the source).  If we're ignoring ancestry, then any two
     nodes of the same type are related enough for us. */
  related = FALSE;
  if (s_entry && t_entry && s_entry->kind == t_entry->kind)
    {
      distance = svn_fs_compare_ids(s_entry->id, t_entry->id);
      if (distance == 0 && !any_path_info(b, e_path)
          && (!info || (!info->start_empty && !info->lock_token)))
        return SVN_NO_ERROR;
      else if (distance != -1 || b->ignore_ancestry)
        related = TRUE;
    }

  /* If there's a source and it's not related to the target, nuke it. */
  if (s_entry && !related)
    {
      SVN_ERR(b->editor->delete_entry(e_path, SVN_INVALID_REVNUM, dir_baton,
                                      pool));
      s_path = NULL;
    }

  /* If there's no target, we have nothing more to do. */
  if (!t_entry)
    return skip_path_info(b, e_path);

  /* Check if the user is authorized to find out about the target. */
  SVN_ERR(check_auth(b, &allowed, t_path, pool));
  if (!allowed)
    {
      if (t_entry->kind == svn_node_dir)
        SVN_ERR(b->editor->absent_directory(e_path, dir_baton, pool));
      else
        SVN_ERR(b->editor->absent_file(e_path, dir_baton, pool));
      return skip_path_info(b, e_path);
    }

  if (t_entry->kind == svn_node_dir)
    {
      if (related)
        SVN_ERR(b->editor->open_directory(e_path, dir_baton, s_rev, pool, 
                                          &new_baton));
      else
        SVN_ERR(b->editor->add_directory(e_path, dir_baton, NULL,
                                         SVN_INVALID_REVNUM, pool,
                                         &new_baton));
      SVN_ERR(delta_dirs(b, s_rev, s_path, t_path, new_baton, e_path,
                         info ? info->start_empty : FALSE, pool));
      return b->editor->close_directory(new_baton, pool);
    }
  else
    {
      if (related)
        SVN_ERR(b->editor->open_file(e_path, dir_baton, s_rev, pool,
                                     &new_baton));
      else
        SVN_ERR(b->editor->add_file(e_path, dir_baton, NULL,
                                    SVN_INVALID_REVNUM, pool, &new_baton));
      SVN_ERR(delta_files(b, new_baton, s_rev, s_path, t_path,
                          info ? info->lock_token : NULL, pool));
      SVN_ERR(svn_fs_file_md5_checksum(digest, b->t_root, t_path, pool));
      hex_digest = svn_md5_digest_to_cstring(digest, pool);
      return b->editor->close_file(new_baton, hex_digest, pool);
    }
}


/* Emit edits within directory DIR_BATON (with corresponding path
   E_PATH) with the changes from the directory S_REV/S_PATH to the
   directory B->t_rev/T_PATH.  S_PATH may be NULL if the entry does
   not exist in the source. */
static svn_error_t *
delta_dirs(report_baton_t *b, svn_revnum_t s_rev, const char *s_path,
           const char *t_path, void *dir_baton, const char *e_path,
           svn_boolean_t start_empty, apr_pool_t *pool)
{
  svn_fs_root_t *s_root;
  apr_hash_t *s_entries = NULL, *t_entries;
  apr_hash_index_t *hi;
  apr_pool_t *subpool;
  const svn_fs_dirent_t *s_entry, *t_entry;
  void *val;
  const char *name, *s_fullpath, *t_fullpath, *e_fullpath;
  path_info_t *info;

  /* Compare the property lists.  If we're starting empty, pass a NULL
     source path so that we add all the properties.
     
     When we support directory locks, we must pass the lock token here. */
  SVN_ERR(delta_proplists(b, s_rev, start_empty ? NULL : s_path, t_path,
                          NULL, change_dir_prop, dir_baton, pool));

  /* Get the list of entries in each of source and target. */
  if (s_path && !start_empty)
    {
      SVN_ERR(get_source_root(b, &s_root, s_rev));
      SVN_ERR(svn_fs_dir_entries(&s_entries, s_root, s_path, pool));
    }
  SVN_ERR(svn_fs_dir_entries(&t_entries, b->t_root, t_path, pool));

  /* Iterate over the report information for this directory. */
  subpool = svn_pool_create(pool);

  while (1)
    {
      svn_pool_clear(subpool);
      SVN_ERR(fetch_path_info(b, &name, &info, e_path, subpool));
      if (!name)
        break;

      if (info && !SVN_IS_VALID_REVNUM(info->rev))
        {
          /* We want to perform deletes before non-replacement adds,
             for graceful handling of case-only renames on
             case-insensitive client filesystems.  So, if the report
             item is a delete, remove the entry from the source hash,
             but don't update the entry yet. */
          if (s_entries)
            apr_hash_set(s_entries, name, APR_HASH_KEY_STRING, NULL);
          continue;
        }

      e_fullpath = svn_path_join(e_path, name, subpool);
      t_fullpath = svn_path_join(t_path, name, subpool);
      t_entry = apr_hash_get(t_entries, name, APR_HASH_KEY_STRING);
      s_fullpath = s_path ? svn_path_join(s_path, name, subpool) : NULL;
      s_entry = s_entries ?
        apr_hash_get(s_entries, name, APR_HASH_KEY_STRING) : NULL;

      SVN_ERR(update_entry(b, s_rev, s_fullpath, s_entry, t_fullpath,
                           t_entry, dir_baton, e_fullpath, info,
                           b->recurse, subpool));

      /* Don't revisit this name in the target or source entries. */
      apr_hash_set(t_entries, name, APR_HASH_KEY_STRING, NULL);
      if (s_entries)
        apr_hash_set(s_entries, name, APR_HASH_KEY_STRING, NULL);

      /* pathinfo entries live in their own subpools due to lookahead,
         so we need to clear each one out as we finish with it. */
      if (info)
        svn_pool_destroy(info->pool);
    }

  /* Remove any deleted entries.  Do this before processing the
     target, for graceful handling of case-only renames. */
  if (s_entries)
    {
      for (hi = apr_hash_first(pool, s_entries); hi; hi = apr_hash_next(hi))
        {
          svn_pool_clear(subpool);
          apr_hash_this(hi, NULL, NULL, &val);
          s_entry = val;

          if (apr_hash_get(t_entries, s_entry->name,
                           APR_HASH_KEY_STRING) == NULL)
            {
              /* There is no corresponding target entry, so delete. */
              e_fullpath = svn_path_join(e_path, s_entry->name, subpool);
              if (b->recurse || s_entry->kind != svn_node_dir)
                SVN_ERR(b->editor->delete_entry(e_fullpath,
                                                SVN_INVALID_REVNUM,
                                                dir_baton, subpool));
            }
        }
    }

  /* Loop over the dirents in the target. */
  for (hi = apr_hash_first(pool, t_entries); hi; hi = apr_hash_next(hi))
    {
      svn_pool_clear(subpool);
      apr_hash_this(hi, NULL, NULL, &val);
      t_entry = val;

      /* Compose the report, editor, and target paths for this entry. */
      e_fullpath = svn_path_join(e_path, t_entry->name, subpool);
      t_fullpath = svn_path_join(t_path, t_entry->name, subpool);

      /* Look for an entry with the same name in the source dirents. */
      s_entry = s_entries ?
        apr_hash_get(s_entries, t_entry->name, APR_HASH_KEY_STRING) : NULL;
      s_fullpath = s_entry ? svn_path_join(s_path, t_entry->name, subpool)
        : NULL;

      SVN_ERR(update_entry(b, s_rev, s_fullpath, s_entry, t_fullpath,
                           t_entry, dir_baton, e_fullpath, NULL,
                           b->recurse, subpool));
    }

  /* Destroy iteration subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
drive(report_baton_t *b, svn_revnum_t s_rev, path_info_t *info,
      apr_pool_t *pool)
{
  const char *t_anchor, *s_fullpath;
  svn_boolean_t allowed, info_is_set_path;
  svn_fs_root_t *s_root;
  const svn_fs_dirent_t *s_entry, *t_entry;
  void *root_baton;

  /* Compute the target path corresponding to the working copy anchor,
     and check its authorization. */
  t_anchor = *b->s_operand ? svn_path_dirname(b->t_path, pool) : b->t_path;
  SVN_ERR(check_auth(b, &allowed, t_anchor, pool));
  if (!allowed)
    return svn_error_create
      (SVN_ERR_AUTHZ_ROOT_UNREADABLE, NULL,
       _("Not authorized to open root of edit operation"));

  SVN_ERR(b->editor->set_target_revision(b->edit_baton, b->t_rev, pool));

  /* Collect information about the source and target nodes. */
  s_fullpath = svn_path_join(b->fs_base, b->s_operand, pool);
  SVN_ERR(get_source_root(b, &s_root, s_rev));
  SVN_ERR(fake_dirent(&s_entry, s_root, s_fullpath, pool));
  SVN_ERR(fake_dirent(&t_entry, b->t_root, b->t_path, pool));

  /* If the operand is a locally added file or directory, it won't
     exist in the source, so accept that. */
  info_is_set_path = (SVN_IS_VALID_REVNUM(info->rev) && !info->link_path);
  if (info_is_set_path && !s_entry)
    s_fullpath = NULL;

  /* Check if the target path exists first.  */
  if (!*b->s_operand && !(t_entry))
    return svn_error_create(SVN_ERR_FS_PATH_SYNTAX, NULL,
                            _("Target path does not exist"));

  /* If the anchor is the operand, the source and target must be dirs.
     Check this before opening the root to avoid modifying the wc. */
  else if (!*b->s_operand && (!s_entry || s_entry->kind != svn_node_dir
                              || t_entry->kind != svn_node_dir))
    return svn_error_create(SVN_ERR_FS_PATH_SYNTAX, NULL,
                            _("Cannot replace a directory from within"));

  SVN_ERR(b->editor->open_root(b->edit_baton, s_rev, pool, &root_baton));

  /* If the anchor is the operand, diff the two directories; otherwise
     update the operand within the anchor directory. */
  if (!*b->s_operand)
    SVN_ERR(delta_dirs(b, s_rev, s_fullpath, b->t_path, root_baton,
                       "", info->start_empty, pool));
  else
    SVN_ERR(update_entry(b, s_rev, s_fullpath, s_entry, b->t_path,
                         t_entry, root_baton, b->s_operand, info,
                         TRUE, pool));

  SVN_ERR(b->editor->close_directory(root_baton, pool));
  SVN_ERR(b->editor->close_edit(b->edit_baton, pool));
  return SVN_NO_ERROR;
}

/* Initialize the baton fields for editor-driving, and drive the editor. */
static svn_error_t *
finish_report(report_baton_t *b, apr_pool_t *pool)
{
  apr_off_t offset;
  path_info_t *info;
  apr_pool_t *subpool;
  svn_revnum_t s_rev;
  int i;

  /* Save our pool to manage the lookahead and fs_root cache with. */
  b->pool = pool;

  /* Add an end marker and rewind the temporary file. */
  SVN_ERR(svn_io_file_write_full(b->tempfile, "-", 1, NULL, pool));
  offset = 0;
  SVN_ERR(svn_io_file_seek(b->tempfile, APR_SET, &offset, pool));

  /* Read the first pathinfo from the report and verify that it is a top-level
     set_path entry. */
  SVN_ERR(read_path_info(&info, b->tempfile, pool));
  if (!info || strcmp(info->path, b->s_operand) != 0
      || info->link_path || !SVN_IS_VALID_REVNUM(info->rev))
    return svn_error_create(SVN_ERR_REPOS_BAD_REVISION_REPORT, NULL,
                            _("Invalid report for top level of working copy"));
  s_rev = info->rev;

  /* Initialize the lookahead pathinfo. */
  subpool = svn_pool_create(pool);
  SVN_ERR(read_path_info(&b->lookahead, b->tempfile, subpool));

  if (b->lookahead && strcmp(b->lookahead->path, b->s_operand) == 0)
    {
      /* If the operand of the wc operation is switched or deleted,
         then info above is just a place-holder, and the only thing we
         have to do is pass the revision it contains to open_root.
         The next pathinfo actually describes the target. */
      if (!*b->s_operand)
        return svn_error_create(SVN_ERR_REPOS_BAD_REVISION_REPORT, NULL,
                                _("Two top-level reports with no target"));
      info = b->lookahead;
      SVN_ERR(read_path_info(&b->lookahead, b->tempfile, subpool));
    }

  /* Open the target root and initialize the source root cache. */
  SVN_ERR(svn_fs_revision_root(&b->t_root, b->repos->fs, b->t_rev, pool));
  for (i = 0; i < NUM_CACHED_SOURCE_ROOTS; i++)
    b->s_roots[i] = NULL;

  return drive(b, s_rev, info, pool);
}

/* --- COLLECTING THE REPORT INFORMATION --- */

/* Record a report operation into the temporary file. */
static svn_error_t *
write_path_info(report_baton_t *b, const char *path, const char *lpath,
                svn_revnum_t rev, svn_boolean_t start_empty,
                const char *lock_token, apr_pool_t *pool)
{
  const char *lrep, *rrep, *ltrep, *rep;

  /* Munge the path to be anchor-relative, so that we can use edit paths
     as report paths. */
  path = svn_path_join(b->s_operand, path, pool);

  lrep = lpath ? apr_psprintf(pool, "+%" APR_SIZE_T_FMT ":%s",
                              strlen(lpath), lpath) : "-";
  rrep = (SVN_IS_VALID_REVNUM(rev)) ?
    apr_psprintf(pool, "+%ld:", rev) : "-";
  ltrep = lock_token ? apr_psprintf(pool, "+%" APR_SIZE_T_FMT ":%s",
                                    strlen(lock_token), lock_token) : "-";
  rep = apr_psprintf(pool, "+%" APR_SIZE_T_FMT ":%s%s%s%c%s",
                     strlen(path), path, lrep, rrep, start_empty ? '+' : '-',
                     ltrep);
  return svn_io_file_write_full(b->tempfile, rep, strlen(rep), NULL, pool);
}

svn_error_t *
svn_repos_set_path2(void *baton, const char *path, svn_revnum_t rev,
                    svn_boolean_t start_empty, const char *lock_token,
                    apr_pool_t *pool)
{
  return write_path_info(baton, path, NULL, rev, start_empty,
                         lock_token, pool);
}

svn_error_t *
svn_repos_set_path(void *baton, const char *path, svn_revnum_t rev,
                   svn_boolean_t start_empty, apr_pool_t *pool)
{
  return svn_repos_set_path2(baton, path, rev, start_empty, NULL, pool);
}

svn_error_t *
svn_repos_link_path2(void *baton, const char *path, const char *link_path,
                     svn_revnum_t rev, svn_boolean_t start_empty,
                     const char *lock_token, apr_pool_t *pool)
{
  return write_path_info(baton, path, link_path, rev, start_empty, lock_token,
                         pool);
}

svn_error_t *
svn_repos_link_path(void *baton, const char *path, const char *link_path,
                    svn_revnum_t rev, svn_boolean_t start_empty,
                    apr_pool_t *pool)
{
  return svn_repos_link_path2(baton, path, link_path, rev, start_empty,
                              NULL, pool);
}

svn_error_t *
svn_repos_delete_path(void *baton, const char *path, apr_pool_t *pool)
{
  return write_path_info(baton, path, NULL, SVN_INVALID_REVNUM, FALSE, NULL,
                         pool);
}

svn_error_t *
svn_repos_finish_report(void *baton, apr_pool_t *pool)
{
  report_baton_t *b = baton;
  svn_error_t *finish_err, *close_err;

  finish_err = finish_report(b, pool);
  close_err = svn_io_file_close(b->tempfile, pool);
  if (finish_err)
    svn_error_clear(close_err);
  return finish_err ? finish_err : close_err;
}

svn_error_t *
svn_repos_abort_report(void *baton, apr_pool_t *pool)
{
  report_baton_t *b = baton;

  return svn_io_file_close(b->tempfile, pool);
}

/* --- BEGINNING THE REPORT --- */

svn_error_t *
svn_repos_begin_report(void **report_baton,
                       svn_revnum_t revnum,
                       const char *username,
                       svn_repos_t *repos,
                       const char *fs_base,
                       const char *s_operand,
                       const char *switch_path,
                       svn_boolean_t text_deltas,
                       svn_boolean_t recurse,
                       svn_boolean_t ignore_ancestry,
                       const svn_delta_editor_t *editor,
                       void *edit_baton,
                       svn_repos_authz_func_t authz_read_func,
                       void *authz_read_baton,
                       apr_pool_t *pool)
{
  report_baton_t *b;
  const char *tempdir;

  /* Build a reporter baton.  Copy strings in case the caller doesn't
     keep track of them. */
  b = apr_palloc(pool, sizeof(*b));
  b->repos = repos;
  b->fs_base = apr_pstrdup(pool, fs_base);
  b->s_operand = apr_pstrdup(pool, s_operand);
  b->t_rev = revnum;
  b->t_path = switch_path ? switch_path
    : svn_path_join(fs_base, s_operand, pool);
  b->text_deltas = text_deltas;
  b->recurse = recurse;
  b->ignore_ancestry = ignore_ancestry;
  b->is_switch = (switch_path != NULL);
  b->editor = editor;
  b->edit_baton = edit_baton;
  b->authz_read_func = authz_read_func;
  b->authz_read_baton = authz_read_baton;

  SVN_ERR(svn_io_temp_dir(&tempdir, pool));
  SVN_ERR(svn_io_open_unique_file2(&b->tempfile, NULL,
                                   apr_psprintf(pool, "%s/report", tempdir),
                                   ".tmp", svn_io_file_del_on_close, pool));

  /* Hand reporter back to client. */
  *report_baton = b;
  return SVN_NO_ERROR;
}
