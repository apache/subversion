#include <stdlib.h>
#include <assert.h>

#include <svn_types.h>
#include <svn_pools.h>
#include <svn_path.h>
#include <svn_hash.h>
#include <svn_md5.h>
#include <svn_repos.h>

struct rep_pointer
{
  svn_revnum_t rev;
  apr_off_t off;
  apr_off_t len;       /* Serves for both the expanded and rep size */
  const char *digest;
};

struct entry
{
  apr_hash_t *children;  /* NULL for files */
  svn_boolean_t children_changed;
  apr_hash_t *props;
  apr_pool_t *props_pool;
  struct rep_pointer text_rep;
  struct rep_pointer props_rep;
  svn_revnum_t node_rev;
  apr_off_t node_off;
  int pred_count;
  struct entry *pred;
  int node_id;
  int copy_id;
  const char *created_path;
  svn_revnum_t copyfrom_rev;
  const char *copyfrom_path;
  struct entry *copyroot;
};

struct parse_baton
{
  svn_revnum_t txn_rev;
  apr_array_header_t *roots;
  struct entry *current_node;
  svn_revnum_t current_rev;
  apr_pool_t *rev_pool;
  apr_file_t *rev_file;
  svn_stream_t *rev_stream;
  svn_stream_t *delta_stream;
  apr_hash_t *deleted_paths;
  apr_hash_t *added_paths;
  apr_hash_t *modified_paths;
  apr_hash_t *rev_props;
  apr_md5_ctx_t md5_ctx;
  int next_node_id;
  int next_copy_id;
  svn_stream_t *empty_stream;
  apr_pool_t *pool;
};

/* XXX Replace this with svn_hash_write2 when it's present. */
svn_error_t *
hash_write(apr_hash_t *hash, svn_stream_t *out, apr_pool_t *pool)
{
  apr_hash_index_t *this;      /* current hash entry */
  apr_pool_t *subpool;
  apr_size_t len;

  subpool = svn_pool_create(pool);
  for (this = apr_hash_first(pool, hash); this; this = apr_hash_next(this))
    {
      const void *key;
      void *val;
      apr_ssize_t keylen;
      const svn_string_t *str;

      svn_pool_clear(subpool);

      /* Get this key and val. */
      apr_hash_this(this, &key, &keylen, &val);

      /* Output name length, then name. */
      SVN_ERR(svn_stream_printf(out, subpool, "K %" APR_SSIZE_T_FMT "\n",
                                keylen));
      len = keylen;
      SVN_ERR(svn_stream_write(out, key, &len));

      /* Output value length, then value. */
      str = val;
      SVN_ERR(svn_stream_printf(out, subpool, "\nV %" APR_SIZE_T_FMT "\n",
                                str->len));
      len = str->len;
      SVN_ERR(svn_stream_write(out, str->data, &len));
      len = 1;
      SVN_ERR(svn_stream_write(out, "\n", &len));
    }

  svn_pool_destroy (subpool);

  len = 4;
  SVN_ERR(svn_stream_write(out, "END\n", &len));

  return SVN_NO_ERROR;
}

static void
init_rep(struct rep_pointer *rep)
{
  rep->rev = SVN_INVALID_REVNUM;
  rep->off = -1;
  rep->len = -1;
  rep->digest = NULL;
}

static struct entry *
new_entry(apr_pool_t *pool)
{
  struct entry *entry;

  entry = apr_palloc(pool, sizeof(*entry));
  entry->children = NULL;
  entry->children_changed = FALSE;
  entry->props = NULL;
  init_rep(&entry->text_rep);
  init_rep(&entry->props_rep);
  entry->node_rev = SVN_INVALID_REVNUM;
  entry->node_off = -1;
  entry->pred_count = 0;
  entry->pred = NULL;
  entry->node_id = -1;
  entry->copy_id = -1;
  entry->created_path = NULL;
  entry->copyfrom_rev = SVN_INVALID_REVNUM;
  entry->copyfrom_path = NULL;
  entry->copyroot = NULL;
  return entry;
}

static struct entry *
get_root(struct parse_baton *pb, svn_revnum_t rev)
{
  return APR_ARRAY_IDX(pb->roots, rev, struct entry *);
}

/* Find the entry for PATH under the root ENTRY.  Do not create copies
   for the current rev; this is for looking up copy history. */
static struct entry *
find_entry(struct entry *entry, const char *path, apr_pool_t *pool)
{
  apr_array_header_t *elems;
  int i;
  const char *name;

  elems = svn_path_decompose(path, pool);
  for (i = 0; i < elems->nelts; i++)
    {
      name = APR_ARRAY_IDX(elems, i, const char *);
      assert(entry->children);
      entry = apr_hash_get(entry->children, name, APR_HASH_KEY_STRING);
      assert(entry);
    }
  return entry;
}

/* Initialize new_entry from the fields of old_entry, tweaking them as
   appropriate for a modification.  (Further changes will be needed
   for copy operations.) */
