#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_repos.h"
#include "svn_io.h"
#include "svn_ra.h"

#include "load_editor.h"

static svn_error_t *
new_revision_record(void **revision_baton,
		    apr_hash_t *headers,
		    void *parse_baton,
		    apr_pool_t *pool)
{
  struct revision_baton *rb;
  apr_hash_index_t *hi;
  const struct svn_delta_editor_t *commit_editor;
  void *root_baton;

  rb = apr_pcalloc(pool, sizeof(*rb));
  rb->pb = parse_baton;
  commit_editor = rb->pb->commit_editor;

  for (hi = apr_hash_first(pool, headers); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *hname, *hval;

      apr_hash_this(hi, &key, NULL, &val);
      hname = key;
      hval = val;

      if (strcmp(hname, SVN_REPOS_DUMPFILE_REVISION_NUMBER) == 0)
        rb->rev = atoi(hval);
    }

  SVN_ERR(commit_editor->open_root(rb->pb->commit_edit_baton, rb->rev,
                                   pool, &root_baton));
  rb->dir_baton = root_baton;

  *revision_baton = rb;
  return SVN_NO_ERROR;
}

static svn_error_t *
uuid_record(const char *uuid,
            void *parse_baton,
            apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
new_node_record(void **node_baton,
                apr_hash_t *headers,
                void *revision_baton,
                apr_pool_t *pool)
{
  struct node_baton *nb;
  apr_hash_index_t *hi;
  void *file_baton;
  void *child_baton;
  const struct svn_delta_editor_t *commit_editor;
  nb = apr_pcalloc(pool, sizeof(*nb));
  nb->rb = revision_baton;

  commit_editor = nb->rb->pb->commit_editor;

  for (hi = apr_hash_first(pool, headers); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *hname, *hval;

      apr_hash_this(hi, &key, NULL, &val);
      hname = key;
      hval = val;

      /* Parse the different kinds of headers we can encounter and
         stuff them into the node_baton for writing later */
      if (strcmp(hname, SVN_REPOS_DUMPFILE_NODE_PATH) == 0)
        nb->path = apr_pstrdup(pool, hval);
      if (strcmp(hname, SVN_REPOS_DUMPFILE_NODE_KIND) == 0)
        nb->kind = strcmp(hval, "file") == 0 ? svn_node_file : svn_node_dir;
      if (strcmp(hname, SVN_REPOS_DUMPFILE_NODE_ACTION) == 0)
        {
          if (strcmp(hval, "add") == 0)
            nb->action = svn_node_action_add;
          if (strcmp(hval, "change") == 0)
            nb->action = svn_node_action_change;
          if (strcmp(hval, "delete") == 0)
            nb->action = svn_node_action_delete;
          if (strcmp(hval, "replace") == 0)
            nb->action = svn_node_action_replace;
        }
      if (strcmp(hname, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV) == 0)
        nb->copyfrom_rev = atoi(hval);
      if (strcmp(hname, SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH) == 0)
        nb->copyfrom_path = apr_pstrdup(pool, hval);
    }

  if (nb->action == svn_node_action_add)
    {
      if (nb->kind == svn_node_file)
        {
          SVN_ERR(commit_editor->add_file(nb->path, nb->rb->dir_baton, NULL,
                                          SVN_INVALID_REVNUM, pool,
                                          &file_baton));
          nb->file_baton = file_baton;
        }
      else if(nb->kind == svn_node_dir)
        {
          SVN_ERR(commit_editor->add_directory(nb->path, nb->rb->dir_baton,
                                               NULL, SVN_INVALID_REVNUM,
                                               pool, &child_baton));
          nb->rb->dir_baton = child_baton;
        }
    }

  *node_baton = nb;
  return SVN_NO_ERROR;
}


static svn_error_t *
set_revision_property(void *baton,
                      const char *name,
                      const svn_string_t *value)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
set_node_property(void *baton,
                  const char *name,
                  const svn_string_t *value)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_node_property(void *baton,
                     const char *name)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
remove_node_props(void *baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
set_fulltext(svn_stream_t **stream,
             void *node_baton)
{
  /* ### Not implemented */
  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(svn_txdelta_window_handler_t *handler,
                void **handler_baton,
                void *node_baton)
{
  struct node_baton *nb;
  const struct svn_delta_editor_t *commit_editor;
  apr_pool_t *pool;
  nb = node_baton;
  commit_editor = nb->rb->pb->commit_editor;
  pool = nb->rb->pb->pool;
  return commit_editor->apply_textdelta(nb->file_baton, NULL, pool,
                                        handler, handler_baton);
}

static svn_error_t *
close_node(void *baton)
{
  struct node_baton *nb;
  const struct svn_delta_editor_t *commit_editor;  
  nb = baton;
  commit_editor = nb->rb->pb->commit_editor;
  if (nb->kind == svn_node_file)
    SVN_ERR(commit_editor->close_file(nb->file_baton, NULL,
                                      nb->rb->pb->pool));
  else if(nb->kind == svn_node_dir)
    SVN_ERR(commit_editor->close_directory(nb->rb->dir_baton,
                                           nb->rb->pb->pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
close_revision(void *baton)
{
  struct revision_baton *rb;
  const struct svn_delta_editor_t *commit_editor;    
  rb = baton;
  commit_editor = rb->pb->commit_editor;
  SVN_ERR(commit_editor->close_edit(rb->pb->commit_edit_baton, rb->pb->pool));
  return SVN_NO_ERROR;
}

svn_error_t *
build_dumpfile_parser(const svn_repos_parse_fns2_t **parser,
                      void **parse_baton,
                      const struct svn_delta_editor_t *editor,
                      void *edit_baton,
                      apr_pool_t *pool)
{
  svn_repos_parse_fns2_t *pf;
  struct parse_baton *pb;

  pf = apr_pcalloc(pool, sizeof(*pf));
  pf->new_revision_record = new_revision_record;
  pf->uuid_record = uuid_record;
  pf->new_node_record = new_node_record;
  pf->set_revision_property = set_revision_property;
  pf->set_node_property = set_node_property;
  pf->delete_node_property = delete_node_property;
  pf->remove_node_props = remove_node_props;
  pf->set_fulltext = set_fulltext;
  pf->apply_textdelta = apply_textdelta;
  pf->close_node = close_node;
  pf->close_revision = close_revision;

  pb = apr_pcalloc(pool, sizeof(*pb));
  pb->commit_editor = editor;
  pb->commit_edit_baton = edit_baton;
  pb->pool = pool;

  *parser = pf;
  *parse_baton = pb;

  return SVN_NO_ERROR;
}
