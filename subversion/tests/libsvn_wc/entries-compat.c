/*
 * entries-compat.c :  test backwards compatibility issues for entries
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
#include "svn_wc.h"

#include "../../libsvn_wc/wc.h"
#include "../../libsvn_wc/wc_db.h"

#include "../svn_test.h"
#include "utils.h"


/* NOTE: these must be canonical!  */
#define ROOT_ONE "http://example.com/one"
#define ROOT_TWO "http://example.com/two"
#define ROOT_THREE "http://example.com/three"

#define UUID_ONE "uuid1"
#define UUID_TWO "uuid2"
#define UUID_THREE "uuid3"

#define TIME_1 1235142208
#define TIME_2 1235142268
#define TIME_3 1235142328

#define TIME_1s APR_STRINGIFY(TIME_1) "000000"
#define TIME_2s APR_STRINGIFY(TIME_2) "000000"
#define TIME_3s APR_STRINGIFY(TIME_3) "000000"

#define TIME_1a apr_time_from_sec(TIME_1)
#define TIME_2a apr_time_from_sec(TIME_2)
#define TIME_3a apr_time_from_sec(TIME_3)

#define AUTHOR_1 "johndoe"
#define AUTHOR_2 "janedoe"

/* Some arbitrary checksum values */
#define MD5_1 "2d18c5e57e84c5b8a5e9a6e13fa394dc"
#define SHA1_1 "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d"

#define F_TC_DATA "(conflict F file update edited deleted (version 22 " ROOT_ONE " 1 2 branch1/ft/F none) (version 22 " ROOT_ONE " 1 3 branch1/ft/F file))"
#define G_TC_DATA "(conflict G file update edited deleted (version 22 " ROOT_ONE " 1 2 branch1/ft/F none) (version 22 " ROOT_ONE " 1 3 branch1/ft/F file))"

static const char * const TESTING_DATA = (
   /* Load our test data.

      Note: do not use named-column insertions. This allows us to test
      the column count in the schema matches our expectation here. */

   "insert into repository values (1, '" ROOT_ONE "', '" UUID_ONE "'); "
   "insert into repository values (2, '" ROOT_TWO "', '" UUID_TWO "'); "
   "insert into wcroot values (1, null); "

   "insert into pristine values ('$sha1$" SHA1_1 "', NULL, 15, 1, '$md5 $" MD5_1 "'); "
   );

#define NOT_MOVED FALSE, NULL
#define NO_COPY_FROM 0, NULL, SVN_INVALID_REVNUM

