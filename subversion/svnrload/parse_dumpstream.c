#include "svn_pools.h"
#include "svn_cmdline.h"
#include "svn_repos.h"
#include "svn_io.h"

static svn_error_t *
new_revision_record(void **revision_baton,
		    apr_hash_t *headers,
		    void *parse_baton,
		    apr_pool_t *pool)
{
  printf("new_revision_record called");
  return SVN_NO_ERROR;
}

static svn_error_t *
new_node_record(void **node_baton,
                apr_hash_t *headers,
                void *revision_baton,
                apr_pool_t *pool)
{
  printf("new_node_record called");
  return SVN_NO_ERROR;
}

static svn_error_t *
uuid_record(const char *uuid,
            void *parse_baton,
            apr_pool_t *pool)
{
  printf("uuid_record called");
  return SVN_NO_ERROR;
}

static svn_error_t *
set_revision_property(void *baton,
                      const char *name,
                      const svn_string_t *value)
{
  printf("set_revision_property called");
  return SVN_NO_ERROR;
}

static svn_error_t *
set_node_property(void *baton,
                  const char *name,
                  const svn_string_t *value)
{
  printf("set_node_property called");
  return SVN_NO_ERROR;
}

static svn_error_t *
remove_node_props(void *baton)
{
  printf("remove_node_props called");
  return SVN_NO_ERROR;
}

static svn_error_t *
set_fulltext(svn_stream_t **stream,
             void *node_baton)
{
  printf("set_fulltext called");
  return SVN_NO_ERROR;
}

static svn_error_t *
close_node(void *baton)
{
  printf("close_node called");
  return SVN_NO_ERROR;
}

static svn_error_t *
close_revision(void *baton)
{
  printf("close_revision called");
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_node_property(void *baton,
                     const char *name)
{
  printf("delete_node_property called");
  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(svn_txdelta_window_handler_t *handler,
                void **handler_baton,
                void *node_baton)
{
  printf("apply_textdelta called");
  return SVN_NO_ERROR;
}

int
main(int argc, char **argv)
{
  apr_pool_t *pool;
  apr_file_t *dumpfile;
  svn_stream_t *dumpstream;
  svn_repos_parse_fns2_t *parser;
  struct parser_baton_t *parser_baton;

  if (svn_cmdline_init ("parse_dumpstream", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;
  pool = svn_pool_create(NULL);

  parser = apr_pcalloc(pool, sizeof(*parser));

  /* parser->new_revision_record = new_revision_record; */
  /* parser->new_node_record = new_node_record; */
  /* parser->uuid_record = uuid_record; */
  /* parser->set_revision_property = set_revision_property; */
  /* parser->set_node_property = set_node_property; */
  /* parser->remove_node_props = remove_node_props; */
  /* parser->set_fulltext = set_fulltext; */
  /* parser->close_node = close_node; */
  /* parser->close_revision = close_revision; */
  /* parser->delete_node_property = delete_node_property; */
  /* parser->apply_textdelta = apply_textdelta; */

  apr_file_open_stdin(&dumpfile, pool);
  dumpstream = svn_stream_from_aprfile2(dumpfile, FALSE, pool);
  SVN_INT_ERR(svn_repos_parse_dumpstream2(dumpstream, parser, NULL,
                                          NULL, NULL, pool));
  SVN_INT_ERR(svn_stream_close(dumpstream));
  svn_pool_destroy(pool);
  return 0;
}
