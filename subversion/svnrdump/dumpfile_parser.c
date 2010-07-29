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
  rb = apr_pcalloc(pool, sizeof(*rb));
  rb->pb = parse_baton;

  fprintf(stderr, "new_revision_record called\n");

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
  nb = apr_pcalloc(pool, sizeof(*nb));
  nb->rb = revision_baton;

  fprintf(stderr, "new_node_record called\n");

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
  return SVN_NO_ERROR;
}

#if 0
static svn_error_t *
apply_window(svn_txdelta_window_t *window, void *baton)
{
  struct apply_baton *apply_baton;
  apr_size_t tlen;
  apply_baton = baton;
  if (window == NULL)
    return SVN_NO_ERROR;

  tlen = window->tview_len;
  apply_baton->target = apr_pcalloc(apply_baton->pool, tlen);
  svn_txdelta_apply_instructions(window, apply_baton->source,
                                 apply_baton->target, &tlen);
  return SVN_NO_ERROR;
}
#endif

static svn_error_t *
apply_window(svn_txdelta_window_t *window, void *baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(svn_txdelta_window_handler_t *handler,
                void **handler_baton,
                void *node_baton)
{
  struct node_baton *nb;
  nb = node_baton;
  *handler = apply_window;
  *handler_baton = nb->rb->pb->ab;
  return SVN_NO_ERROR;
}

static svn_error_t *
close_node(void *baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
close_revision(void *baton)
{
  return SVN_NO_ERROR;
}

svn_error_t *
build_dumpfile_parser(const svn_repos_parse_fns2_t **parser,
                      void **parse_baton,
                      apr_pool_t *pool)
{
  svn_repos_parse_fns2_t *pf;
  struct parse_baton *pb;
  struct apply_baton *ab;

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
  ab = apr_pcalloc(pool, sizeof(struct apply_baton));
  ab->source = apr_pstrmemdup(pool, " ", sizeof(" "));
  pb->ab = ab;

  *parser = pf;
  *parse_baton = pb;

  return SVN_NO_ERROR;
}