static const svn_test__nodes_data_t nodes[] =
{
   /* load the base nodes into the nodes table */
  { 0, "",            "normal",         1, "", 1,               NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "A",           "normal",         1, "A", 1,              NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "B",           "excluded",       1, "B", 1,              NOT_MOVED,
    svn_node_symlink},

  { 0, "C",           "server-excluded",1, "C", 0,              NOT_MOVED,
    svn_node_unknown},

  { 0, "D",           "not-present",    1, "D", 0,              NOT_MOVED,
    svn_node_unknown},

  { 0, "E",           "incomplete",     1, "E", SVN_INVALID_REVNUM, NOT_MOVED,
    svn_node_unknown},

  { 0, "F",           "normal",         1, "G-alt", 1,          NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "G",           "normal",         1, "G-alt", 1,          NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "H",           "normal",         1, "I", 1,              NOT_MOVED,
    svn_node_symlink, NULL, NULL, NULL, "H-target", 1, TIME_1a, AUTHOR_1},

  { 0, "I",           "normal",         1, "I", 1,              NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "J",           "normal",         1, "J", 1,              NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "J/J-c",       "normal",         1, "J/J-c", 1,          NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "J/J-c/J-c-a", "not-present",    1, "J/J-c/J-c-a", 1,    NOT_MOVED,
    svn_node_dir},

  { 0, "J/J-e",       "normal",         1, "J/J-e", 1,          NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "J/J-e/J-e-a", "normal",         1, "J/J-e/J-e-a", 1,    NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "J/J-e/J-e-b", "normal",         1, "J/J-e/J-e-b", 1,    NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "J/J-e/J-e-b/Jeba", "normal",    1, "J/J-e/J-e-b/Jeba", 1, NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "J/J-f",       "normal",         1, "J/J-f", 1,          NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "J/J-f/J-f-a", "normal",         1, "J/J-f/J-f-a", 1,    NOT_MOVED,
   svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "K",           "normal",         1, "K", 1,              NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "K/K-a",       "normal",         1, "K/K-a", 1,          NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "K/K-b",       "normal",         1, "K/K-b", 1,          NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "L",           "normal",         1, "switched", 1,        NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "L/L-a",       "normal",         1, "switched/L-a", 1,    NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1},

  { 0, "L/L-a/L-a-a", "normal",         1, "switched/L-a/L-a-a", 1, NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1},

   /* Load data into NODES table;
      ### op_depths have not been calculated by me yet;
      the value 1 is just 'good enough' to make the nodes WORKING nodes. */

  { 1, "I",           "normal",         2, "some/file", 2,      NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2},

  { 1, "J",           "normal",         NO_COPY_FROM,           NOT_MOVED,
    svn_node_dir, NULL, "immediates"},

  { 2, "J/J-a",       "normal",         NO_COPY_FROM,           NOT_MOVED,
    svn_node_file},

  { 2, "J/J-b",       "normal",         2, "some/dir", 2,       NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 2, TIME_2a, AUTHOR_2},

  { 3, "J/J-b/J-b-a", "normal",         2, "another/dir", 2,    NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 2, TIME_2a, AUTHOR_2},

  { 3, "J/J-b/J-b-b", "normal",         NO_COPY_FROM,           NOT_MOVED,
    svn_node_file},

  /* This triggers a validation warning: bad delete */
  { 1, "J/J-c",       "base-deleted",   NO_COPY_FROM,           NOT_MOVED,
    svn_node_dir},

  { 1, "J/J-d",       "normal",         2, "moved/file", 2,     NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2},

  { 1, "J/J-e",       "base-deleted",   NO_COPY_FROM,           FALSE, "other/place",
    svn_node_dir},

  { 1, "J/J-e/J-e-a", "base-deleted",   NO_COPY_FROM,           NOT_MOVED,
    svn_node_file},

  { 1, "J/J-e/J-e-b", "base-deleted",   NO_COPY_FROM,           NOT_MOVED,
    svn_node_dir},

  { 1, "J/J-e/J-e-b/Jeba", "base-deleted", NO_COPY_FROM,        NOT_MOVED,
    svn_node_file},

  { 1, "J/J-f",       "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir},

  { 2, "J/J-f",       "normal",           NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir, NULL, "immediates"},

  { 1, "J/J-f/J-f-a", "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir},

  { 1, "K",           "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir},

  { 1, "K/K-a",       "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_file},

  { 1, "K/K-b",       "base-deleted",     NO_COPY_FROM,         FALSE, "moved/away",
    svn_node_file},

  { 1, "L",           "normal",           NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir, NULL, "immediates"},

  { 1, "L/L-a",       "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir},

  { 1, "L/L-a/L-a-a", "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir},

  { 1, "M",           "normal",           1, "M", 1,            NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1},

  { 1, "M/M-a",       "not-present",      1, "M/M-a", 1,        NOT_MOVED,
    svn_node_file},

  /**** Move target of K/K-b ****/
  { 1, "moved",       "normal",           NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir, NULL, "infinity" },
  { 2, "moved/away",  "normal",           1, "??", 1,           TRUE, NULL,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1},

  /**** Move target of J/J-e ****/
  { 1, "other",       "normal",           NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir, NULL, "empty"},

  { 2, "other/place", "normal",           1, "??", 1,           TRUE, NULL,
    svn_node_dir, "()", "infinity"},

  { 0 },
};

static const svn_test__actual_data_t actuals[] =
{
  { "I", NULL, "changelist", NULL },
  { "F", NULL, NULL, NULL /* TC-DATA */ },
  { "G", NULL, NULL, NULL /* TC-DATA */ },
  { 0 },
};


static const char * const M_TESTING_DATA = (
   /* Load our test data.

      Note: do not use named-column insertions. This allows us to test
      the column count in the schema matches our expectation here. */

   "insert into repository values (1, '" ROOT_ONE "', '" UUID_ONE "'); "
   "insert into repository values (2, '" ROOT_TWO "', '" UUID_TWO "'); "
   "insert into wcroot values (1, null); "

   "insert into nodes values ("
   "  1, '', 0, null, 1, 'M', 1, 'normal',"
   "  null, null, 'dir', '()', 'infinity', null, null, 1, " TIME_1s ", '" AUTHOR_1 "',"
   "  null, null, null, null, null);"
   );


static svn_error_t *
create_fake_wc(const char *subdir, apr_pool_t *pool)
{
  const char *root;
  const char *wc_abspath;

  root = svn_dirent_join("fake-wc", subdir, pool);

  SVN_ERR(svn_io_remove_dir2(root, TRUE, NULL, NULL, pool));

  SVN_ERR(svn_dirent_get_absolute(&wc_abspath, root, pool));
  SVN_ERR(svn_test__create_fake_wc(wc_abspath, TESTING_DATA, nodes, actuals,
                                   pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
create_open(svn_wc__db_t **db,
            const char **local_abspath,
            const char *subdir,
            apr_pool_t *pool)
{
  SVN_ERR(create_fake_wc(subdir, pool));

  SVN_ERR(svn_dirent_get_absolute(local_abspath,
                                  svn_dirent_join("fake-wc", subdir, pool),
                                  pool));
  SVN_ERR(svn_wc__db_open(db,
                          NULL /* config */,
                          FALSE /* not_upgraded_ok */,
                          TRUE /* enforce_empty_wq */,
                          pool, pool));

  svn_test_add_dir_cleanup(*local_abspath);

  return SVN_NO_ERROR;
}


static svn_error_t *
test_entries_alloc(apr_pool_t *pool)
{
  svn_wc__db_t *db;
  const char *local_abspath;
  svn_wc_adm_access_t *adm_access;
  apr_hash_t *entries;
  const svn_wc_entry_t *entry;
  const char *local_relpath;

#undef WC_NAME
#define WC_NAME "test_entries_alloc"

  SVN_ERR(create_open(&db, &local_abspath, WC_NAME, pool));

  SVN_ERR(svn_wc_adm_open3(&adm_access,
                           NULL /* associated */,
                           svn_dirent_join("fake-wc", WC_NAME, pool),
                           FALSE /* write_lock */,
                           0 /* levels_to_lock */,
                           NULL /* cancel_func */,
                           NULL /* cancel_baton */,
                           pool));
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE /* show_hidden */,
                              pool));

  /* The wcroot has 12 BASE children + 3 WORKING child + "this dir".  */
  SVN_TEST_ASSERT(apr_hash_count(entries) == 16);

  /* The "D" entry in the entries hash should be what we get from the
     svn_wc_entry() entrypoint.  */
  local_relpath = svn_dirent_join_many(pool,
                                       "fake-wc",
                                       WC_NAME,
                                       "D",
                                       SVN_VA_NULL);
  SVN_ERR(svn_wc_entry(&entry, local_relpath, adm_access, TRUE, pool));
  SVN_TEST_ASSERT(entry == apr_hash_get(entries, "D", APR_HASH_KEY_STRING));

  /* This entry should be missing.  */
  SVN_ERR(svn_wc_entry(&entry, "missing", adm_access, TRUE, pool));
  SVN_TEST_ASSERT(entry == NULL);

  return SVN_NO_ERROR;
}


static svn_error_t *
test_stubs(apr_pool_t *pool)
{
  svn_wc__db_t *db;
  const char *local_abspath;
  const char *local_relpath;
  svn_wc_adm_access_t *adm_access;
  svn_wc_adm_access_t *subdir_access;
  const svn_wc_entry_t *stub_entry;
  const svn_wc_entry_t *entry;
  const svn_wc_entry_t *test_entry;
  const char *M_dir;
  apr_hash_t *entries;

#undef WC_NAME
#define WC_NAME "test_stubs"

  SVN_ERR(create_open(&db, &local_abspath, WC_NAME, pool));

  M_dir = svn_dirent_join(local_abspath, "M", pool);
  SVN_ERR(svn_test__create_fake_wc(M_dir, M_TESTING_DATA, NULL, NULL, pool));

  /* The "M" entry is a subdir. Let's ensure we can reach its stub,
     and the actual contents.  */
  local_relpath = svn_dirent_join_many(pool,
                                       "fake-wc",
                                       WC_NAME,
                                       "M",
                                       SVN_VA_NULL);

  SVN_ERR(svn_wc_adm_open3(&adm_access,
                           NULL /* associated */,
                           svn_dirent_join("fake-wc", WC_NAME, pool),
                           FALSE /* write_lock */,
                           0 /* levels_to_lock */,
                           NULL /* cancel_func */,
                           NULL /* cancel_baton */,
                           pool));

  /* Ensure we get the stub. NOTE: do this before we have associated the
     subdir baton with ADM_ACCESS.  */
  SVN_ERR(svn_wc_entry(&stub_entry, local_relpath, adm_access, TRUE, pool));
  SVN_TEST_STRING_ASSERT(stub_entry->name, "M");
  /* Schedule add in parent-wc. Schedule normal in obstructing working copy */
  SVN_TEST_ASSERT(stub_entry->schedule == svn_wc_schedule_add);

  SVN_ERR(svn_wc_adm_open3(&subdir_access,
                           adm_access,
                           local_relpath,
                           FALSE /* write_lock */,
                           0 /* levels_to_lock */,
                           NULL /* cancel_func */,
                           NULL /* cancel_baton */,
                           pool));

  /* Ensure we get the real entry.  */
  SVN_ERR(svn_wc_entry(&entry, local_relpath, subdir_access, TRUE, pool));
  SVN_TEST_STRING_ASSERT(entry->name, "");
  SVN_TEST_ASSERT(entry->schedule == svn_wc_schedule_normal);

  /* Ensure that we get the SAME entry, even using the parent baton.  */
  SVN_ERR(svn_wc_entry(&test_entry, local_relpath, adm_access, TRUE, pool));
  SVN_TEST_ASSERT(test_entry == entry);

  /* Ensure we get the stub when reading entries with ADM_ACCESS.  */
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE /* show_hidden */,
                              pool));
  SVN_TEST_ASSERT(stub_entry
                  == apr_hash_get(entries, "M", APR_HASH_KEY_STRING));

  /* Ensure we get the real entry when reading entries with SUBDIR_ACCESS.  */
  SVN_ERR(svn_wc_entries_read(&entries, subdir_access, TRUE /* show_hidden */,
                              pool));
  SVN_TEST_ASSERT(entry
                  == apr_hash_get(entries, "", APR_HASH_KEY_STRING));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_access_baton_like_locking(apr_pool_t *pool)
{
  svn_wc__db_t *db;
  svn_wc_context_t *wc_ctx, *wc_ctx2;
  const char *local_abspath;
  const char *D, *D1, *D2, *D3, *D4;
  svn_boolean_t locked_here, locked;
  svn_error_t *err;
  svn_wc_adm_access_t *adm_access, *subdir_access;

#undef WC_NAME
#define WC_NAME "test_access_batons"
  SVN_ERR(create_open(&db, &local_abspath, WC_NAME, pool));

  D = svn_dirent_join(local_abspath, "DD", pool);

  D1 = svn_dirent_join(D, "DD", pool);
  D2 = svn_dirent_join(D1, "DD", pool);
  D3 = svn_dirent_join(D2, "DD", pool);
  D4 = svn_dirent_join(D3, "DD", pool);

  SVN_ERR(svn_io_make_dir_recursively(D4, pool));

  /* Use the legacy interface */
  SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, local_abspath, TRUE, 0,
                           NULL, NULL, pool));
  SVN_ERR(svn_wc_add3(D, adm_access, svn_depth_infinity, NULL,
                      SVN_INVALID_REVNUM, NULL, NULL, NULL, NULL, pool));
  SVN_ERR(svn_wc_adm_retrieve(&subdir_access, adm_access, D, pool));
  SVN_ERR(svn_wc_add3(D1, subdir_access, svn_depth_infinity, NULL,
                      SVN_INVALID_REVNUM, NULL, NULL, NULL, NULL, pool));
  SVN_ERR(svn_wc_adm_retrieve(&subdir_access, adm_access, D1, pool));
  SVN_ERR(svn_wc_add3(D2, subdir_access, svn_depth_infinity, NULL,
                      SVN_INVALID_REVNUM, NULL, NULL, NULL, NULL, pool));
  SVN_ERR(svn_wc_adm_retrieve(&subdir_access, adm_access, D2, pool));
  SVN_ERR(svn_wc_add3(D3, subdir_access, svn_depth_infinity, NULL,
                      SVN_INVALID_REVNUM, NULL, NULL, NULL, NULL, pool));
  SVN_ERR(svn_wc_add3(D4, subdir_access, svn_depth_infinity, NULL,
                      SVN_INVALID_REVNUM, NULL, NULL, NULL, NULL, pool));
  SVN_ERR(svn_wc_locked(&locked, D3, pool));
  SVN_TEST_ASSERT(locked);
  SVN_ERR(svn_wc_locked(&locked, D4, pool));
  SVN_TEST_ASSERT(locked);
  SVN_ERR(svn_wc_delete3(D4, subdir_access, NULL, NULL, NULL, NULL, FALSE,
                         pool));
  SVN_ERR(svn_wc_locked(&locked, D4, pool));
  SVN_TEST_ASSERT(!locked);
  SVN_ERR(svn_wc_revert3(D, adm_access, svn_depth_infinity, FALSE,
                         NULL, NULL, NULL, NULL, NULL, pool));
  SVN_ERR(svn_wc_locked(&locked, D3, pool));
  SVN_TEST_ASSERT(!locked);
  SVN_ERR(svn_wc_locked(&locked, local_abspath, pool));
  SVN_TEST_ASSERT(locked);
  SVN_ERR(svn_wc_adm_close2(adm_access, pool));

  SVN_ERR(svn_wc_context_create(&wc_ctx, NULL, pool, pool));

  /* Obtain a lock for the root, which is extended on each level */
  SVN_ERR(svn_wc__db_wclock_obtain(wc_ctx->db, local_abspath, 0, FALSE, pool));
  SVN_ERR(svn_io_make_dir_recursively(D4, pool));
  SVN_ERR(svn_wc_add4(wc_ctx, D, svn_depth_infinity, NULL, SVN_INVALID_REVNUM,
                      NULL, NULL, NULL, NULL, pool));
  SVN_ERR(svn_wc_add4(wc_ctx, D1, svn_depth_infinity, NULL, SVN_INVALID_REVNUM,
                      NULL, NULL, NULL, NULL, pool));
  SVN_ERR(svn_wc_add4(wc_ctx, D2, svn_depth_infinity, NULL, SVN_INVALID_REVNUM,
                      NULL, NULL, NULL, NULL, pool));
  SVN_ERR(svn_wc_add4(wc_ctx, D3, svn_depth_infinity, NULL, SVN_INVALID_REVNUM,
                      NULL, NULL, NULL, NULL, pool));

  SVN_ERR(svn_wc_locked2(&locked_here, &locked, wc_ctx, D3, pool));
  SVN_TEST_ASSERT(locked_here && locked);

  /* Test if the not added path is already locked */
  SVN_ERR(svn_wc_locked2(&locked_here, &locked, wc_ctx, D4, pool));
  SVN_TEST_ASSERT(!locked_here && !locked);

  SVN_ERR(svn_wc_add4(wc_ctx, D4, svn_depth_infinity, NULL, SVN_INVALID_REVNUM,
                      NULL, NULL, NULL, NULL, pool));

  SVN_ERR(svn_wc_locked2(&locked_here, &locked, wc_ctx, D4, pool));
  SVN_TEST_ASSERT(locked_here && locked);

  SVN_ERR(svn_wc__db_wclock_release(wc_ctx->db, local_abspath, pool));
  /* Should be unlocked */
  SVN_ERR(svn_wc_locked2(&locked_here, &locked, wc_ctx, local_abspath, pool));
  SVN_TEST_ASSERT(!locked_here && !locked);

  /* Lock shouldn't be released */
  SVN_ERR(svn_wc_locked2(&locked_here, &locked, wc_ctx, D, pool));
  SVN_TEST_ASSERT(locked_here && locked);

  SVN_ERR(svn_wc__db_wclock_release(wc_ctx->db, D, pool));
  SVN_ERR(svn_wc__db_wclock_release(wc_ctx->db, D1, pool));
  SVN_ERR(svn_wc__db_wclock_release(wc_ctx->db, D2, pool));
  SVN_ERR(svn_wc__db_wclock_release(wc_ctx->db, D3, pool));

  /* Try reobtaining lock on D3; should succeed */
  SVN_ERR(svn_wc__db_wclock_obtain(wc_ctx->db, D3, 0, FALSE, pool));
  SVN_ERR(svn_wc__db_wclock_release(wc_ctx->db, D4, pool));


  /* D3 should still be locked; try stealing in a different context */
  SVN_ERR(svn_wc_context_create(&wc_ctx2, NULL, pool, pool));
  SVN_ERR(svn_wc_locked2(&locked_here, &locked, wc_ctx2, D3, pool));
  SVN_TEST_ASSERT(!locked_here && locked);

  err = svn_wc__db_wclock_obtain(wc_ctx2->db, D3, 0, FALSE, pool);

  if (err && err->apr_err != SVN_ERR_WC_LOCKED)
    return svn_error_trace(err);
  svn_error_clear(err);

  SVN_TEST_ASSERT(err != NULL); /* Can't lock, as it is still locked */

  err = svn_wc__db_wclock_release(wc_ctx2->db, D4, pool);
  if (err && err->apr_err != SVN_ERR_WC_NOT_LOCKED)
    return svn_error_trace(err);
  svn_error_clear(err);

  SVN_TEST_ASSERT(err != NULL); /* Can't unlock, as it is not ours */

  /* Now steal the lock */
  SVN_ERR(svn_wc__db_wclock_obtain(wc_ctx2->db, D3, 0, TRUE, pool));

  /* We should own the lock now */
  SVN_ERR(svn_wc_locked2(&locked_here, &locked, wc_ctx2, D3, pool));
  SVN_TEST_ASSERT(locked_here && locked);

  err = svn_wc__db_wclock_release(wc_ctx2->db, D4, pool);
  if (err && err->apr_err != SVN_ERR_WC_NOT_LOCKED)
    return svn_error_trace(err);
  svn_error_clear(err);

  SVN_TEST_ASSERT(err != NULL); /* Can't unlock a not locked path */

  /* Now create a separate working copy from the same repository directly
     below this WC and test if our code really sees it as a separate wc,
     for locking and normal operation */
  {
    const char *url, *repos_root_url, *repos_uuid;
    const char *subdir = svn_dirent_join(local_abspath, "sub-wc", pool);
    const char *repos_relpath;

    svn_boolean_t is_root;
    SVN_ERR(svn_wc__node_get_repos_info(NULL, &repos_relpath,
                                        &repos_root_url, &repos_uuid,
                                        wc_ctx, local_abspath,
                                        pool, pool));
    url = svn_path_url_add_component2(repos_root_url, repos_relpath, pool);

    SVN_ERR(svn_io_make_dir_recursively(subdir, pool));
    SVN_ERR(svn_wc_ensure_adm3(subdir, repos_uuid,
                               svn_path_url_add_component2(url, "sub-wc", pool),
                               repos_root_url, 0, svn_depth_infinity,
                               pool));

    SVN_ERR(svn_wc__db_is_switched(&is_root, NULL, NULL, wc_ctx->db, subdir,
                                   pool));

    SVN_TEST_ASSERT(is_root);

    SVN_ERR(svn_wc__db_is_switched(&is_root, NULL, NULL, wc_ctx2->db, subdir,
                                   pool));

    /* This test was added to show a regression where the next check failed,
       but the check above this succeeded */
    SVN_TEST_ASSERT(is_root);

    SVN_ERR(svn_wc_locked2(&locked_here, &locked, wc_ctx2, subdir, pool));
    SVN_TEST_ASSERT(!locked_here && !locked);
  }

  return SVN_NO_ERROR;
}


static int max_threads = -1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_entries_alloc,
                   "entries are allocated in access baton"),
    SVN_TEST_PASS2(test_stubs,
                   "access baton mojo can return stubs"),
    SVN_TEST_PASS2(test_access_baton_like_locking,
                   "access baton like locks must work with wc-ng"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
