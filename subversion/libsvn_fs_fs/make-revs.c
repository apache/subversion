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
  svn_boolean_t soft_copy;
};

struct parse_baton
{
  apr_array_header_t *roots;
  struct entry *current_node;
  svn_revnum_t current_rev;
  apr_file_t *rev_file;
  svn_stream_t *rev_stream;
  apr_md5_ctx_t md5_ctx;
  int next_node_id;
  int next_copy_id;
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
  entry->soft_copy = FALSE;
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

static void
copy_entry(struct parse_baton *pb, struct entry *new_entry,
           struct entry *old_entry, svn_boolean_t is_copy,
           svn_boolean_t soft_copy)
{
  *new_entry = *old_entry;
  if (new_entry->children)
    new_entry->children = apr_hash_copy(pb->pool, old_entry->children);
  new_entry->node_rev = pb->current_rev;
  new_entry->node_off = -1;
  new_entry->pred_count = old_entry->pred_count + 1;
  new_entry->pred = old_entry;
  if (is_copy)
    {
      new_entry->copy_id = pb->next_copy_id++;
      new_entry->copyfrom_rev = old_entry->node_rev;
      new_entry->copyfrom_path = old_entry->created_path;
      new_entry->soft_copy = soft_copy;
    }
  else
    {
      /* Make the new node-rev a change of the old one. */
      new_entry->copyfrom_rev = SVN_INVALID_REVNUM;
      new_entry->copyfrom_path = NULL;
      if (SVN_IS_VALID_REVNUM(old_entry->copyfrom_rev) || !old_entry->pred)
        new_entry->copyroot = old_entry;
    }
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
      path = svn_path_join(entry->created_path, name, pb->pool);
      /* Copy the child entry.  Create a "soft copy" if our created
         path does not match the old child entry's created path. */
      new_child = new_entry(pb->pool);
      copy_entry(pb, new_child, child,
                 (strcmp(path, child->created_path) != 0), TRUE);
      new_child->created_path = path;
      name = apr_pstrdup(pb->pool, name);
      apr_hash_set(entry->children, name, APR_HASH_KEY_STRING, new_child);
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

static svn_error_t *
write_directory_rep(struct parse_baton *pb, struct entry *entry,
                    apr_pool_t *pool)
{
  apr_hash_t *tmpmap;
  apr_hash_index_t *hi;
  const void *key;
  void *val;
  struct entry *child;
  const char *rep;

  /* Convert the children hash to something we can dump. */
  tmpmap = apr_hash_copy(pool, entry->children);
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

  return write_hash_rep(pb, tmpmap, &entry->text_rep, pool);
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
write_field(struct parse_baton *pb, apr_pool_t *pool, const char *name,
            const char *fmt, ...)
{
  const char *val;
  va_list ap;

  /* Format the value. */
  va_start(ap, fmt);
  val = apr_pvsprintf(pool, fmt, ap);
  va_end(ap);

  /* Write it out using the normal or length-counted field format as needed. */
  if (strchr(val, '\n') == NULL)
    return svn_stream_printf(pb->rev_stream, pool, "%s: %s\n", name, val);
  else
    return svn_stream_printf(pb->rev_stream, pool,
                             "%s:%" APR_SIZE_T_FMT "%s\n",
                             name, strlen(val), val);
}

static svn_error_t *
write_node_rev(struct parse_baton *pb, struct entry *entry, apr_pool_t *pool)
{
  svn_stream_t *out = pb->rev_stream;

  /* Get the rev file offset of the node-rev. */
  entry->node_off = 0;
  SVN_ERR(svn_io_file_seek(pb->rev_file, APR_CUR, &entry->node_off, pool));

  SVN_ERR(write_field(pb, pool, "id", "%s", node_rev_id(entry, pool)));
  SVN_ERR(write_field(pb, pool, "type", entry->children ? "dir" : "file"));
  if (entry->pred)
    SVN_ERR(write_field(pb, pool, "pred", "%s",
                        node_rev_id(entry->pred, pool)));
  SVN_ERR(write_field(pb, pool, "count", "%d", entry->pred_count));
  SVN_ERR(write_field(pb, pool, "text", "%s", repstr(&entry->text_rep, pool)));
  if (SVN_IS_VALID_REVNUM(entry->props_rep.rev))
    SVN_ERR(write_field(pb, pool, "props", "%s",
                        repstr(&entry->props_rep, pool)));
  SVN_ERR(write_field(pb, pool, "cpath", "%s", entry->created_path));
  if (SVN_IS_VALID_REVNUM(entry->copyfrom_rev))
    SVN_ERR(write_field(pb, pool, "copyfrom",
                        "%s %" SVN_REVNUM_T_FMT "%s\n",
                        (entry->soft_copy) ? "soft" : "hard",
                        entry->copyfrom_rev, entry->copyfrom_path));
  else
    SVN_ERR(write_field(pb, pool, "copyroot", "%s",
                        node_rev_id(entry->copyroot, pool)));
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

      if (entry->node_rev == pb->current_rev)
        SVN_ERR(write_directory_rep(pb, entry, pool));
    }

  if (entry->props)
    SVN_ERR(write_props(pb, entry, pool));

  if (entry->node_rev == pb->current_rev)
    SVN_ERR(write_node_rev(pb, entry, pool));

  return SVN_NO_ERROR;
}

/* --- The parser functions --- */

static svn_error_t *
new_revision_record(void **revision_baton, apr_hash_t *headers, void *baton,
                    apr_pool_t *pool)
{
  struct parse_baton *pb = baton;
  const char *revstr;
  svn_revnum_t rev;
  struct entry *root;

  /* Get the number of this revision in string and integral form. */
  revstr = apr_hash_get(headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER,
                        APR_HASH_KEY_STRING);
  rev = SVN_STR_TO_REV(revstr);
  assert(rev == pb->roots->nelts);
  assert(rev == pb->current_rev + 1);
  pb->current_rev = rev;

  /* Open a file for this revision. */
  SVN_ERR(svn_io_file_open(&pb->rev_file, revstr,
                           APR_WRITE|APR_CREATE|APR_TRUNCATE|APR_BUFFERED,
                           APR_OS_DEFAULT, pb->pool));
  pb->rev_stream = svn_stream_from_aprfile(pb->rev_file, pb->pool);

  /* Set up a new root for this rev. */
  root = new_entry(pb->pool);
  if (rev != 0)
    copy_entry(pb, root, get_root(pb, rev - 1), FALSE, FALSE);
  else
    {
      root->node_id = pb->next_node_id++;
      root->copy_id = pb->next_copy_id++;
      root->children = apr_hash_make(pb->pool);
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
  switch (action)
    {
    case svn_node_action_change:
      pb->current_node = get_child(pb, parent, name, pool);
      break;
    case svn_node_action_delete:
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
          copy_entry(pb, entry, copy_src, TRUE, FALSE);
        }
      else
        {
          entry->node_id = pb->next_node_id++;
          entry->copy_id = parent->copy_id;
          if (kind == svn_node_dir)
            entry->children = apr_hash_make(pb->pool);
          entry->node_rev = pb->current_rev;
          entry->node_off = -1;
          entry->copyroot = parent->copyroot;
        }
      entry->created_path = apr_pstrdup(pb->pool, path);
      name = apr_pstrdup(pb->pool, name);
      apr_hash_set(parent->children, name, APR_HASH_KEY_STRING, entry);
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
  /* Nothing yet. */
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
  return svn_io_file_write_full(pb->rev_file, data, *len, len, pb->pool);
}

static svn_error_t *
text_close(void *baton)
{
  struct parse_baton *pb = baton;
  struct entry *entry = pb->current_node;
  struct rep_pointer *rep = &entry->text_rep;
  unsigned char digest[APR_MD5_DIGESTSIZE];
  apr_off_t offset;

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

  /* Record the current offset of the rev file as the text rep location. */
  rep->rev = pb->current_rev;
  rep->off = 0;
  SVN_ERR(svn_io_file_seek(pb->rev_file, APR_CUR, &rep->off, pb->pool));

  /* Write a representation header to the rev file. */
  SVN_ERR(svn_io_file_write_full(pb->rev_file, "PLAIN\n", 6, NULL, pb->pool));

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

svn_error_t *close_revision(void *baton)
{
  struct parse_baton *pb = baton;
  apr_pool_t *pool = svn_pool_create(pb->pool);
  struct entry *root = get_root(pb, pb->current_rev);
  apr_off_t offset;

  SVN_ERR(write_entry(pb, root, pool));

  /* Get the rev file offset of the changed-path data. */
  offset = 0;
  SVN_ERR(svn_io_file_seek(pb->rev_file, APR_CUR, &offset, pool));

  /* XXX changed-path data goes here */

  /* Write out the offsets for the root node and changed-path data. */
  SVN_ERR(svn_stream_printf(pb->rev_stream, pool,
                            "\n%" APR_OFF_T_FMT " %" APR_OFF_T_FMT "\n",
                            root->node_off, offset));
  SVN_ERR(svn_io_file_close(pb->rev_file, pool));
  svn_pool_destroy(pool);
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

int main()
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
  pb.roots = apr_array_make(pool, 1, sizeof(struct entry *));
  pb.current_rev = SVN_INVALID_REVNUM;
  pb.rev_file = NULL;
  pb.rev_stream = NULL;
  pb.next_node_id = 0;
  pb.next_copy_id = 0;
  pb.pool = pool;
  err = svn_repos_parse_dumpstream2(instream, &parser, &pb, NULL, NULL, pool);
  if (err)
    svn_handle_error(err, stderr, TRUE);
  return 0;
}