static void
copy_entry(struct parse_baton *pb, struct entry *new_entry,
           struct entry *old_entry)
{
  *new_entry = *old_entry;
  if (new_entry->children)
    {
      new_entry->children = apr_hash_copy(pb->pool, old_entry->children);
      new_entry->children_changed = FALSE;
    }
  new_entry->node_rev = pb->current_rev;
  new_entry->node_off = -1;
  new_entry->pred_count = old_entry->pred_count + 1;
  new_entry->pred = old_entry;
  new_entry->copyfrom_rev = SVN_INVALID_REVNUM;
  new_entry->copyfrom_path = NULL;
}

/* Get the child entry for NAME under ENTRY, copying it for the current
   rev if necessary.  Use POOL only for temporary allocations. */
static struct entry *
get_child(struct parse_baton *pb, struct entry *entry, const char *name,
          apr_pool_t *pool)
{
  struct entry *child, *new_child;
  const char *path;

  assert(entry->children);
  child = apr_hash_get(entry->children, name, APR_HASH_KEY_STRING);
  assert(child);
  if (child->node_rev != pb->current_rev)
    {
      /* We need to make a copy of child for this revision. */
      new_child = new_entry(pb->pool);
      copy_entry(pb, new_child, child);
      path = svn_path_join(entry->created_path, name, pb->pool);
      new_child->created_path = path;

      /* We need to assign a copy-id to the new child.  The rules:
         - If child is not derived from a copy, we inherit from the
           parent.  (Often this means keeping the same copy-id as
           child has; if parent has a different copy-id, then this is
           the "lazy" copy of the child onto the parent's branch.)
         - If child is derived from a copy and we are accessing it
           through its created path, then we don't change the copy ID.
         - If child is derived from a copy and we are not accessing
           it through its created path, then we create a "soft copy"
           with a fresh copy ID.  Unlike true copies, we do not assign
           copy history and we inherit copy root information from the
           predecessor. */
      if (child->node_id != child->copyroot->node_id)
        {
          new_child->copy_id = entry->copy_id;
          new_child->copyroot = entry->copyroot;
        }
      else if (strcmp(child->created_path, new_child->created_path) != 0)
        new_child->copy_id = pb->next_copy_id++;

      name = apr_pstrdup(pb->pool, name);
      apr_hash_set(entry->children, name, APR_HASH_KEY_STRING, new_child);
      entry->children_changed = TRUE;
      child = new_child;
    }
  return child;
}

/* Get the entry for PATH in the current rev of PB.  Only use POOL
   only for temporary allocations. */
static struct entry *
follow_path(struct parse_baton *pb, const char *path, apr_pool_t *pool)
{
  apr_array_header_t *elems;
  int i;
  struct entry *entry;

  entry = get_root(pb, pb->current_rev);
  elems = svn_path_decompose(path, pool);
  for (i = 0; i < elems->nelts; i++)
    entry = get_child(pb, entry, APR_ARRAY_IDX(elems, i, const char *), pool);
  return entry;
}

/* Return the node-rev ID of ENTRY in string form. */
static const char *
node_rev_id(struct entry *entry, apr_pool_t *pool)
{
  return apr_psprintf(pool, "%d.%d.r%" SVN_REVNUM_T_FMT "/%" APR_OFF_T_FMT,
                      entry->node_id, entry->copy_id, entry->node_rev,
                      entry->node_off);
}

/* Return the string form of a rep pointer as used in a node-rev field. */
static const char *
repstr(struct rep_pointer *rep, apr_pool_t *pool)
{
  return apr_psprintf(pool, "%" SVN_REVNUM_T_FMT " %" APR_OFF_T_FMT
                      " %" APR_OFF_T_FMT " %" APR_OFF_T_FMT " %s",
                      rep->rev, rep->off, rep->len, rep->len, rep->digest);
}

static void
get_node_info(apr_hash_t *headers, const char **path, svn_node_kind_t *kind,
              enum svn_node_action *action, svn_revnum_t *copyfrom_rev,
              const char **copyfrom_path)
{
  const char *val;

  /* Then add info from the headers.  */
  assert(*path = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_PATH,
                              APR_HASH_KEY_STRING));

  val = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_KIND,
                     APR_HASH_KEY_STRING);
  *kind = !val ? svn_node_unknown
    : (strcmp(val, "file") == 0) ? svn_node_file : svn_node_dir;

  assert(val = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_ACTION,
                            APR_HASH_KEY_STRING));
  if (strcmp(val, "change") == 0)
    *action = svn_node_action_change;
  else if (strcmp(val, "add") == 0)
    *action = svn_node_action_add;
  else if (strcmp(val, "delete") == 0)
    *action = svn_node_action_delete;
  else if (strcmp(val, "replace") == 0)
    *action = svn_node_action_replace;
  else
    abort();

  val = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV,
                     APR_HASH_KEY_STRING);
  *copyfrom_rev = val ? SVN_STR_TO_REV(val) : SVN_INVALID_REVNUM;

  *copyfrom_path = apr_hash_get(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH,
                                APR_HASH_KEY_STRING);
}

