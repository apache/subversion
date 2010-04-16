/*
 * pristine-store-test.c :  test the pristine-store subsystem
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

#include <apr_pools.h>
#include <apr_general.h>

#include "svn_types.h"

/* Make sure SVN_DEPRECATED is defined as empty before including svn_io.h.
   We don't want to trigger deprecation warnings.  */
#ifdef SVN_DEPRECATED
#undef SVN_DEPRECATED
#endif
#define SVN_DEPRECATED
#include "svn_io.h"

#include "svn_dirent_uri.h"
#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_wc.h"
#include "svn_client.h"

#include "../../libsvn_wc/wc.h"
#include "../../libsvn_wc/wc_db.h"
#include "../../libsvn_wc/wc-queries.h"

#include "private/svn_wc_private.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"


#define REPOSITORIES_WORK_DIR "svn-test-work/repositories"
#define WCS_WORK_DIR "svn-test-work/working-copies"

/* Create an empty repository and WC for the test TEST_NAME, and open a WC
 * DB context.  Set *REPOS_URL to the URL of the new repository, *WC_ABSPATH
 * to the root path of the new WC, and *DB to a new DB context.
 *
 * Create the repository and WC in subdirectories called repos/TEST_NAME and
 * wcs/TEST_NAME respectively, within the current working directory. */
static svn_error_t *
create_repos_and_wc(const char **repos_url,
                    const char **wc_abspath,
                    svn_wc__db_t **db,
                    const char *test_name,
                    const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  const char *repos_path = svn_relpath_join(REPOSITORIES_WORK_DIR, test_name,
                                            pool);
  const char *wc_path = svn_relpath_join(WCS_WORK_DIR, test_name, pool);

  /* Remove the repo and WC dirs if they already exist, to ensure the test
   * will run even if a previous failed attempt was not cleaned up. */
  SVN_ERR(svn_io_remove_dir2(repos_path, TRUE, NULL, NULL, pool));
  SVN_ERR(svn_io_remove_dir2(wc_path, TRUE, NULL, NULL, pool));

  /* Create the parent dirs of the repo and WC if necessary. */
  SVN_ERR(svn_io_make_dir_recursively(REPOSITORIES_WORK_DIR, pool));
  SVN_ERR(svn_io_make_dir_recursively(WCS_WORK_DIR, pool));

  /* Create a repos and set *REPOS_URL to its path. */
  {
    svn_repos_t *repos;

    SVN_ERR(svn_test__create_repos(&repos, repos_path, opts, pool));
    SVN_ERR(svn_test__current_directory_url(repos_url, repos_path, pool));
  }

  /* Create a WC */
  {
    svn_client_ctx_t *ctx;
    svn_opt_revision_t head_rev = { svn_opt_revision_head, {0} };

    SVN_ERR(svn_client_create_context(&ctx, pool));
    /* SVN_ERR(svn_config_get_config(&ctx->config, config_dir, pool)); */
    SVN_ERR(svn_dirent_get_absolute(wc_abspath, wc_path, pool));
    SVN_ERR(svn_client_checkout3(NULL, *repos_url, *wc_abspath,
                                 &head_rev, &head_rev, svn_depth_infinity,
                                 FALSE /* ignore_externals */,
                                 FALSE /* allow_unver_obstructions */,
                                 ctx, pool));
  }

  /* Open a DB context */
  SVN_ERR(svn_wc__db_open(db, svn_wc__db_openmode_readonly, NULL,
                          FALSE /* auto_upgrade */,
                          TRUE /* enforce_empty_wq */,
                          pool, pool));

  return SVN_NO_ERROR;
}


/* Exercise the pristine text API with a simple write and read. */
static svn_error_t *
pristine_write_read(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_wc__db_t *db;
  const char *repos_url;
  const char *wc_abspath;

  const char *pristine_tmp_abspath;
  svn_stream_t *pristine_tmp_stream;

  const char data[] = "Blah";
  svn_string_t *data_string = svn_string_create(data, pool);
  svn_checksum_t *data_sha1, *data_md5;

  SVN_ERR(create_repos_and_wc(&repos_url, &wc_abspath, &db,
                              "pristine_write_read", opts, pool));

  /* Make a new temporary pristine file, and set PRISTINE_TMP_STREAM and
   * PRISTINE_TMP_ABSPATH to access it. */
  {
    const char *pristine_tmp_dir;

    SVN_ERR(svn_wc__db_pristine_get_tempdir(&pristine_tmp_dir, db,
                                            wc_abspath, pool, pool));
    SVN_ERR(svn_stream_open_unique(&pristine_tmp_stream, &pristine_tmp_abspath,
                                   pristine_tmp_dir, svn_io_file_del_none,
                                   pool, pool));
  }

  /* Copy DATA to PRISTINE_TMP_STREAM and calculate its checksums. */
  {
    svn_stream_t *data_stream = svn_stream_from_string(data_string, pool);

    data_stream = svn_stream_checksummed2(data_stream, &data_sha1, NULL,
                                          svn_checksum_sha1, TRUE, pool);
    data_stream = svn_stream_checksummed2(data_stream, &data_md5, NULL,
                                          svn_checksum_md5, TRUE, pool);
    SVN_ERR(svn_stream_copy3(data_stream, pristine_tmp_stream, NULL, NULL,
                             pool));
  }

  /* Install the new pristine file, referenced by its checksum. */
  SVN_ERR(svn_wc__db_pristine_install(db, pristine_tmp_abspath,
                                      data_sha1, data_md5, pool));

  /* Look up its MD-5 from its SHA-1, and check it's the same MD-5. */
  {
    const svn_checksum_t *looked_up_md5;

    SVN_ERR(svn_wc__db_pristine_get_md5(&looked_up_md5, db, wc_abspath,
                                        data_sha1, pool, pool));
    SVN_TEST_ASSERT(looked_up_md5->kind == svn_checksum_md5);
    SVN_TEST_ASSERT(svn_checksum_match(data_md5, looked_up_md5));
  }

  /* Read the pristine text back and verify it's the same content. */
  {
    svn_stream_t *data_stream = svn_stream_from_string(data_string, pool);
    svn_stream_t *data_read_back;
    svn_boolean_t same;

    SVN_ERR(svn_wc__db_pristine_read(&data_read_back, db, wc_abspath,
                                     data_sha1, pool, pool));
    SVN_ERR(svn_stream_contents_same2(&same, data_read_back, data_stream,
                                      pool));
    SVN_TEST_ASSERT(same);
  }

  return SVN_NO_ERROR;
}


struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(pristine_write_read,
                       "pristine_write_read"),
    SVN_TEST_NULL
  };
