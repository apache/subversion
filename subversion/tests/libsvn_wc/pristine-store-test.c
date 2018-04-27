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

#include "utils.h"

#include "../../libsvn_wc/wc.h"
#include "../../libsvn_wc/wc_db.h"
#include "../../libsvn_wc/wc-queries.h"
#include "../../libsvn_wc/workqueue.h"

#include "private/svn_wc_private.h"

#include "../svn_test.h"


/* Create repos and WC, set *WC_ABSPATH to the WC path, and set *DB to a new
 * DB context. */
static svn_error_t *
create_repos_and_wc(const char **wc_abspath,
                    svn_wc__db_t **db,
                    const char *test_name,
                    const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_test__sandbox_t sandbox;

  SVN_ERR(svn_test__sandbox_create(&sandbox, test_name, opts, pool));
  *wc_abspath = sandbox.wc_abspath;
  *db = sandbox.wc_ctx->db;

  return SVN_NO_ERROR;
}

/* Exercise the pristine text API with a simple write and read. */
static svn_error_t *
pristine_write_read(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_wc__db_t *db;
  const char *wc_abspath;

  svn_wc__db_install_data_t *install_data;
  svn_stream_t *pristine_stream;
  apr_size_t sz;

  const char data[] = "Blah";
  svn_string_t *data_string = svn_string_create(data, pool);
  svn_checksum_t *data_sha1, *data_md5;

  SVN_ERR(create_repos_and_wc(&wc_abspath, &db,
                              "pristine_write_read", opts, pool));

  /* Write DATA into a new temporary pristine file, set PRISTINE_TMP_ABSPATH
   * to its path and set DATA_SHA1 and DATA_MD5 to its checksums. */
  SVN_ERR(svn_wc__db_pristine_prepare_install(&pristine_stream,
                                              &install_data,
                                              &data_sha1, &data_md5,
                                              db, wc_abspath,
                                              pool, pool));

  sz = strlen(data);
  SVN_ERR(svn_stream_write(pristine_stream, data, &sz));
  SVN_ERR(svn_stream_close(pristine_stream));

  /* Ensure it's not already in the store. */
  {
    svn_boolean_t present;

    SVN_ERR(svn_wc__db_pristine_check(&present, db, wc_abspath, data_sha1,
                                      pool));
    SVN_TEST_ASSERT(! present);
  }

  /* Install the new pristine file, referenced by its checksum. */
  SVN_ERR(svn_wc__db_pristine_install(install_data,
                                      data_sha1, data_md5, pool));

  /* Ensure it is now found in the store. */
  {
    svn_boolean_t present;

    SVN_ERR(svn_wc__db_pristine_check(&present, db, wc_abspath, data_sha1,
                                      pool));
    SVN_TEST_ASSERT(present);
  }

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

    SVN_ERR(svn_wc__db_pristine_read(&data_read_back, NULL, db, wc_abspath,
                                     data_sha1, pool, pool));
    SVN_ERR(svn_stream_contents_same2(&same, data_read_back, data_stream,
                                      pool));
    SVN_TEST_ASSERT(same);
  }

  /* Trivially test the "remove if unreferenced" API: it's not referenced
     so we should be able to remove it. */
  {
    svn_error_t *err;
    svn_stream_t *data_read_back;

    SVN_ERR(svn_wc__db_pristine_remove(db, wc_abspath, data_sha1, pool));
    err = svn_wc__db_pristine_read(&data_read_back, NULL, db, wc_abspath,
                                   data_sha1, pool, pool);
    SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_PATH_NOT_FOUND);
  }

  /* Ensure it's no longer found in the store. */
  {
    svn_boolean_t present;

    SVN_ERR(svn_wc__db_pristine_check(&present, db, wc_abspath, data_sha1,
                                      pool));
    SVN_TEST_ASSERT(! present);
  }

  return SVN_NO_ERROR;
}