static svn_error_t *
write_hash_rep(struct parse_baton *pb, apr_hash_t *hash,
               struct rep_pointer *rep, apr_pool_t *pool)
{
  svn_stringbuf_t *buf;
  svn_stream_t *stream;
  unsigned char md5buf[APR_MD5_DIGESTSIZE];

  /* Record the rev file offset of the rep. */
  rep->rev = pb->current_rev;
  rep->off = 0;
  SVN_ERR(svn_io_file_seek(pb->rev_file, APR_CUR, &rep->off, pool));

  /* Write out a rep header. */
  svn_stream_printf(pb->rev_stream, pool, "PLAIN\n");

  /* Marshal the hash to a stringbuf. */
  buf = svn_stringbuf_create("", pool);
  stream = svn_stream_from_stringbuf(buf, pool);
  hash_write(hash, stream, pool);

  /* Record the MD5 digest of the marshalled hash. */
  apr_md5(md5buf, buf->data, buf->len);
  rep->digest = svn_md5_digest_to_cstring(md5buf, pb->pool);

  /* Write the marshalled hash out to the rev file. */
  SVN_ERR(svn_io_file_write_full(pb->rev_file, buf->data, buf->len,
                                 NULL, pool));

  /* Record the length of the props data. */
  rep->len = buf->len;

  SVN_ERR(svn_stream_printf(pb->rev_stream, pool, "ENDREP\n"));
  return SVN_NO_ERROR;
}

/* Convert a directory's children map into a dumpable hash map. */
static apr_hash_t *
children_to_dirmap(apr_hash_t *children, apr_pool_t *pool)
{
  apr_hash_t *tmpmap;
  apr_hash_index_t *hi;
  const void *key;
  void *val;
  struct entry *child;
  const char *rep;

  tmpmap = apr_hash_copy(pool, children);
  for (hi = apr_hash_first(pool, tmpmap); hi; hi = apr_hash_next(hi))
    {
      apr_hash_this(hi, &key, NULL, &val);
      child = val;
      rep = apr_psprintf(pool, "%s %s",
                         (child->children == NULL) ? "file" : "dir",
                         node_rev_id(child, pool));
      apr_hash_set(tmpmap, key, APR_HASH_KEY_STRING,
                   svn_string_create(rep, pool));
    }
  return tmpmap;
}

