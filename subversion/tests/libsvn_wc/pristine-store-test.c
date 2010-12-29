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


/* Create repos and WC, and also set *DB to a new DB context. */
static svn_error_t *
create_repos_and_wc(const char **repos_url,
                    const char **wc_abspath,
                    svn_wc__db_t **db,
                    const char *test_name,
                    const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  SVN_ERR(svn_test__create_repos_and_wc(repos_url, wc_abspath, test_name,
                                        opts, pool));

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

  /* Ensure it's not already in the store. */
  {
    svn_boolean_t present;

    SVN_ERR(svn_wc__db_pristine_check(&present, db, wc_abspath, data_sha1,
                                      pool));
    SVN_TEST_ASSERT(! present);
  }

  /* Install the new pristine file, referenced by its checksum. */
  SVN_ERR(svn_wc__db_pristine_install(db, pristine_tmp_abspath,
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

    SVN_ERR(svn_wc__db_pristine_read(&data_read_back, db, wc_abspath,
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
    err = svn_wc__db_pristine_read(&data_read_back, db, wc_abspath,
                                   data_sha1, pool, pool);
    SVN_TEST_ASSERT(err != NULL);
    svn_error_clear(err);
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

/* Test the WQ item for getting and translating a text. */
static svn_error_t *
pristine_get_translated(const svn_test_opts_t *opts,
                        apr_pool_t *pool)
{
  svn_wc__db_t *db;
  const char *repos_url;
  const char *wc_abspath, *versioned_abspath, *new_abspath;
  const char data[] = "Blah at r$Rev$\n";
  const char expected_data[] = "Blah at r$Rev: -1 $\n";
  svn_checksum_t *data_sha1, *data_md5;

  SVN_ERR(create_repos_and_wc(&repos_url, &wc_abspath, &db,
                              "pristine_get_translated", opts, pool));

  versioned_abspath = svn_dirent_join(wc_abspath, "foo", pool);
  new_abspath = svn_dirent_join(wc_abspath, "foo.fetched", pool);

  /* Create VERSIONED_ABSPATH, whose metadata will be used for the
     translation. Set some properties on it. */
  {
    svn_wc_context_t *wc_ctx;
    const char *dirname = svn_dirent_dirname(versioned_abspath, pool);

    SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, db, pool));
    SVN_ERR(svn_io_file_create(versioned_abspath, data, pool));

    SVN_ERR(svn_wc__db_wclock_obtain(wc_ctx->db, dirname, 0, FALSE, pool));

    SVN_ERR(svn_wc_add_from_disk(wc_ctx, versioned_abspath, NULL, NULL, pool));
    SVN_ERR(svn_wc_prop_set4(wc_ctx, versioned_abspath,
                             "svn:keywords", svn_string_create("Rev", pool),
                             FALSE, NULL, NULL, pool));

    SVN_ERR(svn_wc__db_wclock_release(wc_ctx->db, dirname, pool));
  }

  /* Store a pristine text, and set DATA_SHA1 and DATA_MD5. */
  {
    const char *pristine_tmp_dir;
    const char *pristine_tmp_abspath;
    svn_stream_t *pristine_tmp_stream;

    SVN_ERR(svn_wc__db_pristine_get_tempdir(&pristine_tmp_dir, db,
                                            wc_abspath, pool, pool));
    SVN_ERR(svn_stream_open_unique(&pristine_tmp_stream, &pristine_tmp_abspath,
                                   pristine_tmp_dir, svn_io_file_del_none,
                                   pool, pool));

    pristine_tmp_stream = svn_stream_checksummed2(
                            pristine_tmp_stream, NULL, &data_sha1,
                            svn_checksum_sha1, TRUE, pool);
    pristine_tmp_stream = svn_stream_checksummed2(
                            pristine_tmp_stream, NULL, &data_md5,
                            svn_checksum_md5, TRUE, pool);

    SVN_ERR(svn_stream_printf(pristine_tmp_stream, pool, "%s", data));
    SVN_ERR(svn_stream_close(pristine_tmp_stream));

    SVN_ERR(svn_wc__db_pristine_install(db, pristine_tmp_abspath,
                                        data_sha1, data_md5, pool));
  }

  /* Run a work item to read and translate the text into NEW_ABSPATH. */
  {
    svn_skel_t *work_item;

    SVN_ERR(svn_wc__wq_build_pristine_get_translated(&work_item,
                                                     db, versioned_abspath,
                                                     new_abspath, data_sha1,
                                                     pool, pool));
    SVN_ERR(svn_wc__db_wq_add(db, versioned_abspath, work_item, pool));

    SVN_ERR(svn_wc__wq_run(db, wc_abspath, NULL, NULL, pool));
  }

  /* Check that NEW_ABSPATH has been created with the translated text. */
  {
    svn_stream_t *expected_stream
      = svn_stream_from_string(svn_string_create(expected_data, pool), pool);
    svn_stream_t *file_stream;
    svn_boolean_t same;

    SVN_ERR(svn_stream_open_readonly(&file_stream, new_abspath,
                                     pool, pool));
    SVN_ERR(svn_stream_contents_same2(&same, expected_stream, file_stream,
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
    SVN_TEST_OPTS_PASS(pristine_get_translated,
                       "pristine_get_translated"),
    SVN_TEST_NULL
  };