/* Test deleting a pristine text while it is open for reading. */
static svn_error_t *
pristine_delete_while_open(const svn_test_opts_t *opts,
                           apr_pool_t *pool)
{
  svn_wc__db_t *db;
  const char *wc_abspath;
  svn_wc__db_install_data_t *install_data;
  svn_stream_t *pristine_stream;
  svn_stream_t *contents;
  apr_size_t sz;

  const char data[] = "Blah";
  svn_checksum_t *data_sha1, *data_md5;

  SVN_ERR(create_repos_and_wc(&wc_abspath, &db,
                              "pristine_delete_while_open", opts, pool));

  SVN_ERR(svn_wc__db_pristine_prepare_install(&pristine_stream,
                                              &install_data,
                                              &data_sha1, &data_md5,
                                              db, wc_abspath,
                                              pool, pool));

  sz = strlen(data);
  SVN_ERR(svn_stream_write(pristine_stream, data, &sz));
  SVN_ERR(svn_stream_close(pristine_stream));
  SVN_ERR(svn_wc__db_pristine_install(install_data,
                                      data_sha1, data_md5, pool));

  /* Open it for reading */
  SVN_ERR(svn_wc__db_pristine_read(&contents, NULL, db, wc_abspath, data_sha1,
                                   pool, pool));

  /* Delete it */
  SVN_ERR(svn_wc__db_pristine_remove(db, wc_abspath, data_sha1, pool));

  /* Continue to read from it */
  {
    char buffer[4];
    apr_size_t len = 4;

    SVN_ERR(svn_stream_read_full(contents, buffer, &len));
    SVN_TEST_ASSERT(len == 4);
    SVN_TEST_ASSERT(memcmp(buffer, data, len) == 0);
  }

  /* Ensure it's no longer found in the store. (The file may still exist as
   * an orphan, depending on the implementation.) */
  {
    svn_boolean_t present;

    SVN_ERR(svn_wc__db_pristine_check(&present, db, wc_abspath, data_sha1,
                                      pool));
    SVN_TEST_ASSERT(! present);
  }

  /* Close the read stream */
  SVN_ERR(svn_stream_close(contents));

  return SVN_NO_ERROR;
}

/* Check that the store rejects an attempt to replace an existing pristine
 * text with different text.
 *
 * White-box knowledge: The implementation compares the file sizes but
 * doesn't compare the text itself, so in this test we ensure the second
 * text is a different size. */
static svn_error_t *
reject_mismatching_text(const svn_test_opts_t *opts,
                        apr_pool_t *pool)
{
#ifdef SVN_DEBUG  /* The pristine store only checks this in debug mode. */
  svn_wc__db_t *db;
  const char *wc_abspath;

  const char data[] = "Blah";
  svn_checksum_t *data_sha1, *data_md5;

  const char data2[] = "Baz";

  SVN_ERR(create_repos_and_wc(&wc_abspath, &db,
                              "reject_mismatching_text", opts, pool));

  /* Install a pristine text. */
  {
    svn_wc__db_install_data_t *install_data;
    svn_stream_t *pristine_stream;
    apr_size_t sz;

    SVN_ERR(svn_wc__db_pristine_prepare_install(&pristine_stream,
                                                &install_data,
                                                &data_sha1, &data_md5,
                                                db, wc_abspath,
                                                pool, pool));

    sz = strlen(data);
    SVN_ERR(svn_stream_write(pristine_stream, data, &sz));
    SVN_ERR(svn_stream_close(pristine_stream));

    SVN_ERR(svn_wc__db_pristine_install(install_data,
                                        data_sha1, data_md5,
                                        pool));
  }

  /* Try to install the wrong pristine text against the same checksum.
   * Should fail. */
  {
    svn_wc__db_install_data_t *install_data;
    svn_stream_t *pristine_stream;
    apr_size_t sz;

    SVN_ERR(svn_wc__db_pristine_prepare_install(&pristine_stream,
                                                &install_data,
                                                &data_sha1, &data_md5,
                                                db, wc_abspath,
                                                pool, pool));

    sz = strlen(data2);
    SVN_ERR(svn_stream_write(pristine_stream, data2, &sz));
    SVN_ERR(svn_stream_close(pristine_stream));

    SVN_ERR(svn_wc__db_pristine_install(install_data,
                                        data_sha1, data_md5,
                                        pool));
  }

  return SVN_NO_ERROR;
#else
  return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL,
                          "The consistency check to be tested is only "
                          "active in debug-mode builds");
#endif
}


static int max_threads = -1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(pristine_write_read,
                       "pristine_write_read"),
    SVN_TEST_OPTS_PASS(pristine_delete_while_open,
                       "pristine_delete_while_open"),
    SVN_TEST_OPTS_PASS(reject_mismatching_text,
                       "reject_mismatching_text"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