static svn_error_t *
write_props(struct parse_baton *pb, struct entry *entry, apr_pool_t *pool)
{
  SVN_ERR(write_hash_rep(pb, entry->props, &entry->props_rep, pool));

  /* We don't need the props hash any more. */
  entry->props = NULL;
  svn_pool_destroy(entry->props_pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
write_field(svn_stream_t *out, apr_pool_t *pool, const char *name,
            const char *fmt, ...)
{
  const char *val;
  va_list ap;

  /* Format the value. */
  va_start(ap, fmt);
  val = apr_pvsprintf(pool, fmt, ap);
  va_end(ap);

  return svn_stream_printf(out, pool, "%s: %s\n", name, val);
}

static svn_error_t *
write_node_rev(struct parse_baton *pb, struct entry *entry, apr_pool_t *pool)
{
  svn_stream_t *out = pb->rev_stream;

  /* Get the rev file offset of the node-rev. */
  entry->node_off = 0;
  SVN_ERR(svn_io_file_seek(pb->rev_file, APR_CUR, &entry->node_off, pool));

  SVN_ERR(write_field(out, pool, "id", "%s", node_rev_id(entry, pool)));
  SVN_ERR(write_field(out, pool, "type", entry->children ? "dir" : "file"));
  if (entry->pred)
    SVN_ERR(write_field(out, pool, "pred", "%s",
                        node_rev_id(entry->pred, pool)));
  SVN_ERR(write_field(out, pool, "count", "%d", entry->pred_count));
  SVN_ERR(write_field(out, pool, "text", "%s",
                      repstr(&entry->text_rep, pool)));
  if (SVN_IS_VALID_REVNUM(entry->props_rep.rev))
    SVN_ERR(write_field(out, pool, "props", "%s",
                        repstr(&entry->props_rep, pool)));
  SVN_ERR(write_field(out, pool, "cpath", "%s", entry->created_path));
  if (SVN_IS_VALID_REVNUM(entry->copyfrom_rev))
    SVN_ERR(write_field(out, pool, "copyfrom", "%" SVN_REVNUM_T_FMT " %s",
                        entry->copyfrom_rev, entry->copyfrom_path));
  if (entry->copyroot != entry)
    SVN_ERR(write_field(out, pool, "copyroot", "%" SVN_REVNUM_T_FMT " %s",
                        entry->copyroot->node_rev,
                        entry->copyroot->created_path));
  SVN_ERR(svn_stream_printf(out, pool, "\n"));

  return SVN_NO_ERROR;
}

static svn_error_t *
write_entry(struct parse_baton *pb, struct entry *entry, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  void *val;
  apr_pool_t *subpool;

  /* We can prune here if this node was not copied for the current rev. */
  if (entry->node_rev != pb->current_rev)
    return SVN_NO_ERROR;

  if (entry->children)
    {
      /* This is a directory; write out all the changed child entries. */
      subpool = svn_pool_create(pool);
      for (hi = apr_hash_first(pool, entry->children); hi;
           hi = apr_hash_next(hi))
        {
          svn_pool_clear(subpool);
          apr_hash_this(hi, NULL, NULL, &val);
          SVN_ERR(write_entry(pb, val, subpool));
        }
      svn_pool_destroy(subpool);

      if (entry->children_changed)
        SVN_ERR(write_hash_rep(pb, children_to_dirmap(entry->children, pool),
                               &entry->text_rep, pool));
    }

  if (entry->props)
    {
      if (apr_hash_count(entry->props) == 0)
        entry->props_rep.rev = SVN_INVALID_REVNUM;
      else
        SVN_ERR(write_props(pb, entry, pool));
    }

  if (entry->node_rev == pb->current_rev)
    SVN_ERR(write_node_rev(pb, entry, pool));

  return SVN_NO_ERROR;
}

/* Return the string form of a changed-path entry. */
static svn_error_t *
write_change(svn_stream_t *out, const char *path, struct entry *entry,
             const char *action, apr_pool_t *pool)
{
  svn_boolean_t text_mod = FALSE, props_mod = FALSE;

  if (strcmp(action, "delete") != 0)
    {
      text_mod = (entry->text_rep.rev == entry->node_rev);
      props_mod = (entry->props_rep.rev == entry->node_rev);
    }
  return svn_stream_printf(out, pool, "%s %s %s %s %s\n",
                           node_rev_id(entry, pool), action,
                           text_mod ? "true" : "false",
                           props_mod ? "true" : "false", path);
}

static svn_error_t *
write_changed_path_data(struct parse_baton *pb, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  const void *key;
  void *val;
  const char *action;
  svn_stream_t *out = pb->rev_stream;

  for (hi = apr_hash_first(pool, pb->added_paths); hi; hi = apr_hash_next(hi))
    {
      apr_hash_this(hi, &key, NULL, &val);
      if (apr_hash_get(pb->deleted_paths, key, APR_HASH_KEY_STRING))
        {
          apr_hash_set(pb->deleted_paths, key, APR_HASH_KEY_STRING, NULL);
          action = "replace";
        }
      else
        action = "add";
      SVN_ERR(write_change(out, key, val, action, pool));
    }
  for (hi = apr_hash_first(pool, pb->deleted_paths); hi;
       hi = apr_hash_next(hi))
    {
      apr_hash_this(hi, &key, NULL, &val);
      SVN_ERR(write_change(out, key, val, "delete", pool));
    }
  for (hi = apr_hash_first(pool, pb->modified_paths); hi;
       hi = apr_hash_next(hi))
    {
      apr_hash_this(hi, &key, NULL, &val);
      SVN_ERR(write_change(out, key, val, "modify", pool));
    }
  return SVN_NO_ERROR;
}

/* Dump a hash to PATH. */
static svn_error_t *
write_hash_to_file(apr_hash_t *hash, const char *path, apr_pool_t *pool)
{
  apr_file_t *file;
  svn_stream_t *stream;

  SVN_ERR(svn_io_file_open(&file, path,
                           APR_WRITE|APR_CREATE|APR_TRUNCATE|APR_BUFFERED,
                           APR_OS_DEFAULT, pool));
  stream = svn_stream_from_aprfile(file, pool);
  SVN_ERR(hash_write(hash, stream, pool));
  SVN_ERR(svn_io_file_close(file, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
write_current(struct parse_baton *pb, apr_pool_t *pool)
{
  apr_file_t *current_file;
  const char *str;

  SVN_ERR(svn_io_file_open(&current_file, "current",
                           APR_WRITE|APR_CREATE|APR_TRUNCATE|APR_BUFFERED,
                           APR_OS_DEFAULT, pool));
  str = apr_psprintf(pool, "%" SVN_REVNUM_T_FMT " %d %d\n", pb->current_rev,
                     pb->next_node_id, pb->next_copy_id);
  SVN_ERR(svn_io_file_write_full(current_file, str, strlen(str), NULL, pool));
  SVN_ERR(svn_io_file_close(current_file, pool));
  return SVN_NO_ERROR;
}

static const char *
txn_node_rev_id(struct parse_baton *pb, struct entry *entry, apr_pool_t *pool)
{
  const char *node_id, *copy_id, *txn_id;

  node_id = (entry->node_id < 0) ? apr_psprintf(pool, "_%d", -entry->node_id)
    : apr_psprintf(pool, "%d", entry->node_id);
  copy_id = (entry->copy_id < 0) ? apr_psprintf(pool, "_%d", -entry->copy_id)
    : apr_psprintf(pool, "%d", entry->copy_id);
  txn_id = (entry->node_rev == pb->current_rev) ? "t0" :
      apr_psprintf(pool, "r%" SVN_REVNUM_T_FMT "/%" APR_OFF_T_FMT,
                   entry->node_rev, entry->node_off);
  return apr_psprintf(pool, "%s.%s.%s", node_id, copy_id, txn_id);
}

static const char *
txn_revstr(struct parse_baton *pb, svn_revnum_t rev, apr_pool_t *pool)
{
  return (rev == pb->current_rev) ? "-1"
    : apr_psprintf(pool, "%" SVN_REVNUM_T_FMT, rev);
}

static const char *
txn_repstr(struct parse_baton *pb, struct rep_pointer *rep,
           svn_boolean_t only_this, apr_pool_t *pool)
{
  if (rep->rev == pb->current_rev && only_this)
    return "-1";

  return apr_psprintf(pool, "%s %" APR_OFF_T_FMT " %" APR_OFF_T_FMT
                      " %" APR_OFF_T_FMT " %s",
                      txn_revstr(pb, rep->rev, pool),
                      rep->off, rep->len, rep->len, rep->digest);
}

static svn_error_t *
write_txn_dir_children(struct parse_baton *pb, struct entry *entry,
                       const char *nrpath, apr_pool_t *pool)
{
  apr_hash_t *oldmap, *newmap;
  const char *path;
  apr_file_t *children_file;
  svn_stream_t *out;
  apr_hash_index_t *hi;
  const void *key;
  void *val;
  struct entry *child;
  const char *name, *rep;

  path = apr_psprintf(pool, "%s.children", nrpath);
  SVN_ERR(svn_io_file_open(&children_file, path,
                           APR_WRITE|APR_CREATE|APR_TRUNCATE|APR_BUFFERED,
                           APR_OS_DEFAULT, pool));
  out = svn_stream_from_aprfile(children_file, pool);

  oldmap = entry->pred ? entry->pred->children : apr_hash_make(pool);
  newmap = entry->children;

  /* Dump the old directory contents. */
  SVN_ERR(hash_write(children_to_dirmap(oldmap, pool), out, pool));

  /* Dump an entry for each deletion. */
  for (hi = apr_hash_first(pool, oldmap); hi; hi = apr_hash_next(hi))
    {
      apr_hash_this(hi, &key, NULL, NULL);
      name = key;
      if (apr_hash_get(newmap, key, APR_HASH_KEY_STRING) == NULL)
        SVN_ERR(svn_stream_printf(out, pool, "D %" APR_SIZE_T_FMT "\n%s\n",
                                  strlen(name), name));
    }

  /* Dump an entry for each change or addition. */
  for (hi = apr_hash_first(pool, newmap); hi; hi = apr_hash_next(hi))
    {
      apr_hash_this(hi, &key, NULL, &val);
      name = key;
      if (apr_hash_get(oldmap, name, APR_HASH_KEY_STRING) != val)
        {
          child = val;
          rep = apr_psprintf(pool, "%s %s",
                             (child->children == NULL) ? "file" : "dir",
                             txn_node_rev_id(pb, child, pool));
          SVN_ERR(svn_stream_printf(out, pool, "K %" APR_SIZE_T_FMT "\n%s\n"
                                    "V %" APR_SIZE_T_FMT "\n%s\n",
                                    strlen(name), name, strlen(rep), rep));
        }
    }

  SVN_ERR(svn_io_file_close(children_file, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
dump_txn_node_rev(struct parse_baton *pb, struct entry *entry,
                  apr_pool_t *pool)
{
  apr_pool_t *subpool;
  apr_hash_index_t *hi;
  void *val;
  const char *nrpath, *path;
  apr_file_t *nrfile;
  svn_stream_t *out;

  if (entry->node_rev != pb->current_rev)
    return SVN_NO_ERROR;

  nrpath = txn_node_rev_id(pb, entry, pool);
  *strrchr(nrpath, '.') = '\0';
  nrpath = apr_psprintf(pool, "transactions/0/%s", nrpath);

  if (entry->children)
    {
      subpool = svn_pool_create(pool);
      for (hi = apr_hash_first(pool, entry->children); hi;
           hi = apr_hash_next(hi))
        {
          svn_pool_clear(subpool);
          apr_hash_this(hi, NULL, NULL, &val);
          SVN_ERR(dump_txn_node_rev(pb, val, subpool));
        }

      if (entry->children_changed)
        {
          SVN_ERR(write_txn_dir_children(pb, entry, nrpath, pool));
          entry->text_rep.rev = pb->current_rev;
        }
    }

  if (entry->props)
    {
      if (apr_hash_count(entry->props) == 0)
        entry->props_rep.rev = SVN_INVALID_REVNUM;
      else
        {
          path = apr_psprintf(pool, "%s.props", nrpath);
          SVN_ERR(write_hash_to_file(entry->props, path, pool));
          entry->props_rep.rev = pb->current_rev;
        }
    }

  SVN_ERR(svn_io_file_open(&nrfile, nrpath,
                           APR_WRITE|APR_CREATE|APR_TRUNCATE|APR_BUFFERED,
                           APR_OS_DEFAULT, pool));
  out = svn_stream_from_aprfile(nrfile, pool);

  SVN_ERR(write_field(out, pool, "id", "%s",
                      txn_node_rev_id(pb, entry, pool)));
  SVN_ERR(write_field(out, pool, "type", entry->children ? "dir" : "file"));
  if (entry->pred)
    SVN_ERR(write_field(out, pool, "pred", "%s",
                        node_rev_id(entry->pred, pool)));
  SVN_ERR(write_field(out, pool, "count", "%d", entry->pred_count));
  SVN_ERR(write_field(out, pool, "text", "%s",
                      txn_repstr(pb, &entry->text_rep,
                                 (entry->children != NULL), pool)));
  if (SVN_IS_VALID_REVNUM(entry->props_rep.rev))
    SVN_ERR(write_field(out, pool, "props", "%s",
                        txn_repstr(pb, &entry->props_rep, TRUE, pool)));
  SVN_ERR(write_field(out, pool, "cpath", "%s", entry->created_path));
  if (SVN_IS_VALID_REVNUM(entry->copyfrom_rev))
    SVN_ERR(write_field(out, pool, "copyfrom", "%" SVN_REVNUM_T_FMT " %s",
                        entry->copyfrom_rev, entry->copyfrom_path));
  if (entry->copyroot != entry)
    SVN_ERR(write_field(out, pool, "copyroot", "%s %s",
                        txn_revstr(pb, entry->copyroot->node_rev, pool),
                        entry->copyroot->created_path));
  SVN_ERR(svn_stream_printf(out, pool, "\n"));

  SVN_ERR(svn_io_file_close(nrfile, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
dump_txn(struct parse_baton *pb, apr_pool_t *pool)
{
  apr_file_t *next_ids_file;
  const char *str;

  /* We're done writing the prototype rev file. */
  SVN_ERR(svn_io_file_close(pb->rev_file, pool));

  /* Open a file for the rev-props. */
  SVN_ERR(write_hash_to_file(pb->rev_props, "transactions/0/props", pool));

  /* Dump the new node-revisions in the transaction. */
  SVN_ERR(dump_txn_node_rev(pb, get_root(pb, pb->current_rev), pool));

  /* Write the next-ids file. */
  SVN_ERR(svn_io_file_open(&next_ids_file, "transactions/0/next-ids",
                           APR_WRITE|APR_CREATE|APR_TRUNCATE|APR_BUFFERED,
                           APR_OS_DEFAULT, pool));
  str = "10001 10001";
  SVN_ERR(svn_io_file_write_full(next_ids_file, str, strlen(str), NULL, pool));
  SVN_ERR(svn_io_file_close(next_ids_file, pool));

  return SVN_NO_ERROR;
}

/* --- The parser functions --- */

static svn_error_t *
new_revision_record(void **revision_baton, apr_hash_t *headers, void *baton,
                    apr_pool_t *pool)
{
  struct parse_baton *pb = baton;
  const char *revstr, *path;
  svn_revnum_t rev;
  struct entry *root;

  /* Get the number of this revision in string and integral form. */
  revstr = apr_hash_get(headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER,
                        APR_HASH_KEY_STRING);
  rev = SVN_STR_TO_REV(revstr);
  assert(rev == pb->roots->nelts);
  assert(rev == pb->current_rev + 1);

  pb->rev_pool = svn_pool_create(pb->pool);

  /* Open a file for this revision. */
  if (rev == pb->txn_rev)
    {
      /* We've been asked to dump this rev as a transaction. */
      SVN_ERR(write_current(pb, pb->rev_pool));
      pb->next_node_id = -10000;
      pb->next_copy_id = -10000;
      SVN_ERR(svn_io_make_dir_recursively("transactions/0", pb->rev_pool));
      path = "transactions/0/rev";
    }
  else
    {
      SVN_ERR(svn_io_make_dir_recursively("revs", pb->rev_pool));
      path = svn_path_join("revs", revstr, pb->rev_pool);
    }
  SVN_ERR(svn_io_file_open(&pb->rev_file, path,
                           APR_WRITE|APR_CREATE|APR_TRUNCATE|APR_BUFFERED,
                           APR_OS_DEFAULT, pb->rev_pool));
  pb->rev_stream = svn_stream_from_aprfile(pb->rev_file, pb->rev_pool);
  pb->current_rev = rev;

  /* Initialize the changed-path hash tables and rev-props. */
  pb->deleted_paths = apr_hash_make(pb->rev_pool);
  pb->added_paths = apr_hash_make(pb->rev_pool);
  pb->modified_paths = apr_hash_make(pb->rev_pool);
  pb->rev_props = apr_hash_make(pb->rev_pool);

  /* Set up a new root for this rev. */
  root = new_entry(pb->pool);
  if (rev != 0)
    copy_entry(pb, root, get_root(pb, rev - 1));
  else
    {
      root->node_id = pb->next_node_id++;
      root->copy_id = pb->next_copy_id++;
      root->children = apr_hash_make(pb->pool);
      root->children_changed = TRUE;
      root->copyroot = root;
      root->node_rev = 0;
    }
  root->created_path = "";
  APR_ARRAY_PUSH(pb->roots, struct entry *) = root;

  *revision_baton = pb;
  return SVN_NO_ERROR;
}

static svn_error_t *
uuid_record(const char *uuid, void *parse_baton, apr_pool_t *pool)
{
  /* Nothing yet. */
  return SVN_NO_ERROR;
}

static svn_error_t *
new_node_record(void **node_baton, apr_hash_t *headers, void *baton,
                apr_pool_t *pool)
{
  struct parse_baton *pb = baton;
  svn_node_kind_t kind;
  enum svn_node_action action;
  svn_revnum_t copyfrom_rev;
  const char *path, *copyfrom_path, *parent_path, *name;
  struct entry *parent, *entry, *copy_src;

  get_node_info(headers, &path, &kind, &action, &copyfrom_rev, &copyfrom_path);
  svn_path_split(path, &parent_path, &name, pool);
  parent = follow_path(pb, parent_path, pool);
  path = apr_pstrdup(pb->rev_pool, path);
  switch (action)
    {
    case svn_node_action_change:
      entry = get_child(pb, parent, name, pool);
      apr_hash_set(pb->modified_paths, path, APR_HASH_KEY_STRING, entry);
      pb->current_node = entry;
      break;
    case svn_node_action_delete:
      entry = apr_hash_get(parent->children, name, APR_HASH_KEY_STRING);
      apr_hash_set(pb->deleted_paths, path, APR_HASH_KEY_STRING, entry);
      apr_hash_set(parent->children, name, APR_HASH_KEY_STRING, NULL);
      pb->current_node = NULL;
      break;
    case svn_node_action_add:
    case svn_node_action_replace:
      entry = new_entry(pb->pool);
      if (SVN_IS_VALID_REVNUM(copyfrom_rev))
        {
          copy_src = find_entry(get_root(pb, copyfrom_rev), copyfrom_path,
                                pool);
          copy_entry(pb, entry, copy_src);
          entry->copy_id = pb->next_copy_id++;
          entry->copyfrom_rev = copy_src->node_rev;
          entry->copyfrom_path = apr_pstrdup(pb->pool, copyfrom_path);
          entry->copyroot = entry;
        }
      else
        {
          entry->node_id = pb->next_node_id++;
          entry->copy_id = parent->copy_id;
          entry->copyroot = parent->copyroot;
          if (kind == svn_node_dir)
            {
              entry->children = apr_hash_make(pb->pool);
              entry->children_changed = TRUE;
            }
          entry->node_rev = pb->current_rev;
          entry->node_off = -1;
        }
      entry->created_path = apr_pstrdup(pb->pool, path);
      name = apr_pstrdup(pb->pool, name);
      apr_hash_set(parent->children, name, APR_HASH_KEY_STRING, entry);
      parent->children_changed = TRUE;
      apr_hash_set(pb->added_paths, path, APR_HASH_KEY_STRING, entry);
      pb->current_node = entry;
      break;
    default:
      abort();
    }

  *node_baton = pb;
  return SVN_NO_ERROR;
}

static svn_error_t *
set_revision_property(void *baton, const char *name, const svn_string_t *value)
{
  struct parse_baton *pb = baton;

  name = apr_pstrdup(pb->rev_pool, name);
  value = svn_string_dup(value, pb->rev_pool);
  apr_hash_set(pb->rev_props, name, APR_HASH_KEY_STRING, value);
  return SVN_NO_ERROR;
}

static svn_error_t *
set_node_property(void *baton, const char *name, const svn_string_t *value)
{
  struct parse_baton *pb = baton;

  assert(pb->current_node);
  assert(pb->current_node->props);
  name = apr_pstrdup(pb->pool, name);
  value = svn_string_dup(value, pb->pool);
  apr_hash_set(pb->current_node->props, name, APR_HASH_KEY_STRING, value);
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_node_property(void *node_baton, const char *name)
{
  /* We can't handle incremental dumps. */
  abort();
}

static svn_error_t *
remove_node_props(void *baton)
{
  struct parse_baton *pb = baton;
  struct entry *entry = pb->current_node;

  entry->props_pool = svn_pool_create(pb->pool);
  entry->props = apr_hash_make(entry->props_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
text_write(void *baton, const char *data, apr_size_t *len)
{
  struct parse_baton *pb = baton;

  apr_md5_update(&pb->md5_ctx, data, *len);
  return svn_stream_write(pb->delta_stream, data, len);
}

static svn_error_t *
text_close(void *baton)
{
  struct parse_baton *pb = baton;
  struct entry *entry = pb->current_node;
  struct rep_pointer *rep = &entry->text_rep;
  unsigned char digest[APR_MD5_DIGESTSIZE];
  apr_off_t offset;

  SVN_ERR(svn_stream_close(pb->delta_stream));
  apr_md5_final(digest, &pb->md5_ctx);
  rep->digest = svn_md5_digest_to_cstring(digest, pb->pool);

  /* Record the length of the data written (subtract for the header line). */
  offset = 0;
  SVN_ERR(svn_io_file_seek(pb->rev_file, APR_CUR, &offset, pb->pool));
  rep->len = offset - rep->off - 6;

  /* Write a representation trailer to the rev file. */
  SVN_ERR(svn_stream_printf(pb->rev_stream, pb->pool, "ENDREP\n"));

  return SVN_NO_ERROR;
}

static svn_error_t *
set_fulltext(svn_stream_t **stream, void *baton)
{
  struct parse_baton *pb = baton;
  struct entry *entry = pb->current_node;
  struct rep_pointer *rep = &entry->text_rep;
  svn_txdelta_window_handler_t wh;
  void *whb;

  /* Record the current offset of the rev file as the text rep location. */
  rep->rev = pb->current_rev;
  rep->off = 0;
  SVN_ERR(svn_io_file_seek(pb->rev_file, APR_CUR, &rep->off, pb->pool));

  /* Write a representation header to the rev file. */
  SVN_ERR(svn_io_file_write_full(pb->rev_file, "DELTA\n", 6, NULL, pb->pool));

  /* Prepare to write the svndiff data. */
  svn_txdelta_to_svndiff(pb->rev_stream, pb->rev_pool, &wh, &whb);
  pb->delta_stream = svn_txdelta_target_push(wh, whb, pb->empty_stream,
                                             pb->rev_pool);

  /* Get ready to compute the MD5 digest. */
  apr_md5_init(&pb->md5_ctx);

  /* Hand the caller a writable stream to write the data to. */
  *stream = svn_stream_create(pb, pb->pool);
  svn_stream_set_write(*stream, text_write);
  svn_stream_set_close(*stream, text_close);
  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(svn_txdelta_window_handler_t *handler, void **handler_baton,
                void *baton)
{
  /* We can't handle incremental dumps. */
  abort();
}

svn_error_t *
close_node(void *baton)
{
  return SVN_NO_ERROR;
}

svn_error_t *
close_revision(void *baton)
{
  struct parse_baton *pb = baton;
  struct entry *root = get_root(pb, pb->current_rev);
  apr_pool_t *pool = pb->rev_pool;
  apr_off_t offset;
  const char *revstr, *path;

  if (pb->current_rev == pb->txn_rev)
    {
      /* We've been asked to dump this rev as a transaction and exit. */
      SVN_ERR(dump_txn(pb, pool));
      exit(0);
    }

  SVN_ERR(write_entry(pb, root, pool));

  /* Get the rev file offset of the changed-path data. */
  offset = 0;
  SVN_ERR(svn_io_file_seek(pb->rev_file, APR_CUR, &offset, pool));

  SVN_ERR(write_changed_path_data(pb, pool));

  /* Write out the offsets for the root node and changed-path data. */
  SVN_ERR(svn_stream_printf(pb->rev_stream, pool,
                            "\n%" APR_OFF_T_FMT " %" APR_OFF_T_FMT "\n",
                            root->node_off, offset));
  SVN_ERR(svn_io_file_close(pb->rev_file, pool));

  /* Dump the rev-props. */
  SVN_ERR(svn_io_make_dir_recursively("revprops", pool));
  revstr = apr_psprintf(pool, "%" SVN_REVNUM_T_FMT, pb->current_rev);
  path = svn_path_join("revprops", revstr, pool);
  SVN_ERR(write_hash_to_file(pb->rev_props, path, pool));

  svn_pool_destroy(pb->rev_pool);
  return SVN_NO_ERROR;
}

static svn_repos_parser_fns2_t parser = {
  new_revision_record,
  uuid_record,
  new_node_record,
  set_revision_property,
  set_node_property,
  delete_node_property,
  remove_node_props,
  set_fulltext,
  apply_textdelta,
  close_node,
  close_revision
};

int
main(int argc, char **argv)
{
  apr_pool_t *pool;
  apr_file_t *infile;
  svn_stream_t *instream;
  struct parse_baton pb;
  svn_error_t *err;

  apr_initialize();
  pool = svn_pool_create(NULL);
  apr_file_open_stdin(&infile, pool);
  instream = svn_stream_from_aprfile(infile, pool);
  pb.txn_rev = (argc > 1) ? SVN_STR_TO_REV(argv[1]) : SVN_INVALID_REVNUM;
  pb.roots = apr_array_make(pool, 1, sizeof(struct entry *));
  pb.current_rev = SVN_INVALID_REVNUM;
  pb.rev_file = NULL;
  pb.rev_stream = NULL;
  pb.next_node_id = 0;
  pb.next_copy_id = 0;
  pb.empty_stream = svn_stream_empty(pool);
  pb.pool = pool;
  err = svn_repos_parse_dumpstream2(instream, &parser, &pb, NULL, NULL, pool);
  if (!err)
    err = write_current(&pb, pool);
  if (err)
    svn_handle_error(err, stderr, TRUE);
  return 0;
}
