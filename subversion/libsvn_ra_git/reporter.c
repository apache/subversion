/*
 * reporter.c : `reporter' vtable routines for updates.
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

#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_private_config.h"

#include "private/svn_dep_compat.h"
#include "private/svn_subr_private.h"
#include "private/svn_string_private.h"
#include "private/svn_sorts_private.h"

#include "ra_git.h"

/* Theory of operation: we write report operations out to a spill-buffer
   as we receive them.  When the report is finished, we read the
   operations back out again, using them to guide the progression of
   the delta between the source and target revs.

   Spill-buffer content format: we use a simple ad-hoc format to store the
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
       +/-                    '+' indicates depth other than svn_depth_infinity
       If previous is +:
         <depth>:             "X","E","F","M" =>
                                 svn_depth_{exclude,empty,files,immediates}
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
  svn_depth_t depth;           /* Depth of this path, meaningless for files */
  svn_boolean_t start_empty;   /* Meaningless for delete_path */
  const char *lock_token;      /* NULL if no token */
  apr_pool_t *pool;            /* Container pool */
} path_info_t;

/* Describes the standard revision properties that are relevant for
   reports.  Since a particular revision will often show up more than
   once in the report, we cache these properties for the time of the
   report generation. */
typedef struct revision_info_t
{
  svn_revnum_t rev;            /* revision number */
  svn_string_t* date;          /* revision timestamp */
  svn_string_t* author;        /* name of the revisions' author */
} revision_info_t;

/* A structure used by the routines within the `reporter' vtable,
   driven by the client as it describes its working copy revisions. */
typedef struct report_baton_t
{
  /* Parameters remembered from svn_ra_git__reporter_begin_report */
  git_repository *repos;
  apr_hash_t *revmap;
  const char *fs_base;         /* fspath corresponding to wc anchor */
  const char *s_operand;       /* anchor-relative wc target (may be empty) */
  svn_revnum_t t_rev;          /* Revnum which the edit will bring the wc to */
  const char *t_path;          /* FS path the edit will bring the wc to */
  svn_boolean_t text_deltas;   /* Whether to report text deltas */

  /* If the client requested a specific depth, record it here; if the
     client did not, then this is svn_depth_unknown, and the depth of
     information transmitted from server to client will be governed
     strictly by the path-associated depths recorded in the report. */
  svn_depth_t requested_depth;

  svn_boolean_t ignore_ancestry;
  svn_boolean_t send_copyfrom_args;
  svn_boolean_t is_switch;
  const svn_delta_editor_t *editor;
  void *edit_baton;

  /* The spill-buffer holding the report. */
  svn_spillbuf_reader_t *reader;

  /* For the actual editor drive, we'll need a lookahead path info
     entry, a cache of FS roots, and a pool to store them. */
  path_info_t *lookahead;
  git_tree *t_root;
  git_tree *s_root;
  svn_revnum_t s_root_revision;

  /* Cache for revision properties. This is used to eliminate redundant
     revprop fetching. */
  apr_hash_t *revision_infos;

  /* This will not change. So, fetch it once and reuse it. */
  svn_string_t *repos_uuid;
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
                               svn_boolean_t start_empty,
                               svn_depth_t wc_depth,
                               svn_depth_t requested_depth,
                               apr_pool_t *pool);

/* --- READING PREVIOUSLY STORED REPORT INFORMATION --- */

