#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_repos.h"
#include "svn_io.h"
#include "svn_ra.h"

#include "load_editor.h"

svn_error_t *
build_dumpfile_parser(const svn_repos_parse_fns2_t **parser,
                      void **parse_baton,
                      svn_stream_t *stream,
                      apr_pool_t *pool)
{
  svn_repos_parse_fns2_t *pf;

  pf = apr_pcalloc(pool, sizeof(*pf));

  SVN_ERR(svn_repos_parse_dumpstream2(stream, pf, NULL,
                                      NULL, NULL, pool));

  *parser = pf;
  *parse_baton = NULL;

  return SVN_NO_ERROR;
}