static svn_error_t *
read_number(apr_uint64_t *num, svn_spillbuf_reader_t *reader, apr_pool_t *pool)
{
  char c;

  *num = 0;
  while (1)
    {
      SVN_ERR(svn_spillbuf__reader_getc(&c, reader, pool));
      if (c == ':')
        break;
      *num = *num * 10 + (c - '0');
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
read_string(const char **str, svn_spillbuf_reader_t *reader, apr_pool_t *pool)
{
  apr_uint64_t len;
  apr_size_t size;
  apr_size_t amt;
  char *buf;

  SVN_ERR(read_number(&len, reader, pool));

  /* Len can never be less than zero.  But could len be so large that
     len + 1 wraps around and we end up passing 0 to apr_palloc(),
     thus getting a pointer to no storage?  Probably not (16 exabyte
     string, anyone?) but let's be future-proof anyway. */
  if (len + 1 < len || len + 1 > APR_SIZE_MAX)
    {
      /* xgettext doesn't expand preprocessor definitions, so we must
         pass translatable string to apr_psprintf() function to create
         intermediate string with appropriate format specifier. */
      return svn_error_createf(SVN_ERR_REPOS_BAD_REVISION_REPORT, NULL,
                               apr_psprintf(pool,
                                            _("Invalid length (%%%s) when "
                                              "about to read a string"),
                                            APR_UINT64_T_FMT),
                               len);
    }

  size = (apr_size_t)len;
  buf = apr_palloc(pool, size+1);
  if (size > 0)
    {
      SVN_ERR(svn_spillbuf__reader_read(&amt, reader, buf, size, pool));
      SVN_ERR_ASSERT(amt == size);
    }
  buf[len] = 0;
  *str = buf;
  return SVN_NO_ERROR;
}

static svn_error_t *
read_rev(svn_revnum_t *rev, svn_spillbuf_reader_t *reader, apr_pool_t *pool)
{
  char c;
  apr_uint64_t num;

  SVN_ERR(svn_spillbuf__reader_getc(&c, reader, pool));
  if (c == '+')
    {
      SVN_ERR(read_number(&num, reader, pool));
      *rev = (svn_revnum_t) num;
    }
  else
    *rev = SVN_INVALID_REVNUM;
  return SVN_NO_ERROR;
}

/* Read a single character to set *DEPTH (having already read '+')
   from READER.  PATH is the path to which the depth applies, and is
   used for error reporting only. */
static svn_error_t *
read_depth(svn_depth_t *depth, svn_spillbuf_reader_t *reader, const char *path,
           apr_pool_t *pool)
{
  char c;

  SVN_ERR(svn_spillbuf__reader_getc(&c, reader, pool));
  switch (c)
    {
    case 'X':
      *depth = svn_depth_exclude;
      break;
    case 'E':
      *depth = svn_depth_empty;
      break;
    case 'F':
      *depth = svn_depth_files;
      break;
    case 'M':
      *depth = svn_depth_immediates;
      break;

      /* Note that we do not tolerate explicit representation of
         svn_depth_infinity here, because that's not how
         write_path_info() writes it. */
    default:
      return svn_error_createf(SVN_ERR_REPOS_BAD_REVISION_REPORT, NULL,
                               _("Invalid depth (%c) for path '%s'"), c, path);
    }

  return SVN_NO_ERROR;
}

/* Read a report operation *PI out of READER.  Set *PI to NULL if we
   have reached the end of the report. */
static svn_error_t *
read_path_info(path_info_t **pi,
               svn_spillbuf_reader_t *reader,
               apr_pool_t *pool)
{
  char c;

  SVN_ERR(svn_spillbuf__reader_getc(&c, reader, pool));
  if (c == '-')
    {
      *pi = NULL;
      return SVN_NO_ERROR;
    }

  *pi = apr_palloc(pool, sizeof(**pi));
  SVN_ERR(read_string(&(*pi)->path, reader, pool));
  SVN_ERR(svn_spillbuf__reader_getc(&c, reader, pool));
  if (c == '+')
    SVN_ERR(read_string(&(*pi)->link_path, reader, pool));
  else
    (*pi)->link_path = NULL;
  SVN_ERR(read_rev(&(*pi)->rev, reader, pool));
  SVN_ERR(svn_spillbuf__reader_getc(&c, reader, pool));
  if (c == '+')
    SVN_ERR(read_depth(&((*pi)->depth), reader, (*pi)->path, pool));
  else
    (*pi)->depth = svn_depth_infinity;
  SVN_ERR(svn_spillbuf__reader_getc(&c, reader, pool));
  (*pi)->start_empty = (c == '+');
  SVN_ERR(svn_spillbuf__reader_getc(&c, reader, pool));
  if (c == '+')
    SVN_ERR(read_string(&(*pi)->lock_token, reader, pool));
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

/* Fetch the next pathinfo from B->reader for a descendant of
   PREFIX.  If the next pathinfo is for an immediate child of PREFIX,
   set *ENTRY to the path component of the report information and
   *INFO to the path information for that entry.  If the next pathinfo
   is for a grandchild or other more remote descendant of PREFIX, set
   *ENTRY to the immediate child corresponding to that descendant and
   set *INFO to NULL.  If the next pathinfo is not for a descendant of
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
          SVN_ERR(read_path_info(&b->lookahead, b->reader, subpool));
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
      SVN_ERR(read_path_info(&b->lookahead, b->reader, subpool));
    }
  return SVN_NO_ERROR;
}

/* Return true if there is at least one path info entry relevant to *PREFIX. */
static svn_boolean_t
any_path_info(report_baton_t *b, const char *prefix)
{
  return relevant(b->lookahead, prefix, strlen(prefix));
}

static svn_error_t *
fetch_revision_root(git_tree **root,
                    git_repository *repos,
                    apr_hash_t *revmap,
                    svn_revnum_t rev)
{
  git_oid *oid;
  git_commit *commit;
  int git_err;

  oid = apr_hash_get(revmap, &rev, sizeof(rev));
  if (oid == NULL)
    return svn_error_create(SVN_ERR_FS_NO_SUCH_REVISION, NULL, NULL);

  git_err = git_commit_lookup(&commit, repos, oid);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  git_err = git_commit_tree(root, commit);
  git_commit_free(commit);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  return SVN_NO_ERROR;
}

/* --- DRIVING THE EDITOR ONCE THE REPORT IS FINISHED --- */

/* While driving the editor, the target root will remain constant, but
   we may have to jump around between source roots depending on the
   state of the working copy. We open a root each time we revisit a rev
   unless the same revision is requested in succession. */
static svn_error_t *
get_source_root(report_baton_t *b, git_tree **s_root, svn_revnum_t rev)
{
  if (b->s_root)
    {
      if (SVN_IS_VALID_REVNUM(b->s_root_revision) &&
          b->s_root_revision == rev)
        {
          *s_root = b->s_root;
          return SVN_NO_ERROR;
        }

      git_tree_free(b->s_root);
      b->s_root = NULL;
    }
    
  SVN_ERR(fetch_revision_root(s_root, b->repos, b->revmap, rev));
  return SVN_NO_ERROR;
}

/* Call the directory property-setting function of B->editor to set
   the property NAME to VALUE on DIR_BATON. */
static svn_error_t *
change_dir_prop(report_baton_t *b, void *dir_baton, const char *name,
                const svn_string_t *value, apr_pool_t *pool)
{
  return svn_error_trace(b->editor->change_dir_prop(dir_baton, name, value,
                                                    pool));
}

/* Call the file property-setting function of B->editor to set the
   property NAME to VALUE on FILE_BATON. */
static svn_error_t *
change_file_prop(report_baton_t *b, void *file_baton, const char *name,
                 const svn_string_t *value, apr_pool_t *pool)
{
  return svn_error_trace(b->editor->change_file_prop(file_baton, name, value,
                                                     pool));
}

/* For the report B, return the relevant revprop data of revision REV in
   REVISION_INFO. The revision info will be allocated in b->pool.
   Temporaries get allocated on SCRATCH_POOL. */
static svn_error_t *
get_revision_info(report_baton_t *b,
                  svn_revnum_t rev,
                  revision_info_t** revision_info,
                  apr_pool_t *scratch_pool)
{
  apr_hash_t *r_props;
  svn_string_t *cdate, *author;
  revision_info_t* info;

  /* Try to find the info in the report's cache */
  info = apr_hash_get(b->revision_infos, &rev, sizeof(rev));
  if (!info)
    {
      git_oid *oid;
      git_commit *commit;
      int git_err;

      /* Info is not available, yet.
         Get all revprops. */
      oid = apr_hash_get(b->revmap, &rev, sizeof(rev));
      if (oid == NULL)
        return svn_error_create(SVN_ERR_FS_NO_SUCH_REVISION, NULL, NULL);

      git_err = git_commit_lookup(&commit, b->repos, oid);
      if (git_err)
        return svn_error_trace(svn_ra_git__wrap_git_error());

      r_props = svn_ra_git__make_revprops_hash(commit, scratch_pool);

      /* Extract the committed-date. */
      cdate = svn_hash_gets(r_props, SVN_PROP_REVISION_DATE);

      /* Extract the last-author. */
      author = svn_hash_gets(r_props, SVN_PROP_REVISION_AUTHOR);

      /* Create a result object */
      info = apr_palloc(b->pool, sizeof(*info));
      info->rev = rev;
      info->date = cdate ? svn_string_dup(cdate, b->pool) : NULL;
      info->author = author ? svn_string_dup(author, b->pool) : NULL;

      /* Cache it */
      apr_hash_set(b->revision_infos, &info->rev, sizeof(info->rev), info);
    }

  *revision_info = info;
  return SVN_NO_ERROR;
}

static svn_error_t *
node_proplist(apr_hash_t **table_p, git_tree *root,
              const char *path, apr_pool_t *pool)
{
  /* ### This is just a stub until property support appears in ra_git. */
  *table_p = apr_hash_make(pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
props_different(svn_boolean_t *different_p,
                git_tree *root1,
                const char *path1,
                git_tree *root2,
                const char *path2,
                apr_pool_t *pool)
{
  /* ### This is just a stub until property support appears in ra_git. */
  *different_p = FALSE;
  return SVN_NO_ERROR;
}

static svn_error_t *
get_lock(svn_lock_t **lock,
         git_repository *repos,
         const char *path, 
         apr_pool_t *pool)
{
  *lock = NULL; /* ### locks are hard in a distributed system */
  return SVN_NO_ERROR;
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
  git_tree *s_root;
  apr_hash_t *s_props = NULL, *t_props;
  apr_array_header_t *prop_diffs;
  int i;
  svn_revnum_t crev;
  revision_info_t *revision_info;
  svn_boolean_t changed;
  const svn_prop_t *pc;
  svn_lock_t *lock;
  apr_hash_index_t *hi;

  /* Fetch the target's created-rev and send entry props. */
  SVN_ERR(svn_ra_git__find_last_changed(&crev, b->revmap, t_path, b->t_rev,
                                        git_tree_owner(b->t_root),
                                        pool));
  if (SVN_IS_VALID_REVNUM(crev))
    {
      /* convert committed-rev to  string */
      char buf[SVN_INT64_BUFFER_SIZE];
      svn_string_t cr_str;
      cr_str.data = buf;
      cr_str.len = svn__i64toa(buf, crev);

      /* Transmit the committed-rev. */
      SVN_ERR(change_fn(b, object,
                        SVN_PROP_ENTRY_COMMITTED_REV, &cr_str, pool));

      SVN_ERR(get_revision_info(b, crev, &revision_info, pool));

      /* Transmit the committed-date. */
      if (revision_info->date || s_path)
        SVN_ERR(change_fn(b, object, SVN_PROP_ENTRY_COMMITTED_DATE,
                          revision_info->date, pool));

      /* Transmit the last-author. */
      if (revision_info->author || s_path)
        SVN_ERR(change_fn(b, object, SVN_PROP_ENTRY_LAST_AUTHOR,
                          revision_info->author, pool));

      /* Transmit the UUID. */
      SVN_ERR(change_fn(b, object, SVN_PROP_ENTRY_UUID,
                        b->repos_uuid, pool));
    }

  /* Update lock properties. */
  if (lock_token)
    {
      SVN_ERR(get_lock(&lock, b->repos, t_path, pool));
      /* Delete a defunct lock. */
      if (! lock || strcmp(lock_token, lock->token) != 0)
        SVN_ERR(change_fn(b, object, SVN_PROP_ENTRY_LOCK_TOKEN,
                          NULL, pool));
    }

  if (s_path)
    {
      SVN_ERR(get_source_root(b, &s_root, s_rev));

      /* Is this deltification worth our time? */
      SVN_ERR(props_different(&changed, b->t_root, t_path, s_root,
                              s_path, pool));
      if (! changed)
        return SVN_NO_ERROR;

      /* If so, go ahead and get the source path's properties. */
      SVN_ERR(node_proplist(&s_props, s_root, s_path, pool));
    }

  /* Get the target path's properties */
  SVN_ERR(node_proplist(&t_props, b->t_root, t_path, pool));

  if (s_props && apr_hash_count(s_props))
    {
      /* Now transmit the differences. */
      SVN_ERR(svn_prop_diffs(&prop_diffs, t_props, s_props, pool));
      for (i = 0; i < prop_diffs->nelts; i++)
        {
          pc = &APR_ARRAY_IDX(prop_diffs, i, svn_prop_t);
          SVN_ERR(change_fn(b, object, pc->name, pc->value, pool));
        }
    }
  else if (apr_hash_count(t_props))
    {
      /* So source, i.e. all new.  Transmit all target props. */
      for (hi = apr_hash_first(pool, t_props); hi; hi = apr_hash_next(hi))
        {
          const char *key = svn__apr_hash_index_key(hi);
          svn_string_t *val = svn__apr_hash_index_val(hi);

          SVN_ERR(change_fn(b, object, key, val, pool));
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
file_checksum(svn_checksum_t **checksum,
              svn_checksum_kind_t kind,
              git_tree *root,
              const char *path,
              svn_boolean_t force,
              apr_pool_t *pool)
{
  git_tree_entry *entry;
  git_blob *blob;
  int git_err;

  git_err = git_tree_entry_bypath(&entry, root, path);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  git_err = git_tree_entry_to_object((git_object **)&blob,
                                     git_tree_owner(root), entry);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  SVN_ERR(svn_checksum(checksum, kind, git_blob_rawcontent(blob),
                       git_blob_rawsize(blob), pool));

  git_blob_free(blob);
  git_tree_entry_free(entry);

  return SVN_NO_ERROR;
}

static svn_error_t *
get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                      git_repository *repos,
                      git_tree *source_root,
                      const char *source_path,
                      git_tree *target_root,
                      const char *target_path,
                      apr_pool_t *pool)
{
  git_tree_entry *target_entry;
  git_blob *target_blob;
  svn_stream_t *source_stream;
  svn_stream_t *target_stream;
  int git_err;

  if (source_path)
    {
      git_tree_entry *source_entry;
      git_blob *source_blob;

      git_err = git_tree_entry_bypath(&source_entry, source_root, source_path);
      if (git_err)
        return svn_error_trace(svn_ra_git__wrap_git_error());

      SVN_ERR_ASSERT(git_tree_entry_type(source_entry) == GIT_OBJ_BLOB);

      git_err = git_tree_entry_to_object((git_object **)&source_blob, repos,
                                          source_entry);
      git_tree_entry_free(source_entry);
      if (git_err)
        return svn_error_trace(svn_ra_git__wrap_git_error());

      if (source_blob)
        {
          apr_size_t len = git_blob_rawsize(source_blob);

          source_stream = svn_stream_buffered(pool);
          SVN_ERR(svn_stream_write(source_stream,
                                   (char*)git_blob_rawcontent(source_blob),
                                   &len));
        }
      else
        source_stream = svn_stream_empty(pool);
    }
  else
    source_stream = svn_stream_empty(pool);

  git_err = git_tree_entry_bypath(&target_entry, target_root, target_path);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  SVN_ERR_ASSERT(git_tree_entry_type(target_entry) == GIT_OBJ_BLOB);

  git_err = git_tree_entry_to_object((git_object **)&target_blob, repos,
                                      target_entry);
  git_tree_entry_free(target_entry);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  if (target_blob)
    {
      apr_size_t len = git_blob_rawsize(target_blob);

      target_stream = svn_stream_buffered(pool);
      SVN_ERR(svn_stream_write(target_stream,
                               (char*)git_blob_rawcontent(target_blob),
                               &len));
      git_blob_free(target_blob);
    }
  else
    target_stream = svn_stream_empty(pool);

  svn_txdelta2(stream_p, source_stream, target_stream, FALSE, pool);
  return SVN_NO_ERROR;
}

struct blob_relatedness_baton {
  git_blob *blob;
  git_blob *other_blob;
  int distance;
  svn_boolean_t parent_is_repos_root;
} blob_relatedness_baton;

/* An implementation of git_diff_file_cb. */
static int
blob_relatedness_cb(const git_diff_delta *delta, float progress, void *payload)
{
  struct blob_relatedness_baton *b = payload;
  
  /* At least one of the oids should match, else we're not looking
   * at the right blob. */
  if (!git_oid_equal(&delta->old_file.oid, git_blob_id(b->blob)) &&
      !git_oid_equal(&delta->new_file.oid, git_blob_id(b->other_blob)))
    return 0;

  if (git_oid_iszero(&delta->old_file.oid) ||
      git_oid_iszero(&delta->new_file.oid))
    {
      /* A zero oid means the blob doesn't actually exist on one side. */
      b->distance = -1;
    }
  else if ((delta->old_file.flags & GIT_DIFF_FLAG_BINARY) !=
      (delta->new_file.flags & GIT_DIFF_FLAG_BINARY))
    {
      /* If content switches from/to binary, treat as unrelated. */ 
      b->distance = -1;
    }
  else if ((delta->old_file.flags & GIT_DIFF_FLAG_NOT_BINARY) !=
           (delta->new_file.flags & GIT_DIFF_FLAG_NOT_BINARY))
    {
      /* If content switches from/to not-binary, treat as unrelated. */ 
      b->distance = -1;
    }
  else if (delta->status == GIT_DELTA_ADDED ||
           delta->status == GIT_DELTA_DELETED)
    {
      /* If deletion or addition was detected, treat as unrelated. */ 
      b->distance = -1;
    }
  else if (delta->status == GIT_DELTA_UNMODIFIED)
    {
      /* If it wasn't modified, treat as directly related. */
      b->distance = 0;
    }
  else if (delta->status == GIT_DELTA_MODIFIED)
    {
      /* If the diff is a plain modification, blobs are 'otherwise' related. */
      b->distance = 1;
    }
  else if (delta->status == GIT_DELTA_RENAMED ||
           delta->status == GIT_DELTA_COPIED)
    {
      /* Determine relatedness based on git's similarity score. */
      if (delta->similarity > 75)
        b->distance = 1;
      else
        b->distance = -1;
    }
  else if (b->parent_is_repos_root)
    {
      /* Treat blobs in the repos root as otherwise related if we have
       * no further information. */
      b->distance = 1;
    }
  else
    {
      /* Treat as unrelated by default. */
      b->distance = -1;
    }
  
  return -1;
}

static svn_error_t *
compare_files(svn_boolean_t *changed,
              git_repository *repos,
              git_tree *root,
              const char *path,
              git_tree *other_root,
              const char *other_path,
              apr_pool_t *pool)
{
  git_tree_entry *entry;
  git_tree_entry *other_entry;
  git_blob *blob;
  git_blob *other_blob;
  struct blob_relatedness_baton b;
  int git_err;

  git_err = git_tree_entry_bypath(&entry, root, path);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  git_err = git_tree_entry_bypath(&other_entry, other_root, other_path);
  if (git_err)
    {
      git_tree_entry_free(entry);
      return svn_error_trace(svn_ra_git__wrap_git_error());
    }

  SVN_ERR_ASSERT(git_tree_entry_type(entry) == GIT_OBJ_BLOB);
  SVN_ERR_ASSERT(git_tree_entry_type(other_entry) == GIT_OBJ_BLOB);

  git_err = git_tree_entry_to_object((git_object **)&blob, repos, entry);
  if (git_err)
    {
      git_tree_entry_free(entry);
      git_tree_entry_free(other_entry);
      return svn_error_trace(svn_ra_git__wrap_git_error());
    }

  git_err = git_tree_entry_to_object((git_object **)&other_blob, repos,
                                     other_entry);
  if (git_err)
    {
      git_blob_free(blob);
      git_tree_entry_free(entry);
      git_tree_entry_free(other_entry);
      return svn_error_trace(svn_ra_git__wrap_git_error());
    }

  b.blob = blob;
  b.other_blob = other_blob;
  b.distance = 0;
  b.parent_is_repos_root = FALSE; /* can be set to anything for our purposes */
  git_err = git_diff_blobs(blob, svn_relpath_basename(path, NULL),
                           other_blob, svn_relpath_basename(other_path, NULL),
                           NULL, blob_relatedness_cb, NULL, NULL, &b);
  if (git_err)
    {
      if (git_err == GIT_EUSER)
        giterr_clear();
      else
        {
          git_blob_free(blob);
          git_blob_free(other_blob);
          git_tree_entry_free(entry);
          git_tree_entry_free(other_entry);
          return svn_error_trace(svn_ra_git__wrap_git_error());
        }
    }

  *changed = (b.distance != 0);

  git_blob_free(blob);
  git_blob_free(other_blob);
  git_tree_entry_free(entry);
  git_tree_entry_free(other_entry);
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
  git_tree *s_root = NULL;
  svn_txdelta_stream_t *dstream = NULL;
  svn_checksum_t *s_checksum;
  const char *s_hex_digest = NULL;
  svn_txdelta_window_handler_t dhandler;
  void *dbaton;

  /* Compare the files' property lists.  */
  SVN_ERR(delta_proplists(b, s_rev, s_path, t_path, lock_token,
                          change_file_prop, file_baton, pool));

  if (s_path)
    {
      SVN_ERR(get_source_root(b, &s_root, s_rev));

      /* We're not interested in the theoretical difference between "has
         contents which have not changed with respect to" and "has the same
         actual contents as" when sending text-deltas.  If we know the
         delta is an empty one, we avoiding sending it in either case. */
      SVN_ERR(compare_files(&changed, b->repos, b->t_root, t_path,
                            s_root, s_path, pool));
      if (!changed)
        return SVN_NO_ERROR;

      SVN_ERR(file_checksum(&s_checksum, svn_checksum_md5, s_root,
                            s_path, TRUE, pool));
      s_hex_digest = svn_checksum_to_cstring(s_checksum, pool);
    }

  /* Send the delta stream if desired, or just a NULL window if not. */
  SVN_ERR(b->editor->apply_textdelta(file_baton, s_hex_digest, pool,
                                     &dhandler, &dbaton));

  if (dhandler != svn_delta_noop_window_handler)
    {
      if (b->text_deltas)
        {
          SVN_ERR(get_file_delta_stream(&dstream, b->repos, s_root, s_path,
                                        b->t_root, t_path, pool));
          SVN_ERR(svn_txdelta_send_txstream(dstream, dhandler, dbaton, pool));
        }
      else
        SVN_ERR(dhandler(NULL, dbaton));
    }

  return SVN_NO_ERROR;
}

/* Determine if the user is authorized to view B->t_root/PATH. */
static svn_error_t *
check_auth(report_baton_t *b, svn_boolean_t *allowed, const char *path,
           apr_pool_t *pool)
{
  *allowed = TRUE;
  return SVN_NO_ERROR;
}

typedef struct ra_git_dirent_t {
  const char *name;
  svn_node_kind_t kind;
  git_tree_entry *entry;
  git_tree_entry *parent_entry;
} ra_git_dirent_t;

static apr_status_t
cleanup_fake_dirent(void *data)
{
  ra_git_dirent_t *dirent = data;
  if (dirent->entry)
    git_tree_entry_free(dirent->entry);
  if (dirent->parent_entry)
    git_tree_entry_free(dirent->parent_entry);
  return APR_SUCCESS;
}

/* Create a dirent in *ENTRY for the given ROOT and PATH.  We use this to
   replace the source or target dirent when a report pathinfo tells us to
   change paths or revisions. */
static svn_error_t *
fake_dirent(const ra_git_dirent_t **entry, git_tree *root,
            const char *path, apr_pool_t *pool)
{
  svn_node_kind_t kind;
  ra_git_dirent_t *ent;
  int git_err;

  ent = apr_palloc(pool, sizeof(**entry));
  if (path[0] == '\0')
    {
      ent->name = "";
      ent->kind = svn_node_dir;
      ent->entry = NULL;
      ent->parent_entry = NULL;
      *entry = ent;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_ra_git__check_path(&kind, root, path));
  if (kind == svn_node_none)
    {
      *entry = NULL;
      return SVN_NO_ERROR;
    }

  ent->name = svn_relpath_basename(path, pool);
  ent->kind = kind;
  git_err = git_tree_entry_bypath(&ent->entry, root, path);
  if (git_err)
    {
      if (git_err == GIT_ENOTFOUND)
        return svn_error_create(SVN_ERR_FS_NO_SUCH_ENTRY, NULL, NULL);

      return svn_error_trace(svn_ra_git__wrap_git_error());
    }

  if (git_tree_entry_type(ent->entry) == GIT_OBJ_BLOB)
    {
      const char *parent_path;

      /* Store the parent's entry as well for ancestry detection.*/
      svn_relpath_split(&parent_path, NULL, path, pool);
      if (parent_path[0] == '\0')
        ent->parent_entry = NULL;
      else
        {
          git_err = git_tree_entry_bypath(&ent->parent_entry, root,
                                          parent_path);
          if (git_err)
            {
              if (git_err == GIT_ENOTFOUND)
                return svn_error_create(SVN_ERR_FS_NO_SUCH_ENTRY, NULL, NULL);

              return svn_error_trace(svn_ra_git__wrap_git_error());
            }
        }
    }
  else
    ent->parent_entry = NULL;

  apr_pool_cleanup_register(pool, ent, cleanup_fake_dirent,
                            apr_pool_cleanup_null);
  *entry = ent;
  return SVN_NO_ERROR;
}

/* Given REQUESTED_DEPTH, WC_DEPTH and the current entry's KIND,
   determine whether we need to send the whole entry, not just deltas.
   Please refer to delta_dirs' docstring for an explanation of the
   conditionals below. */
static svn_boolean_t
is_depth_upgrade(svn_depth_t wc_depth,
                 svn_depth_t requested_depth,
                 svn_node_kind_t kind)
{
  if (requested_depth == svn_depth_unknown
      || requested_depth <= wc_depth
      || wc_depth == svn_depth_immediates)
    return FALSE;

  if (kind == svn_node_file
      && wc_depth == svn_depth_files)
    return FALSE;

  if (kind == svn_node_dir
      && wc_depth == svn_depth_empty
      && requested_depth == svn_depth_files)
    return FALSE;

  return TRUE;
}


/* Call the B->editor's add_file() function to create PATH as a child
   of PARENT_BATON, returning a new baton in *NEW_FILE_BATON.
   However, make an attempt to send 'copyfrom' arguments if they're
   available, by examining the closest copy of the original file
   O_PATH within B->t_root.  If any copyfrom args are discovered,
   return those in *COPYFROM_PATH and *COPYFROM_REV;  otherwise leave
   those return args untouched. */
static svn_error_t *
add_file_smartly(report_baton_t *b,
                 const char *path,
                 void *parent_baton,
                 const char *o_path,
                 void **new_file_baton,
                 const char **copyfrom_path,
                 svn_revnum_t *copyfrom_rev,
                 apr_pool_t *pool)
{
  /* ### TODO:  use a subpool to do this work, clear it at the end? */
#if 0
  git_tree *closest_copy_root = NULL;
  const char *closest_copy_path = NULL;
#endif

  /* Pre-emptively assume no copyfrom args exist. */
  *copyfrom_path = NULL;
  *copyfrom_rev = SVN_INVALID_REVNUM;

#if 0
  if (b->send_copyfrom_args)
    {
      /* Find the destination of the nearest 'copy event' which may have
         caused o_path@t_root to exist. svn_fs_closest_copy only returns paths
         starting with '/', so make sure o_path always starts with a '/'
         too. */
      if (*o_path != '/')
        o_path = apr_pstrcat(pool, "/", o_path, SVN_VA_NULL);

      SVN_ERR(svn_fs_closest_copy(&closest_copy_root, &closest_copy_path,
                                  b->t_root, o_path, pool));

      if (closest_copy_root != NULL)
        {
          /* If the destination of the copy event is the same path as
             o_path, then we've found something interesting that should
             have 'copyfrom' history. */
          if (strcmp(closest_copy_path, o_path) == 0)
            {
              SVN_ERR(svn_fs_copied_from(copyfrom_rev, copyfrom_path,
                                         closest_copy_root, closest_copy_path,
                                         pool));
            }
        }
    }
#endif

  return svn_error_trace(b->editor->add_file(path, parent_baton,
                                             *copyfrom_path, *copyfrom_rev,
                                             pool, new_file_baton));
}

static svn_error_t *
detect_blob_relatedness(int *distance, git_repository *repos,
                        git_tree *parent, git_tree *other_parent,
                        git_blob *blob, git_blob *other_blob)
{
  int git_err;
  struct blob_relatedness_baton b;
  git_diff *diff;

  b.blob = blob;
  b.other_blob = other_blob;
  b.distance = 0;

  if (parent == NULL || other_parent == NULL)
    {
      b.parent_is_repos_root = TRUE;
      git_err = git_diff_blobs(blob, "", other_blob, "", NULL,
                               blob_relatedness_cb, NULL, NULL, &b);
      if (git_err)
        {
          if (git_err == GIT_EUSER)
            giterr_clear();
          else
            return svn_error_trace(svn_ra_git__wrap_git_error());
        }

      *distance = b.distance;

      return SVN_NO_ERROR;
    }

  b.parent_is_repos_root = FALSE;

  git_err = git_diff_tree_to_tree(&diff, repos, parent, other_parent, NULL);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  git_err = git_diff_find_similar(diff, NULL);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  /* Loop over changes, detect adds/deletes/mods of the blob in question. */
  git_err = git_diff_foreach(diff, blob_relatedness_cb, NULL, NULL, &b);
  if (git_err)
    {
      if (git_err == GIT_EUSER)
        giterr_clear();
      else
        return svn_error_trace(svn_ra_git__wrap_git_error());
    }

  *distance = b.distance;

  return SVN_NO_ERROR;
}

/* Like svn_fs_compare_ids() but for ra_git_dirent_t. */
static svn_error_t *
detect_relatedness(int *distance,
                   git_repository *repos,
                   const ra_git_dirent_t *entry,
                   const ra_git_dirent_t *other_entry)
{
  git_otype type = git_tree_entry_type(entry->entry);
  git_otype other_type = git_tree_entry_type(other_entry->entry);
  git_object *object;
  git_object *other_object;
  int git_err;

  if (type != other_type)
    {
      /* Trees are unrelated to blobs. */
      *distance = -1;
      return SVN_NO_ERROR;
    }

  git_err = git_tree_entry_to_object(&object, repos, entry->entry);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  git_err = git_tree_entry_to_object(&other_object, repos, other_entry->entry);
  if (git_err)
    return svn_error_trace(svn_ra_git__wrap_git_error());

  if (type == GIT_OBJ_TREE)
    {
      /* ### Trees are always related for now.
       * ### Can we map the concept of node ancestry to git trees?
       * ### We could possibly detect added/deleted trees here. */
      *distance = 1;
      return SVN_NO_ERROR;
    }
  else if (type == GIT_OBJ_BLOB)
    {
      git_tree *parent;
      git_tree *other_parent;

      if (entry->parent_entry)
        {
            git_err = git_tree_entry_to_object((git_object **)&parent, repos,
                                                entry->parent_entry);
            if (git_err)
              return svn_error_trace(svn_ra_git__wrap_git_error());
        } 
      else
        parent = NULL;

      if (other_entry->parent_entry)
        {
          git_err = git_tree_entry_to_object((git_object **)&other_parent,
                                             repos,
                                             other_entry->parent_entry);
          if (git_err)
            return svn_error_trace(svn_ra_git__wrap_git_error());
        }
      else
        other_parent = NULL;

      return svn_error_trace(detect_blob_relatedness(distance, repos,
                                                     parent, other_parent,
                                                     (git_blob *)object,
                                                     (git_blob *)other_object));
    } 

  *distance = -1;
  return SVN_NO_ERROR;
}

static svn_error_t *
find_deleted_rev(git_repository *repos,
                 apr_hash_t *revmap,
                 const char *path,
                 svn_revnum_t start,
                 svn_revnum_t end,
                 svn_revnum_t *deleted,
                 apr_pool_t *pool)
{
  git_tree *tree;
  git_tree_entry *entry;
  svn_revnum_t rev = start;
  int step;

  step = (start < end) ? 1 : -1;
  rev = start;
  if (start == end)
    end += step;
  while (rev != end)
    {
      int git_err;

      SVN_ERR(fetch_revision_root(&tree, repos, revmap, rev));

      git_err = git_tree_entry_bypath(&entry, tree, path);
      if (git_err)
        {
          if (git_err == GIT_ENOTFOUND)
            {
              if (rev == start)
                *deleted = SVN_INVALID_REVNUM;
              else
                *deleted = rev;

              git_tree_free(tree);
              return SVN_NO_ERROR;
            }
          return svn_error_trace(svn_ra_git__wrap_git_error());
        }

      rev += step;

      git_tree_free(tree);
      git_tree_entry_free(entry);
    }

  *deleted = SVN_INVALID_REVNUM;

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

   WC_DEPTH and REQUESTED_DEPTH are propagated to delta_dirs() if
   necessary.  Refer to delta_dirs' docstring to find out what
   should happen for various combinations of WC_DEPTH/REQUESTED_DEPTH. */
static svn_error_t *
update_entry(report_baton_t *b, svn_revnum_t s_rev, const char *s_path,
             const ra_git_dirent_t *s_entry, const char *t_path,
             const ra_git_dirent_t *t_entry, void *dir_baton,
             const char *e_path, path_info_t *info, svn_depth_t wc_depth,
             svn_depth_t requested_depth, apr_pool_t *pool)
{
  git_tree *s_root;
  svn_boolean_t allowed, related;
  void *new_baton;
  svn_checksum_t *checksum;
  const char *hex_digest;

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

  /* If the source and target both exist and are of the same kind,
     then find out whether they're related.  If they're exactly the
     same, then we don't have to do anything (unless the report has
     changes to the source).  If we're ignoring ancestry, then any two
     nodes of the same type are related enough for us. */
  related = FALSE;
  if (s_entry && t_entry && s_entry->kind == t_entry->kind)
    {
      if (b->ignore_ancestry)
        related = TRUE;
      else
        {
          int distance;
          
          SVN_ERR(detect_relatedness(&distance, b->repos, s_entry, t_entry));
          if (distance == 0 && !any_path_info(b, e_path)
              && (requested_depth <= wc_depth || t_entry->kind == svn_node_file))
            {
              if (!info)
                return SVN_NO_ERROR;

              if (!info->start_empty)
                {
                  svn_lock_t *lock;

                  if (!info->lock_token)
                    return SVN_NO_ERROR;

                  SVN_ERR(get_lock(&lock, b->repos, t_path, pool));
                  if (lock && (strcmp(lock->token, info->lock_token) == 0))
                    return SVN_NO_ERROR;
                }
            }

          related = (distance != -1);
        }
    }

  /* If there's a source and it's not related to the target, nuke it. */
  if (s_entry && !related)
    {
      svn_revnum_t deleted_rev;

      SVN_ERR(find_deleted_rev(git_tree_owner(b->t_root), b->revmap,
                               t_path, s_rev, b->t_rev, &deleted_rev, pool));

      if (!SVN_IS_VALID_REVNUM(deleted_rev))
        {
          /* Two possibilities: either the thing doesn't exist in S_REV; or
             it wasn't deleted between S_REV and B->T_REV.  In the first case,
             I think we should leave DELETED_REV as SVN_INVALID_REVNUM, but
             in the second, it should be set to B->T_REV-1 for the call to
             delete_entry() below. */
          svn_node_kind_t kind;

          SVN_ERR(svn_ra_git__check_path(&kind, b->t_root, t_path));
          if (kind != svn_node_none)
            deleted_rev = b->t_rev - 1;
        }

      SVN_ERR(b->editor->delete_entry(e_path, deleted_rev, dir_baton,
                                      pool));
      s_path = NULL;
    }

  /* If there's no target, we have nothing more to do. */
  if (!t_entry)
    return svn_error_trace(skip_path_info(b, e_path));

  /* Check if the user is authorized to find out about the target. */
  SVN_ERR(check_auth(b, &allowed, t_path, pool));
  if (!allowed)
    {
      if (t_entry->kind == svn_node_dir)
        SVN_ERR(b->editor->absent_directory(e_path, dir_baton, pool));
      else
        SVN_ERR(b->editor->absent_file(e_path, dir_baton, pool));
      return svn_error_trace(skip_path_info(b, e_path));
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
                         info ? info->start_empty : FALSE,
                         wc_depth, requested_depth, pool));
      return svn_error_trace(b->editor->close_directory(new_baton, pool));
    }
  else
    {
      if (related)
        {
          SVN_ERR(b->editor->open_file(e_path, dir_baton, s_rev, pool,
                                       &new_baton));
          SVN_ERR(delta_files(b, new_baton, s_rev, s_path, t_path,
                              info ? info->lock_token : NULL, pool));
        }
      else
        {
          svn_revnum_t copyfrom_rev = SVN_INVALID_REVNUM;
          const char *copyfrom_path = NULL;
          SVN_ERR(add_file_smartly(b, e_path, dir_baton, t_path, &new_baton,
                                   &copyfrom_path, &copyfrom_rev, pool));
          if (! copyfrom_path)
            /* Send txdelta between empty file (s_path@s_rev doesn't
               exist) and added file (t_path@t_root). */
            SVN_ERR(delta_files(b, new_baton, s_rev, s_path, t_path,
                                info ? info->lock_token : NULL, pool));
          else
            /* Send txdelta between copied file (copyfrom_path@copyfrom_rev)
               and added file (tpath@t_root). */
            SVN_ERR(delta_files(b, new_baton, copyfrom_rev, copyfrom_path,
                                t_path, info ? info->lock_token : NULL, pool));
        }

      SVN_ERR(file_checksum(&checksum, svn_checksum_md5, b->t_root,
                            t_path, TRUE, pool));
      hex_digest = svn_checksum_to_cstring(checksum, pool);
      return svn_error_trace(b->editor->close_file(new_baton, hex_digest,
                                                   pool));
    }
}

static apr_status_t
cleanup_git_tree(void *data)
{
  git_tree *tree = data;
  git_tree_free(tree);
  return APR_SUCCESS;
}

static svn_error_t *
dir_entries(apr_hash_t **entries_p,
            git_repository *repos,
            git_tree *root,
            const char *path,
            apr_pool_t *pool)
{
  int i;
  git_tree *tree;
  int git_err;

  if (path[0] == '\0')
    tree = root;
  else
    {
      git_tree_entry *entry;

      git_err = git_tree_entry_bypath(&entry, root, path);
      if (git_err)
        return svn_error_trace(svn_ra_git__wrap_git_error());

      if (git_tree_entry_filemode(entry) == GIT_FILEMODE_COMMIT)
          return svn_error_createf(SVN_ERR_FS_NO_SUCH_ENTRY, NULL,
                                   _("'%s' is a git submodule but submodules "
                                     "are not yet supported"), path);

      if (git_tree_entry_type(entry) != GIT_OBJ_TREE)
        return svn_error_createf(SVN_ERR_FS_NOT_DIRECTORY, NULL, NULL);

      git_err = git_tree_entry_to_object((git_object **)&tree, repos, entry);
      if (git_err)
        return svn_error_trace(svn_ra_git__wrap_git_error());

      apr_pool_cleanup_register(pool, tree, cleanup_git_tree,
                                apr_pool_cleanup_null);

      git_tree_entry_free(entry);
    }

  *entries_p = apr_hash_make(pool);
  for (i = 0; i < git_tree_entrycount(tree); i++)
    {
      const git_tree_entry *e = git_tree_entry_byindex(tree, i);
      ra_git_dirent_t *dirent;

      if (git_tree_entry_filemode(e) == GIT_FILEMODE_COMMIT)
        continue; /* ### submodule, map to external */

      dirent = apr_pcalloc(pool, sizeof(*dirent));
      if (git_tree_entry_type(e) == GIT_OBJ_BLOB)
        dirent->kind = svn_node_file;
      else if (git_tree_entry_type(e) == GIT_OBJ_TREE)
        dirent->kind = svn_node_dir;
      else
        dirent->kind = svn_node_unknown;
      dirent->name = git_tree_entry_name(e);
      dirent->entry = (git_tree_entry *)e; /* ### const cast */
      dirent->parent_entry = NULL;
      svn_hash_sets(*entries_p, dirent->name, dirent);
    }

  return SVN_NO_ERROR;
}

/* A helper macro for when we have to recurse into subdirectories. */
#define DEPTH_BELOW_HERE(depth) ((depth) == svn_depth_immediates) ? \
                                 svn_depth_empty : (depth)

/* Emit edits within directory DIR_BATON (with corresponding path
   E_PATH) with the changes from the directory S_REV/S_PATH to the
   directory B->t_rev/T_PATH.  S_PATH may be NULL if the entry does
   not exist in the source.

   WC_DEPTH is this path's depth as reported by set_path/link_path.
   REQUESTED_DEPTH is derived from the depth set by
   svn_repos_begin_report().

   When iterating over this directory's entries, the following tables
   describe what happens for all possible combinations
   of WC_DEPTH/REQUESTED_DEPTH (rows represent WC_DEPTH, columns
   represent REQUESTED_DEPTH):

   Legend:
     X: ignore this entry (it's either below the requested depth, or
        if the requested depth is svn_depth_unknown, below the working
        copy depth)
     o: handle this entry normally
     U: handle the entry as if it were a newly added repository path
        (the client is upgrading to a deeper wc and doesn't currently
        have this entry, but it should be there after the upgrade, so we
        need to send the whole thing, not just deltas)

                              For files:
   ______________________________________________________________
   | req. depth| unknown | empty | files | immediates | infinity |
   |wc. depth  |         |       |       |            |          |
   |___________|_________|_______|_______|____________|__________|
   |empty      |    X    |   X   |   U   |     U      |    U     |
   |___________|_________|_______|_______|____________|__________|
   |files      |    o    |   X   |   o   |     o      |    o     |
   |___________|_________|_______|_______|____________|__________|
   |immediates |    o    |   X   |   o   |     o      |    o     |
   |___________|_________|_______|_______|____________|__________|
   |infinity   |    o    |   X   |   o   |     o      |    o     |
   |___________|_________|_______|_______|____________|__________|

                            For directories:
   ______________________________________________________________
   | req. depth| unknown | empty | files | immediates | infinity |
   |wc. depth  |         |       |       |            |          |
   |___________|_________|_______|_______|____________|__________|
   |empty      |    X    |   X   |   X   |     U      |    U     |
   |___________|_________|_______|_______|____________|__________|
   |files      |    X    |   X   |   X   |     U      |    U     |
   |___________|_________|_______|_______|____________|__________|
   |immediates |    o    |   X   |   X   |     o      |    o     |
   |___________|_________|_______|_______|____________|__________|
   |infinity   |    o    |   X   |   X   |     o      |    o     |
   |___________|_________|_______|_______|____________|__________|

   These rules are enforced by the is_depth_upgrade() function and by
   various other checks below.
*/
static svn_error_t *
delta_dirs(report_baton_t *b, svn_revnum_t s_rev, const char *s_path,
           const char *t_path, void *dir_baton, const char *e_path,
           svn_boolean_t start_empty, svn_depth_t wc_depth,
           svn_depth_t requested_depth, apr_pool_t *pool)
{
  apr_hash_t *s_entries = NULL, *t_entries;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Compare the property lists.  If we're starting empty, pass a NULL
     source path so that we add all the properties.

     When we support directory locks, we must pass the lock token here. */
  SVN_ERR(delta_proplists(b, s_rev, start_empty ? NULL : s_path, t_path,
                          NULL, change_dir_prop, dir_baton, subpool));
  svn_pool_clear(subpool);

  if (requested_depth > svn_depth_empty
      || requested_depth == svn_depth_unknown)
    {
      apr_pool_t *iterpool;

      /* Get the list of entries in each of source and target. */
      if (s_path && !start_empty)
        {
          git_tree *s_root;

          SVN_ERR(get_source_root(b, &s_root, s_rev));
          SVN_ERR(dir_entries(&s_entries, b->repos, s_root, s_path, subpool));
        }
      SVN_ERR(dir_entries(&t_entries, b->repos, b->t_root, t_path, subpool));

      /* Iterate over the report information for this directory. */
      iterpool = svn_pool_create(subpool);

      while (1)
        {
          path_info_t *info;
          const char *name, *s_fullpath, *t_fullpath, *e_fullpath;
          const ra_git_dirent_t *s_entry, *t_entry;

          svn_pool_clear(iterpool);
          SVN_ERR(fetch_path_info(b, &name, &info, e_path, iterpool));
          if (!name)
            break;

          /* Invalid revnum means we should delete, unless this is
             just an excluded subpath. */
          if (info
              && !SVN_IS_VALID_REVNUM(info->rev)
              && info->depth != svn_depth_exclude)
            {
              /* We want to perform deletes before non-replacement adds,
                 for graceful handling of case-only renames on
                 case-insensitive client filesystems.  So, if the report
                 item is a delete, remove the entry from the source hash,
                 but don't update the entry yet. */
              if (s_entries)
                svn_hash_sets(s_entries, name, NULL);

              svn_pool_destroy(info->pool);
              continue;
            }

          e_fullpath = svn_relpath_join(e_path, name, iterpool);
          t_fullpath = svn_relpath_join(t_path, name, iterpool);
          t_entry = svn_hash_gets(t_entries, name);
          s_fullpath = s_path ? svn_relpath_join(s_path, name, iterpool) : NULL;
          s_entry = s_entries ? svn_hash_gets(s_entries, name) : NULL;

          /* The only special cases where we don't process the entry are

             - When requested_depth is files but the reported path is
             a directory.  This is technically a client error, but we
             handle it anyway, by skipping the entry.

             - When the reported depth is svn_depth_exclude.
          */
          if (! ((requested_depth == svn_depth_files
                  && ((t_entry && t_entry->kind == svn_node_dir)
                      || (s_entry && s_entry->kind == svn_node_dir)))
                 || (info && info->depth == svn_depth_exclude)))
            SVN_ERR(update_entry(b, s_rev, s_fullpath, s_entry, t_fullpath,
                                 t_entry, dir_baton, e_fullpath, info,
                                 info ? info->depth
                                      : DEPTH_BELOW_HERE(wc_depth),
                                 DEPTH_BELOW_HERE(requested_depth), iterpool));

          /* Don't revisit this name in the target or source entries. */
          svn_hash_sets(t_entries, name, NULL);
          if (s_entries
              /* Keep the entry for later process if it is reported as
                 excluded and got deleted in repos. */
              && (! info || info->depth != svn_depth_exclude || t_entry))
            svn_hash_sets(s_entries, name, NULL);

          /* pathinfo entries live in their own subpools due to lookahead,
             so we need to clear each one out as we finish with it. */
          if (info)
            svn_pool_destroy(info->pool);
        }

      /* Remove any deleted entries.  Do this before processing the
         target, for graceful handling of case-only renames. */
      if (s_entries)
        {
          for (hi = apr_hash_first(subpool, s_entries);
               hi;
               hi = apr_hash_next(hi))
            {
              const ra_git_dirent_t *s_entry = svn__apr_hash_index_val(hi);

              svn_pool_clear(iterpool);

              if (svn_hash_gets(t_entries, s_entry->name) == NULL)
                {
                  const char *e_fullpath;
                  svn_revnum_t deleted_rev;

                  if (s_entry->kind == svn_node_file
                      && wc_depth < svn_depth_files)
                    continue;

                  if (s_entry->kind == svn_node_dir
                      && (wc_depth < svn_depth_immediates
                          || requested_depth == svn_depth_files))
                    continue;

                  /* There is no corresponding target entry, so delete. */
                  e_fullpath = svn_relpath_join(e_path, s_entry->name, iterpool);
                  SVN_ERR(find_deleted_rev(git_tree_owner(b->t_root),
                                           b->revmap,
                                           svn_relpath_join(t_path,
                                                            s_entry->name,
                                                            iterpool),
                                           s_rev, b->t_rev,
                                           &deleted_rev, iterpool));

                  SVN_ERR(b->editor->delete_entry(e_fullpath,
                                                  deleted_rev,
                                                  dir_baton, iterpool));
                }
            }
        }

      /* Loop over the dirents in the target. */
      for (hi = apr_hash_first(subpool, t_entries);
           hi;
           hi = apr_hash_next(hi))
        {
          const ra_git_dirent_t *t_entry = svn__apr_hash_index_val(hi);
          const ra_git_dirent_t *s_entry;
          const char *s_fullpath, *t_fullpath, *e_fullpath;

          svn_pool_clear(iterpool);

          if (is_depth_upgrade(wc_depth, requested_depth, t_entry->kind))
            {
              /* We're making the working copy deeper, pretend the source
                 doesn't exist. */
              s_entry = NULL;
              s_fullpath = NULL;
            }
          else
            {
              if (t_entry->kind == svn_node_file
                  && requested_depth == svn_depth_unknown
                  && wc_depth < svn_depth_files)
                continue;

              if (t_entry->kind == svn_node_dir
                  && (wc_depth < svn_depth_immediates
                      || requested_depth == svn_depth_files))
                continue;

              /* Look for an entry with the same name in the source dirents. */
              s_entry = s_entries ?
                  svn_hash_gets(s_entries, t_entry->name) : NULL;
              s_fullpath = s_entry ?
                  svn_relpath_join(s_path, t_entry->name, iterpool) : NULL;
            }

          /* Compose the report, editor, and target paths for this entry. */
          e_fullpath = svn_relpath_join(e_path, t_entry->name, iterpool);
          t_fullpath = svn_relpath_join(t_path, t_entry->name, iterpool);

          SVN_ERR(update_entry(b, s_rev, s_fullpath, s_entry, t_fullpath,
                               t_entry, dir_baton, e_fullpath, NULL,
                               DEPTH_BELOW_HERE(wc_depth),
                               DEPTH_BELOW_HERE(requested_depth),
                               iterpool));
        }

      /* iterpool is destroyed by destroying its parent (subpool) below */
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
drive(report_baton_t *b, svn_revnum_t s_rev, path_info_t *info,
      apr_pool_t *pool)
{
  const char *t_anchor, *s_fullpath;
  svn_boolean_t allowed, info_is_set_path;
  git_tree *s_root;
  const ra_git_dirent_t *s_entry, *t_entry;
  void *root_baton;

  /* Compute the target path corresponding to the working copy anchor,
     and check its authorization. */
  t_anchor = *b->s_operand ? svn_relpath_dirname(b->t_path, pool) : b->t_path;
  SVN_ERR(check_auth(b, &allowed, t_anchor, pool));
  if (!allowed)
    return svn_error_create
      (SVN_ERR_AUTHZ_ROOT_UNREADABLE, NULL,
       _("Not authorized to open root of edit operation"));

  /* Collect information about the source and target nodes. */
  s_fullpath = svn_relpath_join(b->fs_base, b->s_operand, pool);
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
    return svn_error_createf(SVN_ERR_FS_PATH_SYNTAX, NULL,
                             _("Target path '%s' does not exist"),
                             b->t_path);

  /* If the anchor is the operand, the source and target must be dirs.
     Check this before opening the root to avoid modifying the wc. */
  else if (!*b->s_operand && (!s_entry || s_entry->kind != svn_node_dir
                              || t_entry->kind != svn_node_dir))
    return svn_error_create(SVN_ERR_FS_PATH_SYNTAX, NULL,
                            _("Cannot replace a directory from within"));

  SVN_ERR(b->editor->set_target_revision(b->edit_baton, b->t_rev, pool));
  SVN_ERR(b->editor->open_root(b->edit_baton, s_rev, pool, &root_baton));

  /* If the anchor is the operand, diff the two directories; otherwise
     update the operand within the anchor directory. */
  if (!*b->s_operand)
    SVN_ERR(delta_dirs(b, s_rev, s_fullpath, b->t_path, root_baton,
                       "", info->start_empty, info->depth, b->requested_depth,
                       pool));
  else
    SVN_ERR(update_entry(b, s_rev, s_fullpath, s_entry, b->t_path,
                         t_entry, root_baton, b->s_operand, info,
                         info->depth, b->requested_depth, pool));

  return svn_error_trace(b->editor->close_directory(root_baton, pool));
}

/* Initialize the baton fields for editor-driving, and drive the editor. */
static svn_error_t *
finish_report(report_baton_t *b, apr_pool_t *pool)
{
  path_info_t *info;
  apr_pool_t *subpool;
  svn_revnum_t s_rev;

  /* Save our pool to manage the lookahead and fs_root cache with. */
  b->pool = pool;

  /* Add the end marker. */
  SVN_ERR(svn_spillbuf__reader_write(b->reader, "-", 1, pool));

  /* Read the first pathinfo from the report and verify that it is a top-level
     set_path entry. */
  SVN_ERR(read_path_info(&info, b->reader, pool));
  if (!info || strcmp(info->path, b->s_operand) != 0
      || info->link_path || !SVN_IS_VALID_REVNUM(info->rev))
    return svn_error_create(SVN_ERR_REPOS_BAD_REVISION_REPORT, NULL,
                            _("Invalid report for top level of working copy"));
  s_rev = info->rev;

  /* Initialize the lookahead pathinfo. */
  subpool = svn_pool_create(pool);
  SVN_ERR(read_path_info(&b->lookahead, b->reader, subpool));

  if (b->lookahead && strcmp(b->lookahead->path, b->s_operand) == 0)
    {
      /* If the operand of the wc operation is switched or deleted,
         then info above is just a place-holder, and the only thing we
         have to do is pass the revision it contains to open_root.
         The next pathinfo actually describes the target. */
      if (!*b->s_operand)
        return svn_error_create(SVN_ERR_REPOS_BAD_REVISION_REPORT, NULL,
                                _("Two top-level reports with no target"));
      /* If the client issued a set-path followed by a delete-path, we need
         to respect the depth set by the initial set-path. */
      if (! SVN_IS_VALID_REVNUM(b->lookahead->rev))
        {
          b->lookahead->depth = info->depth;
        }
      info = b->lookahead;
      SVN_ERR(read_path_info(&b->lookahead, b->reader, subpool));
    }

  /* Open the target root and initialize the source root cache. */
  SVN_ERR(fetch_revision_root(&b->t_root, b->repos, b->revmap, b->t_rev));
  b->s_root = NULL;
  b->s_root_revision = SVN_INVALID_REVNUM;

  {
    svn_error_t *err = svn_error_trace(drive(b, s_rev, info, pool));

    if (err == SVN_NO_ERROR)
      return svn_error_trace(b->editor->close_edit(b->edit_baton, pool));

    return svn_error_trace(
                svn_error_compose_create(err,
                                         b->editor->abort_edit(b->edit_baton,
                                                               pool)));
  }

  git_tree_free(b->t_root);
}

/* --- COLLECTING THE REPORT INFORMATION --- */

/* Record a report operation into the spill buffer.  Return an error
   if DEPTH is svn_depth_unknown. */
static svn_error_t *
write_path_info(report_baton_t *b, const char *path, const char *lpath,
                svn_revnum_t rev, svn_depth_t depth,
                svn_boolean_t start_empty,
                const char *lock_token, apr_pool_t *pool)
{
  const char *lrep, *rrep, *drep, *ltrep, *rep;

  /* Munge the path to be anchor-relative, so that we can use edit paths
     as report paths. */
  path = svn_relpath_join(b->s_operand, path, pool);

  lrep = lpath ? apr_psprintf(pool, "+%" APR_SIZE_T_FMT ":%s",
                              strlen(lpath), lpath) : "-";
  rrep = (SVN_IS_VALID_REVNUM(rev)) ?
    apr_psprintf(pool, "+%ld:", rev) : "-";

  if (depth == svn_depth_exclude)
    drep = "+X";
  else if (depth == svn_depth_empty)
    drep = "+E";
  else if (depth == svn_depth_files)
    drep = "+F";
  else if (depth == svn_depth_immediates)
    drep = "+M";
  else if (depth == svn_depth_infinity)
    drep = "-";
  else
    return svn_error_createf(SVN_ERR_REPOS_BAD_ARGS, NULL,
                             _("Unsupported report depth '%s'"),
                             svn_depth_to_word(depth));

  ltrep = lock_token ? apr_psprintf(pool, "+%" APR_SIZE_T_FMT ":%s",
                                    strlen(lock_token), lock_token) : "-";
  rep = apr_psprintf(pool, "+%" APR_SIZE_T_FMT ":%s%s%s%s%c%s",
                     strlen(path), path, lrep, rrep, drep,
                     start_empty ? '+' : '-', ltrep);
  return svn_error_trace(
            svn_spillbuf__reader_write(b->reader, rep, strlen(rep), pool));
}

svn_error_t *
svn_ra_git__reporter_set_path(void *baton, const char *path, svn_revnum_t rev,
                              svn_depth_t depth, svn_boolean_t start_empty,
                              const char *lock_token, apr_pool_t *pool)
{
  return svn_error_trace(
            write_path_info(baton, path, NULL, rev, depth, start_empty,
                            lock_token, pool));
}

svn_error_t *
svn_ra_git__reporter_link_path(void *baton, const char *path,
                               const char *link_path,
                               svn_revnum_t rev, svn_depth_t depth,
                               svn_boolean_t start_empty,
                               const char *lock_token, apr_pool_t *pool)
{
  if (depth == svn_depth_exclude)
    return svn_error_create(SVN_ERR_REPOS_BAD_ARGS, NULL,
                            _("Depth 'exclude' not supported for link"));

  return svn_error_trace(
            write_path_info(baton, path, link_path, rev, depth,
                            start_empty, lock_token, pool));
}

svn_error_t *
svn_ra_git__reporter_delete_path(void *baton, const char *path, apr_pool_t *pool)
{
  /* We pass svn_depth_infinity because deletion of a path always
     deletes everything underneath it. */
  return svn_error_trace(
            write_path_info(baton, path, NULL, SVN_INVALID_REVNUM,
                            svn_depth_infinity, FALSE, NULL, pool));
}

svn_error_t *
svn_ra_git__reporter_finish_report(void *baton, apr_pool_t *pool)
{
  report_baton_t *b = baton;

  return svn_error_trace(finish_report(b, pool));
}

svn_error_t *
svn_ra_git__reporter_abort_report(void *baton, apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

/* --- BEGINNING THE REPORT --- */


svn_error_t *
svn_ra_git__reporter_begin_report(void **report_baton,
                                  svn_revnum_t revnum,
                                  git_repository *repos,
                                  apr_hash_t *revmap,
                                  const char *fs_base,
                                  const char *s_operand,
                                  const char *switch_path,
                                  svn_boolean_t text_deltas,
                                  svn_depth_t depth,
                                  svn_boolean_t ignore_ancestry,
                                  svn_boolean_t send_copyfrom_args,
                                  const svn_delta_editor_t *editor,
                                  void *edit_baton,
                                  apr_size_t zero_copy_limit,
                                  apr_pool_t *pool)
{
  report_baton_t *b;

  if (depth == svn_depth_exclude)
    return svn_error_create(SVN_ERR_REPOS_BAD_ARGS, NULL,
                            _("Request depth 'exclude' not supported"));

  /* Build a reporter baton.  Copy strings in case the caller doesn't
     keep track of them. */
  b = apr_palloc(pool, sizeof(*b));
  b->repos = repos;
  b->revmap = revmap;
  b->fs_base = svn_relpath_canonicalize(fs_base, pool);
  b->s_operand = apr_pstrdup(pool, s_operand);
  b->t_rev = revnum;
  b->t_path = switch_path ? svn_relpath_canonicalize(switch_path, pool)
                          : svn_relpath_join(b->fs_base, s_operand, pool);
  b->text_deltas = text_deltas;
  b->requested_depth = depth;
  b->ignore_ancestry = ignore_ancestry;
  b->send_copyfrom_args = send_copyfrom_args;
  b->is_switch = (switch_path != NULL);
  b->editor = editor;
  b->edit_baton = edit_baton;
  b->revision_infos = apr_hash_make(pool);
  b->pool = pool;
  b->reader = svn_spillbuf__reader_create(1000 /* blocksize */,
                                          1000000 /* maxsize */,
                                          pool);
  b->repos_uuid = svn_string_create(RA_GIT_UUID, pool);

  /* Hand reporter back to client. */
  *report_baton = b;
  return SVN_NO_ERROR;
}
