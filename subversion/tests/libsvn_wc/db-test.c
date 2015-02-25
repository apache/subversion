/*
 * db-test.c :  test the wc_db subsystem
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

#include "private/svn_sqlite.h"

#include "../../libsvn_wc/wc_db.h"

#include "private/svn_wc_private.h"

#include "../svn_test.h"
#include "utils.h"


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

static const svn_test__nodes_data_t nodes_init_data[] = {
   /* load the base nodes into the nodes table */
  { 0, "",            "normal",           1, "", 1,             NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1 },

  { 0, "A",           "normal",           1, "A", 1,            NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1,
    FALSE, NULL, 10, 10 },

  { 0, "B",           "excluded",         1, "B", SVN_INVALID_REVNUM, NOT_MOVED,
    svn_node_symlink},

  { 0, "C",           "server-excluded",  1, "C", 0,            NOT_MOVED,
    svn_node_unknown},

  { 0, "D",           "not-present",      1, "D", 0,            NOT_MOVED,
    svn_node_unknown},

  { 0, "E",           "incomplete",       1, "E", 1,            NOT_MOVED,
    svn_node_unknown},

  { 0, "F",           "normal",           1, "F", 1,            NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2},

  { 0, "G",           "normal",           2, "G-alt", 1,        NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2 },

  { 0, "H",           "normal",           1, "H", 1,            NOT_MOVED,
    svn_node_symlink, "()", NULL, NULL, "H-target", 1, TIME_1a, AUTHOR_1 },

  { 0, "I",           "normal",           1, "I", 1,            NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1 },

  { 0, "J",           "normal",           1, "J", 1,            NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1 },

  { 0, "J/J-e",       "normal",           1, "J/J-e", 1,        NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1 },

  { 0, "J/J-e/J-e-a", "normal",           1, "J/J-e/J-e-a", 1,  NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1 },

  { 0, "J/J-e/J-e-b", "normal",           1, "J/J-e/J-e-b", 1,  NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1 },

  { 0, "J/J-e/J-e-b/Jeba", "normal",      1, "J/J-e/J-e-b/Jeba", 1,  NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1 },

  { 0, "J/J-f",       "normal",           1, "J/J-f", 1,        NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1 },

  { 0, "J/J-f/J-f-a", "normal",           1, "J/J-f/J-f-a", 1,  NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1 },

  { 0, "J",           "normal",           1, "J", 1,            NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1 },

  { 0, "K",           "normal",           1, "K", 1,            NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1 },

  { 0, "K/K-a",       "normal",           2, "K/K-a", 1,        NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2,
    FALSE, NULL, 15, 14},

  { 0, "K/K-b",       "normal",           2, "K/K-b", 1,        NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2,
    FALSE, NULL, 15, 14},

   /* Load data into the working layers of NODES */

  { 1, "I",           "normal",           2, "some/dir", 2,     NOT_MOVED,
    svn_node_dir, "()", "immediates", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

   /* J was originally a local addition, but its descendants are replaced,
      so let's turn J in a copy */
  { 1, "J",           "normal",           2, "q", 2,            NOT_MOVED,
    svn_node_dir, "()", "immediates", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 1, "J/J-a",       "normal",           2, "q/J-a", 2,        NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2 },

  { 1, "J/J-b",       "normal",           2, "q/J-b", 2,        NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 3, "J/J-b/J-b-a", "normal",           2, "another/dir", 2,  NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 1, "J/J-b/J-b-b", "normal",           2, "q/J-b/J-b-b", 2,  NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2 },

  { 1, "J/J-c",       "normal",           2, "q/J-c", 2,        NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 1, "J/J-c/J-c-a", "normal",           2, "q/J-c/J-c-a", 2,  NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 2, "J/J-c",       "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir},

  { 2, "J/J-c/J-c-a", "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir},

  { 2, "J/J-d",       "normal",           2, "moved/file", 2,   TRUE, NULL,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2 },

  { 0, "moved",       "normal",           2, "moved", 2,        NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 1, TIME_1a, AUTHOR_1 },

  { 0, "moved/file",  "normal",           2, "moved/file", 2,   NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2 },

  { 2, "moved/file",  "base-deleted",     NO_COPY_FROM,         FALSE, "J/J-d",
    svn_node_file},

  { 1, "J/J-e",       "normal",           2, "q/J-e", 2,        NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 1, "J/J-e/J-e-a", "normal",           2, "q/J-e/J-e-a", 2,  NOT_MOVED,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2 },

  { 1, "J/J-e/J-e-b", "normal",           2, "q/J-e/J-e-b", 2,  NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 2, "J/J-e",       "base-deleted",     NO_COPY_FROM,         FALSE, "other/place",
    svn_node_dir},

  { 2, "J/J-e/J-e-a", "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_file},

  { 2, "J/J-e/J-e-b", "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir},

  { 1, "J/J-e/J-e-b/Jeba", "base-deleted", NO_COPY_FROM,        NOT_MOVED,
    svn_node_file},

  { 1, "J/J-f",       "normal",           2, "q/J-f", 2,        NOT_MOVED,
    svn_node_dir, "()", "immediates", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 1, "J/J-f/J-f-a", "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir},

  { 1, "K",           "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir},

  { 1, "K/K-a",       "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_file},

  { 1, "K/K-b",       "base-deleted",     NO_COPY_FROM,         FALSE, "moved/away",
    svn_node_file},

  { 1, "L",           "normal",           2, "from", 2,         NOT_MOVED,
    svn_node_dir, "()", "immediates", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 1, "L/L-a",       "normal",           2, "from/L-a", 2,     NOT_MOVED,
    svn_node_dir, "()", "immediates", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 1, "L/L-a/L-a-a", "normal",           2, "from/L-a/L-a-a", 2, NOT_MOVED,
    svn_node_dir, "()", "immediates", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 2, "L/L-a",       "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir},

  { 2, "L/L-a/L-a-a", "base-deleted",     NO_COPY_FROM,         NOT_MOVED,
    svn_node_dir},

  { 0, "other",       "normal",           2, "other", 2,        NOT_MOVED,
    svn_node_dir, "()", "infinity", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 2, "other/place", "normal",           2, "q/J-e", 2,        TRUE, NULL,
    svn_node_dir, "()", "immediates", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 2, "other/place/J-e-a", "normal",     2, "q/J-e/J-e-a", 2,  TRUE, NULL,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2 },

  { 2, "other/place/J-e-b", "normal",     2, "q/J-e/J-e-b", 2,  TRUE, NULL,
    svn_node_dir, "()", "infinity", NULL, NULL, 2, TIME_2a, AUTHOR_2 },

  { 2, "other/place/J-e-b/Jeba", "normal", 2, "q/J-e/J-e-b/Jeba", 2,  TRUE, NULL,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 1, TIME_1a, AUTHOR_1 },

  /*** NEW ****/
  { 2, "moved/away",  "normal",           2, "K/K-b", 1,        TRUE, NULL,
    svn_node_file, "()", NULL, "$sha1$" SHA1_1, NULL, 2, TIME_2a, AUTHOR_2,
    FALSE, NULL, 15, 14},
  { 0 }
};

static const svn_test__actual_data_t actual_init_data[] = {
  { "A", NULL, "changelist", NULL },
  { "F", NULL, NULL, F_TC_DATA },
  { "G", NULL, NULL, F_TC_DATA },

  { 0 }
};

static svn_error_t *
create_open(svn_wc__db_t **db,
            const char **local_abspath,
            const char *subdir,
            apr_pool_t *pool)
{
  SVN_ERR(svn_dirent_get_absolute(local_abspath,
                                  svn_dirent_join(
                                        svn_test_data_path("db-test", pool),
                                        subdir, pool),
                                  pool));

  SVN_ERR(svn_io_remove_dir2(*local_abspath, TRUE, NULL, NULL, pool));

  SVN_ERR(svn_wc__db_open(db, NULL, FALSE, TRUE, pool, pool));
  SVN_ERR(svn_test__create_fake_wc(*local_abspath, TESTING_DATA,
                                   nodes_init_data, actual_init_data, pool));

  svn_test_add_dir_cleanup(*local_abspath);

  return SVN_NO_ERROR;
}


/* Convert VALUE to a const svn_string_t *, and create a mapping from
   NAME to the converted data type in PROPS. */
static void
set_prop(apr_hash_t *props, const char *name, const char *value,
         apr_pool_t *result_pool)
{
  const svn_string_t *propval = svn_string_create(value, result_pool);

  apr_hash_set(props, name, APR_HASH_KEY_STRING, propval);
}


static svn_error_t *
validate_abspath(const char *wcroot_abspath,
                 const char *expected_relpath,
                 const char *actual_abspath,
                 apr_pool_t *scratch_pool)
{
  SVN_TEST_STRING_ASSERT(actual_abspath,
                         svn_dirent_join(wcroot_abspath,
                                expected_relpath,
                                scratch_pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
test_getting_info(apr_pool_t *pool)
{
  const char *local_abspath;
  svn_node_kind_t kind;
  svn_wc__db_status_t status;
  svn_revnum_t revision;
  const char *repos_relpath;
  const char *repos_root_url;
  const char *repos_uuid;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  svn_depth_t depth;
  const svn_checksum_t *checksum;
  const char *target;
  svn_boolean_t had_props;
  apr_hash_t *props;
  svn_boolean_t update_root;
  svn_wc__db_lock_t *lock;
  svn_wc__db_t *db;
  svn_error_t *err;

  SVN_ERR(create_open(&db, &local_abspath, "test_getting_info", pool));

  /* Test: basic fetching of data. */
  SVN_ERR(svn_wc__db_base_get_info(
            &status, &kind, &revision,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &changed_rev, &changed_date, &changed_author,&depth, &checksum,
            &target, &lock, &had_props, &props,
            &update_root,
            db, local_abspath,
            pool, pool));
  SVN_TEST_ASSERT(kind == svn_node_dir);
  SVN_TEST_ASSERT(status == svn_wc__db_status_normal);
  SVN_TEST_ASSERT(revision == 1);
  SVN_TEST_STRING_ASSERT(repos_relpath, "");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_ASSERT(changed_rev == 1);
  SVN_TEST_ASSERT(changed_date == TIME_1a);
  SVN_TEST_STRING_ASSERT(changed_author, AUTHOR_1);
  SVN_TEST_ASSERT(depth == svn_depth_infinity);
  SVN_TEST_ASSERT(checksum == NULL);
  SVN_TEST_ASSERT(target == NULL);
  SVN_TEST_ASSERT(lock == NULL);
  SVN_TEST_ASSERT(!had_props);
  SVN_TEST_ASSERT(apr_hash_count(props) == 0);
  /* SVN_TEST_ASSERT(update_root == ???); */

  /* Test: file-specific values. */
  SVN_ERR(svn_wc__db_base_get_info(
            NULL, &kind, NULL,
            &repos_relpath, &repos_root_url, &repos_uuid,
            NULL, NULL, NULL, NULL,
            &checksum, NULL, NULL,
            NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "A", pool),
            pool, pool));
  SVN_TEST_ASSERT(kind == svn_node_file);
  SVN_TEST_STRING_ASSERT(SHA1_1, svn_checksum_to_cstring(checksum, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "A");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);

  /* Test: symlink kind, excluded presence, default values for columns. */
  SVN_ERR(svn_wc__db_base_get_info(
            &status, &kind, &revision,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &changed_rev, &changed_date, &changed_author,
            &depth, &checksum, &target, &lock,
            NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "B", pool),
            pool, pool));
  SVN_TEST_ASSERT(kind == svn_node_symlink);
  SVN_TEST_ASSERT(status == svn_wc__db_status_excluded);
  SVN_TEST_ASSERT(!SVN_IS_VALID_REVNUM(revision));
  SVN_TEST_STRING_ASSERT(repos_relpath, "B");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_ASSERT(!SVN_IS_VALID_REVNUM(changed_rev));
  SVN_TEST_ASSERT(changed_date == 0);
  SVN_TEST_ASSERT(changed_author == NULL);
  SVN_TEST_ASSERT(depth == svn_depth_unknown);
  SVN_TEST_ASSERT(checksum == NULL);
  SVN_TEST_ASSERT(target == NULL);
  SVN_TEST_ASSERT(lock == NULL);

  /* Test: unknown kind, server-excluded presence. */
  SVN_ERR(svn_wc__db_base_get_info(
            &status, &kind, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "C", pool),
            pool, pool));
  SVN_TEST_ASSERT(kind == svn_node_unknown);
  SVN_TEST_ASSERT(status == svn_wc__db_status_server_excluded);

  /* Test: not-present presence. */
  SVN_ERR(svn_wc__db_base_get_info(
            &status, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "D", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_not_present);

  /* Test: incomplete presence. */
  SVN_ERR(svn_wc__db_base_get_info(
            &status, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "E", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_incomplete);

  /* Test: SHA1 checksum. */
  SVN_ERR(svn_wc__db_base_get_info(
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, &checksum, NULL,
            NULL, NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "F", pool),
            pool, pool));
  SVN_TEST_STRING_ASSERT(SHA1_1,
                         svn_checksum_to_cstring(checksum, pool));

  /* Test: alternate repository (switched file). */
  SVN_ERR(svn_wc__db_base_get_info(
            NULL, NULL, NULL,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &changed_rev, &changed_date, &changed_author,
            NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "G", pool),
            pool, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "G-alt");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_TWO);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_TWO);
  SVN_TEST_ASSERT(changed_rev == 2);
  SVN_TEST_ASSERT(changed_date == TIME_2a);
  SVN_TEST_STRING_ASSERT(changed_author, AUTHOR_2);

  /* Test: symlink target. */
  SVN_ERR(svn_wc__db_base_get_info(
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, &checksum, &target,
            NULL, NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "H", pool),
            pool, pool));
  SVN_TEST_ASSERT(checksum == NULL);
  SVN_TEST_STRING_ASSERT(target, "H-target");

  /* Test: missing node. */
  err = svn_wc__db_base_get_info(
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "missing-file", pool),
            pool, pool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_WC_PATH_NOT_FOUND);

  return SVN_NO_ERROR;
}


static svn_error_t *
validate_node(svn_wc__db_t *db,
              const char *local_abspath,
              const char *relpath,
              svn_node_kind_t expected_kind,
              svn_wc__db_status_t expected_status,
              apr_pool_t *scratch_pool)
{
  const char *path = svn_dirent_join(local_abspath, relpath, scratch_pool);
  svn_node_kind_t kind;
  svn_wc__db_status_t status;
  apr_hash_t *props;
  const svn_string_t *value;

  SVN_ERR(svn_wc__db_base_get_info(
            &status, &kind, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            db, path,
            scratch_pool, scratch_pool));
  SVN_TEST_ASSERT(kind == expected_kind);
  SVN_TEST_ASSERT(status == expected_status);

  switch (status)
    {
    case svn_wc__db_status_server_excluded:
    case svn_wc__db_status_excluded:
    case svn_wc__db_status_incomplete:
    case svn_wc__db_status_not_present:
      /* Our tests aren't setting properties on these node types, so
         short-circuit examination of name/value pairs, to avoid having
         to handle the error from svn_wc__db_base_get_props(). */
      return SVN_NO_ERROR;
    default:
      break; /* Fall through */
    }

  SVN_ERR(svn_wc__db_base_get_props(&props, db, path,
                                    scratch_pool, scratch_pool));

  SVN_TEST_ASSERT(props != NULL);

  value = apr_hash_get(props, "p1", APR_HASH_KEY_STRING);
  SVN_TEST_STRING_ASSERT(value->data, "v1");

  value = apr_hash_get(props, "for-file", APR_HASH_KEY_STRING);
  SVN_TEST_STRING_ASSERT(value->data, relpath);

  SVN_ERR(svn_wc__db_read_props(&props, db, path,
                                scratch_pool, scratch_pool));
  SVN_TEST_ASSERT(props != NULL);
  value = apr_hash_get(props, "p1", APR_HASH_KEY_STRING);
  SVN_TEST_STRING_ASSERT(value->data, "v1");

  SVN_ERR(svn_wc__db_read_pristine_props(&props, db, path,
                                         scratch_pool, scratch_pool));
  SVN_TEST_ASSERT(props != NULL);
  value = apr_hash_get(props, "p1", APR_HASH_KEY_STRING);
  SVN_TEST_STRING_ASSERT(value->data, "v1");

  /* Now add a property value and read it back (all on actual) */
  {
    apr_hash_t *actual_props = apr_hash_copy(scratch_pool, props);
    apr_hash_set(actual_props, "p999", APR_HASH_KEY_STRING, value);
    SVN_ERR(svn_wc__db_op_set_props(db, path, actual_props, FALSE,
                                    NULL, NULL, scratch_pool));
    SVN_ERR(svn_wc__db_read_props(&props, db, path,
                                  scratch_pool, scratch_pool));
    SVN_TEST_ASSERT(props != NULL);
    value = apr_hash_get(props, "p999", APR_HASH_KEY_STRING);
    SVN_TEST_STRING_ASSERT(value->data, "v1");
  }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_inserting_nodes(apr_pool_t *pool)
{
  const char *local_abspath;
  svn_checksum_t *checksum;
  svn_wc__db_t *db;
  apr_hash_t *props;
  const apr_array_header_t *children;

  SVN_ERR(create_open(&db, &local_abspath, "test_insert_nodes", pool));

  props = apr_hash_make(pool);
  set_prop(props, "p1", "v1", pool);

  children = svn_cstring_split("N-a N-b N-c", " ", FALSE, pool);

  SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_sha1, SHA1_1, pool));

  /* Create a new directory and several child nodes. */
  set_prop(props, "for-file", "N", pool);
  SVN_ERR(svn_wc__db_base_add_directory(
            db, svn_dirent_join(local_abspath, "N", pool),
            local_abspath,
            "N", ROOT_ONE, UUID_ONE, 3,
            props,
            1, TIME_1a, AUTHOR_1,
            children, svn_depth_infinity,
            NULL, FALSE, NULL, NULL, NULL, NULL,
            pool));

  /* Replace an incomplete node with a file node. */
  set_prop(props, "for-file", "N/N-a", pool);
  SVN_ERR(svn_wc__db_base_add_file(
            db, svn_dirent_join(local_abspath, "N/N-a", pool),
            local_abspath,
            "N/N-a", ROOT_ONE, UUID_ONE, 3,
            props,
            1, TIME_1a, AUTHOR_1,
            checksum,
            NULL, FALSE, FALSE, NULL, NULL, FALSE, FALSE,
            NULL, NULL,
            pool));

  /* Create a new symlink node. */
  set_prop(props, "for-file", "O", pool);
  SVN_ERR(svn_wc__db_base_add_symlink(
            db, svn_dirent_join(local_abspath, "O", pool),
            local_abspath,
            "O", ROOT_ONE, UUID_ONE, 3,
            props,
            1, TIME_1a, AUTHOR_1,
            "O-target",
            NULL, FALSE, FALSE, NULL, NULL, FALSE, FALSE,
            NULL, NULL,
            pool));

  /* Replace an incomplete node with an server-excluded file node. */
  SVN_ERR(svn_wc__db_base_add_excluded_node(
            db, svn_dirent_join(local_abspath, "N/N-b", pool),
            "N/N-b", ROOT_ONE, UUID_ONE, 3,
            svn_node_file, svn_wc__db_status_server_excluded,
            NULL, NULL,
            pool));

  /* Create a new excluded directory node. */
  SVN_ERR(svn_wc__db_base_add_excluded_node(
            db, svn_dirent_join(local_abspath, "P", pool),
            "P", ROOT_ONE, UUID_ONE, 3,
            svn_node_dir, svn_wc__db_status_excluded,
            NULL, NULL,
            pool));

  /* Create a new not-present symlink node. */
  SVN_ERR(svn_wc__db_base_add_not_present_node(
            db, svn_dirent_join(local_abspath, "Q", pool),
            "Q", ROOT_ONE, UUID_ONE, 3,
            svn_node_symlink,
            NULL, NULL,
            pool));

  /* Create a new server-excluded unknown-kind node. */
  SVN_ERR(svn_wc__db_base_add_excluded_node(
            db, svn_dirent_join(local_abspath, "R", pool),
            "R", ROOT_ONE, UUID_ONE, 3,
            svn_node_unknown, svn_wc__db_status_server_excluded,
            NULL, NULL,
            pool));


  /* Are all the nodes where we expect them to be? */
  SVN_ERR(validate_node(db, local_abspath, "N",
                        svn_node_dir, svn_wc__db_status_normal,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "N/N-a",
                        svn_node_file, svn_wc__db_status_normal,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "N/N-b",
                        svn_node_file,
                        svn_wc__db_status_server_excluded,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "N/N-c",
                        svn_node_unknown, svn_wc__db_status_incomplete,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "O",
                        svn_node_symlink, svn_wc__db_status_normal,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "P",
                        svn_node_dir, svn_wc__db_status_excluded,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "Q",
                        svn_node_symlink, svn_wc__db_status_not_present,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "R",
                        svn_node_unknown,
                        svn_wc__db_status_server_excluded,
                        pool));

  /* ### do we need to test any attributes of the node? */

  /* ### yes: test the repos inheritance stuff (at least) */

  return SVN_NO_ERROR;
}


static svn_error_t *
test_children(apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc__db_t *db;
  const apr_array_header_t *children;
  int i;

  SVN_ERR(create_open(&db, &local_abspath, "test_children", pool));

  SVN_ERR(svn_wc__db_base_get_children(&children,
                                       db, local_abspath,
                                       pool, pool));
  SVN_TEST_ASSERT(children->nelts == 13);
  for (i = children->nelts; i--; )
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);

      if (strcmp(name, "moved") == 0
          || strcmp(name, "other") == 0)
        {
          continue;
        }

      SVN_TEST_ASSERT(strlen(name) == 1);
      /* ### check the actual values */
    }

  SVN_ERR(svn_wc__db_read_children(&children,
                                   db, local_abspath,
                                   pool, pool));
  SVN_TEST_ASSERT(children->nelts == 14);
  for (i = children->nelts; i--; )
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);

      if (strcmp(name, "moved") == 0
          || strcmp(name, "other") == 0)
        {
          continue;
        }

      SVN_TEST_ASSERT(strlen(name) == 1);
      /* ### check the actual values */
    }

  /* ### insert some more children. replace some nodes. check values. */

  return SVN_NO_ERROR;
}


static svn_error_t *
test_working_info(apr_pool_t *pool)
{
  const char *local_abspath;
  svn_node_kind_t kind;
  svn_wc__db_status_t status;
  svn_revnum_t revision;
  const char *repos_relpath;
  const char *repos_root_url;
  const char *repos_uuid;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  apr_time_t recorded_time;
  svn_depth_t depth;
  const svn_checksum_t *checksum;
  svn_filesize_t recorded_size;
  const char *target;
  const char *changelist;
  const char *original_repos_relpath;
  const char *original_root_url;
  const char *original_uuid;
  svn_revnum_t original_revnum;
  svn_boolean_t op_root;
  svn_boolean_t had_props;
  svn_boolean_t props_mod;
  svn_boolean_t have_base;
  svn_boolean_t have_more_work;
  svn_boolean_t have_work;
  svn_boolean_t conflicted;
  svn_wc__db_lock_t *lock;
  svn_wc__db_t *db;

  SVN_ERR(create_open(&db, &local_abspath, "test_working_info", pool));

  /* Test: basic fetching of data. */
  SVN_ERR(svn_wc__db_read_info(
            &status, &kind, &revision,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &changed_rev, &changed_date, &changed_author,
            &depth, &checksum, &target, &original_repos_relpath,
            &original_root_url, &original_uuid, &original_revnum,
            &lock, &recorded_size, &recorded_time, &changelist,
            &conflicted, &op_root, &had_props, &props_mod,
            &have_base, &have_more_work, &have_work,
            db, svn_dirent_join(local_abspath, "I", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_added);
  SVN_TEST_ASSERT(kind == svn_node_dir);
  SVN_TEST_ASSERT(revision == SVN_INVALID_REVNUM);
  SVN_TEST_ASSERT(repos_relpath == NULL);
  SVN_TEST_ASSERT(repos_root_url == NULL);
  SVN_TEST_ASSERT(repos_uuid == NULL);
  SVN_TEST_ASSERT(changed_rev == 2);
  SVN_TEST_ASSERT(changed_date == TIME_2a);
  SVN_TEST_STRING_ASSERT(changed_author, AUTHOR_2);
  SVN_TEST_ASSERT(depth == svn_depth_immediates);
  SVN_TEST_ASSERT(checksum == NULL);
  SVN_TEST_ASSERT(recorded_size == SVN_INVALID_FILESIZE);
  SVN_TEST_ASSERT(target == NULL);
  SVN_TEST_STRING_ASSERT(changelist, NULL);
  SVN_TEST_STRING_ASSERT(original_repos_relpath, "some/dir");
  SVN_TEST_STRING_ASSERT(original_root_url, ROOT_TWO);
  SVN_TEST_STRING_ASSERT(original_uuid, UUID_TWO);
  SVN_TEST_ASSERT(original_revnum == 2);
  SVN_TEST_ASSERT(!had_props);
  SVN_TEST_ASSERT(!props_mod);
  SVN_TEST_ASSERT(have_base);
  /* SVN_TEST_ASSERT(have_more_work...); */
  SVN_TEST_ASSERT(have_work);
  SVN_TEST_ASSERT(!conflicted);
  SVN_TEST_ASSERT(lock == NULL);
  /* SVN_TEST_ASSERT(last_mod_time...); */
  /* SVN_TEST_ASSERT(op_root...); */


  /* ### we need a hojillion more tests in here. I just want to get this
     ### round checked in, so I'm skipping more tests at this point.  */
  SVN_ERR(svn_wc__db_read_info(
            &status, &kind, &revision,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &changed_rev, &changed_date, &changed_author,
            &depth, &checksum, &target, &original_repos_relpath,
            &original_root_url, &original_uuid, &original_revnum,
            &lock, &recorded_size, &recorded_time, &changelist,
            &conflicted, &op_root, &had_props, &props_mod,
            &have_base, &have_more_work, &have_work,
            db, svn_dirent_join(local_abspath, "A", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_normal);
  SVN_TEST_ASSERT(kind == svn_node_file);
  SVN_TEST_STRING_ASSERT(changelist, "changelist");
  SVN_TEST_ASSERT(revision == 1);
  SVN_TEST_STRING_ASSERT(repos_relpath, "A");
  SVN_TEST_STRING_ASSERT(repos_root_url, "http://example.com/one");
  SVN_TEST_STRING_ASSERT(repos_uuid, "uuid1");
  SVN_TEST_ASSERT(changed_rev == 1);
  SVN_TEST_ASSERT(changed_date == TIME_1a);
  SVN_TEST_STRING_ASSERT(changed_author, AUTHOR_1);
  SVN_TEST_ASSERT(depth == svn_depth_unknown);
  SVN_TEST_ASSERT(checksum != NULL);
  SVN_TEST_ASSERT(recorded_size == 10);
  SVN_TEST_ASSERT(target == NULL);
  return SVN_NO_ERROR;
}


static svn_error_t *
test_pdh(apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc__db_t *db;

  SVN_ERR(create_open(&db, &local_abspath, "test_pdh", pool));

  /* NOTE: this test doesn't do anything apparent -- it simply exercises
     some internal functionality of wc_db.  This is a handy driver for
     debugging wc_db to ensure it manages per-directory handles properly.  */

  SVN_ERR(svn_wc__db_base_add_excluded_node(
            db, svn_dirent_join(local_abspath, "sub", pool),
            "sub", ROOT_ONE, UUID_ONE, 1,
            svn_node_file, svn_wc__db_status_server_excluded,
            NULL, NULL,
            pool));

  SVN_ERR(svn_wc__db_base_add_directory(
            db, svn_dirent_join(local_abspath, "sub2", pool),
            local_abspath, "sub2", ROOT_ONE, UUID_ONE, 1,
            apr_hash_make(pool), 1, 1, "me", NULL,
            svn_depth_infinity, NULL, FALSE, NULL, NULL,
            NULL, NULL, pool));

  SVN_ERR(svn_wc__db_base_add_excluded_node(
            db, svn_dirent_join(local_abspath, "sub2/A", pool),
            "sub2/A", ROOT_ONE, UUID_ONE, 1,
            svn_node_file, svn_wc__db_status_server_excluded,
            NULL, NULL,
            pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_scan_addition(apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc__db_t *db;
  svn_wc__db_status_t status;
  const char *op_root_abspath;
  const char *repos_relpath;
  const char *repos_root_url;
  const char *repos_uuid;
  const char *original_repos_relpath;
  const char *original_root_url;
  const char *original_uuid;
  svn_revnum_t original_revision;
  const char *moved_from_abspath;
  const char *move_op_root_abspath;
  const char *move_op_root_src;
  const char *delete_op_root_abspath;

  SVN_ERR(create_open(&db, &local_abspath, "test_scan_addition", pool));

  /* Simple addition of a directory. */
  SVN_ERR(svn_wc__db_scan_addition(
            &status, &op_root_abspath,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &original_repos_relpath, &original_root_url, &original_uuid,
            &original_revision,
            db, svn_dirent_join(local_abspath, "J", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_copied);
  SVN_ERR(validate_abspath(local_abspath, "J", op_root_abspath, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "J");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_STRING_ASSERT(original_repos_relpath, "q");
  SVN_TEST_STRING_ASSERT(original_root_url, ROOT_TWO);
  SVN_TEST_STRING_ASSERT(original_uuid, UUID_TWO);
  SVN_TEST_ASSERT(original_revision == 2);

  /* Simple copy (affects how scan-up is started). */
  SVN_ERR(svn_wc__db_scan_addition(
            &status, &op_root_abspath,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &original_repos_relpath, &original_root_url, &original_uuid,
            &original_revision,
            db, svn_dirent_join(local_abspath, "J/J-a", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_copied);
  SVN_ERR(validate_abspath(local_abspath, "J", op_root_abspath, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "J/J-a");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_STRING_ASSERT(original_repos_relpath, "q");
  SVN_TEST_STRING_ASSERT(original_root_url, ROOT_TWO);
  SVN_TEST_STRING_ASSERT(original_uuid, UUID_TWO);
  SVN_TEST_ASSERT(original_revision == 2);

  /* Node was moved here. */
  SVN_ERR(svn_wc__db_scan_addition(
            &status, &op_root_abspath,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &original_repos_relpath, &original_root_url, &original_uuid,
            &original_revision,
            db, svn_dirent_join(local_abspath, "J/J-d", pool),
            pool, pool));
  SVN_ERR(svn_wc__db_scan_moved(
            &moved_from_abspath,
            &move_op_root_abspath,
            &move_op_root_src,
            &delete_op_root_abspath,
            db, svn_dirent_join(local_abspath, "J/J-d", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_moved_here);
  SVN_ERR(validate_abspath(local_abspath, "J/J-d",
                                   op_root_abspath, pool));
  SVN_ERR(validate_abspath(local_abspath, "moved/file",
                                   moved_from_abspath, pool));
  SVN_ERR(validate_abspath(local_abspath, "J/J-d",
                                   move_op_root_abspath, pool));
  SVN_ERR(validate_abspath(local_abspath, "moved/file",
                                   move_op_root_src, pool));
  SVN_ERR(validate_abspath(local_abspath, "moved/file",
                                   delete_op_root_abspath, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "J/J-d");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_STRING_ASSERT(original_repos_relpath, "moved/file");
  SVN_TEST_STRING_ASSERT(original_root_url, ROOT_TWO);
  SVN_TEST_STRING_ASSERT(original_uuid, UUID_TWO);
  SVN_TEST_ASSERT(original_revision == 2);

  /* Check root of a copy. */
  SVN_ERR(svn_wc__db_scan_addition(
            &status, &op_root_abspath,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &original_repos_relpath, &original_root_url, &original_uuid,
            &original_revision,
            db, svn_dirent_join(local_abspath, "J/J-b", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_copied);
  SVN_ERR(validate_abspath(local_abspath, "J",
                                   op_root_abspath, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "J/J-b");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_STRING_ASSERT(original_repos_relpath, "q");
  SVN_TEST_STRING_ASSERT(original_root_url, ROOT_TWO);
  SVN_TEST_STRING_ASSERT(original_uuid, UUID_TWO);
  SVN_TEST_ASSERT(original_revision == 2);

  /* Ignore parent copy. Use copy closest to target.  */
  SVN_ERR(svn_wc__db_scan_addition(
            &status, &op_root_abspath,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &original_repos_relpath, &original_root_url, &original_uuid,
            &original_revision,
            db, svn_dirent_join(local_abspath, "J/J-b/J-b-a", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_copied);
  SVN_ERR(validate_abspath(local_abspath, "J/J-b/J-b-a",
                                   op_root_abspath, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "J/J-b/J-b-a");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_STRING_ASSERT(original_repos_relpath, "another/dir");
  SVN_TEST_STRING_ASSERT(original_root_url, ROOT_TWO);
  SVN_TEST_STRING_ASSERT(original_uuid, UUID_TWO);
  SVN_TEST_ASSERT(original_revision == 2);

  /* Inherit parent copy. */
  SVN_ERR(svn_wc__db_scan_addition(
            &status, &op_root_abspath,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &original_repos_relpath, &original_root_url, &original_uuid,
            &original_revision,
            db, svn_dirent_join(local_abspath, "J/J-b/J-b-b", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_copied);
  SVN_ERR(validate_abspath(local_abspath, "J",
                                   op_root_abspath, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "J/J-b/J-b-b");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_STRING_ASSERT(original_repos_relpath, "q");
  SVN_TEST_STRING_ASSERT(original_root_url, ROOT_TWO);
  SVN_TEST_STRING_ASSERT(original_uuid, UUID_TWO);
  SVN_TEST_ASSERT(original_revision == 2);

  return SVN_NO_ERROR;
}


static svn_error_t *
test_scan_deletion(apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc__db_t *db;
  const char *base_del_abspath;
  const char *work_del_abspath;
  const char *moved_to_abspath;
  const char *copy_op_root_abspath;

  SVN_ERR(create_open(&db, &local_abspath, "test_scan_deletion", pool));

  /* Node was moved elsewhere. */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &moved_to_abspath,
            &work_del_abspath,
            &copy_op_root_abspath,
            db, svn_dirent_join(local_abspath, "J/J-e", pool),
            pool, pool));
  SVN_ERR(validate_abspath(local_abspath, "J",
                                   base_del_abspath, pool));
  SVN_ERR(validate_abspath(local_abspath, "other/place",
                                   moved_to_abspath, pool));
  SVN_ERR(validate_abspath(local_abspath, "J/J-e",
                                   work_del_abspath, pool));
  SVN_ERR(validate_abspath(local_abspath, "other/place",
                                   copy_op_root_abspath, pool));

  /* Node was moved elsewhere (child of operation root). */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &moved_to_abspath,
            &work_del_abspath,
            &copy_op_root_abspath,
            db, svn_dirent_join(local_abspath, "J/J-e/J-e-a", pool),
            pool, pool));
  SVN_ERR(validate_abspath(local_abspath, "J",
                                   base_del_abspath, pool));
  SVN_ERR(validate_abspath(local_abspath, "other/place/J-e-a",
                                   moved_to_abspath, pool));
  SVN_ERR(validate_abspath(local_abspath, "J/J-e",
                                   work_del_abspath, pool));
  SVN_ERR(validate_abspath(local_abspath, "other/place",
                                   copy_op_root_abspath, pool));

  /* Root of delete. Parent is a WORKING node. */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &moved_to_abspath,
            &work_del_abspath,
            NULL,
            db, svn_dirent_join(local_abspath, "J/J-c", pool),
            pool, pool));
  /* Implicit delete of "J" (via replacement).  */
  SVN_ERR(validate_abspath(local_abspath, "J",
                                   base_del_abspath, pool));
  SVN_TEST_ASSERT(moved_to_abspath == NULL);
  SVN_ERR(validate_abspath(local_abspath, "J/J-c",
                                   work_del_abspath, pool));

  /* Child of a deleted root. */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &moved_to_abspath,
            &work_del_abspath,
            NULL,
            db, svn_dirent_join(local_abspath, "J/J-c/J-c-a", pool),
            pool, pool));
  /* Implicit delete of "J" (via replacement).  */
  SVN_ERR(validate_abspath(local_abspath, "J",
                                   base_del_abspath, pool));
  SVN_TEST_ASSERT(moved_to_abspath == NULL);
  SVN_ERR(validate_abspath(local_abspath, "J/J-c",
                                   work_del_abspath, pool));

  /* Base-deleted tree extending past deleted WORKING subtree.  */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &moved_to_abspath,
            &work_del_abspath,
            NULL,
            db, svn_dirent_join(local_abspath, "J/J-e/J-e-b/Jeba", pool),
            pool, pool));
  /* ### I don't understand this.  "J/J-e/J-e-b/Jeba" is a deleted
     base node that is not overlayed by the replacement rooted at "J".
     Why does base_del_abspath refer to "J-e"?  */
  SVN_ERR(validate_abspath(local_abspath, "J",
                                   base_del_abspath, pool));
  SVN_ERR(validate_abspath(local_abspath, "other/place/J-e-b/Jeba",
                                   moved_to_abspath, pool));
  SVN_TEST_STRING_ASSERT(work_del_abspath, NULL);

  /* Base-deleted tree extending past added WORKING tree.  */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &moved_to_abspath,
            &work_del_abspath,
            NULL,
            db, svn_dirent_join(local_abspath, "J/J-f/J-f-a", pool),
            pool, pool));
  /* Implicit delete of "J" (via replacement).  */
  SVN_ERR(validate_abspath(local_abspath, "J",
                                   base_del_abspath, pool));
  SVN_TEST_STRING_ASSERT(moved_to_abspath, NULL);
  SVN_TEST_STRING_ASSERT(work_del_abspath, NULL);

  /* Root of delete. Parent is a BASE node. */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &moved_to_abspath,
            &work_del_abspath,
            NULL,
            db, svn_dirent_join(local_abspath, "K", pool),
            pool, pool));
  SVN_ERR(validate_abspath(local_abspath, "K",
                                   base_del_abspath, pool));
  SVN_TEST_STRING_ASSERT(moved_to_abspath, NULL);
  SVN_TEST_STRING_ASSERT(work_del_abspath, NULL);

  /* Base-deleted tree. Start below root.  */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &moved_to_abspath,
            &work_del_abspath,
            NULL,
            db, svn_dirent_join(local_abspath, "K/K-a", pool),
            pool, pool));
  SVN_ERR(validate_abspath(local_abspath, "K",
                                   base_del_abspath, pool));
  SVN_TEST_STRING_ASSERT(moved_to_abspath, NULL);
  SVN_TEST_STRING_ASSERT(work_del_abspath, NULL);

  /* Base-deleted tree via move.  */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &moved_to_abspath,
            &work_del_abspath,
            &copy_op_root_abspath,
            db, svn_dirent_join(local_abspath, "K/K-b", pool),
            pool, pool));
  SVN_ERR(validate_abspath(local_abspath, "K",
                                   base_del_abspath, pool));
  SVN_ERR(validate_abspath(local_abspath, "moved/away",
                                   moved_to_abspath, pool));
  SVN_ERR(validate_abspath(local_abspath, "moved/away",
                                   copy_op_root_abspath, pool));
  SVN_TEST_STRING_ASSERT(work_del_abspath, NULL);

  /* Subtree deletion of added tree. Start at child.  */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &moved_to_abspath,
            &work_del_abspath,
            NULL,
            db, svn_dirent_join(local_abspath, "L/L-a/L-a-a", pool),
            pool, pool));
  SVN_TEST_STRING_ASSERT(base_del_abspath, NULL);
  SVN_TEST_STRING_ASSERT(moved_to_abspath, NULL);
  SVN_ERR(validate_abspath(local_abspath, "L/L-a",
                                   work_del_abspath, pool));

  /* Subtree deletion of added tree. Start at root.  */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &moved_to_abspath,
            &work_del_abspath,
            NULL,
            db, svn_dirent_join(local_abspath, "L/L-a", pool),
            pool, pool));
  SVN_TEST_STRING_ASSERT(base_del_abspath, NULL);
  SVN_TEST_STRING_ASSERT(moved_to_abspath, NULL);
  SVN_ERR(validate_abspath(local_abspath, "L/L-a",
                                   work_del_abspath, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_global_relocate(apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc__db_t *db;
  const char *repos_relpath;
  const char *repos_root_url;
  const char *repos_uuid;

  SVN_ERR(create_open(&db, &local_abspath, "test_global_relocate", pool));

  /* Initial sanity check. */
  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL,
                               &repos_relpath, &repos_root_url, &repos_uuid,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL,
                               db, local_abspath,
                               pool, pool));

  SVN_TEST_STRING_ASSERT(repos_relpath, "");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);

  /* Test relocating to a repos not existent in the db */
  SVN_ERR(svn_wc__db_global_relocate(db, local_abspath, ROOT_THREE, pool));
  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL,
                               &repos_relpath, &repos_root_url, &repos_uuid,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL,
                               db, local_abspath,
                               pool, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_THREE);
  /* The UUID should still be the same. */
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);

  /* While we're at it, let's see if the children have been relocated, too. */
  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL,
                               &repos_relpath, &repos_root_url, &repos_uuid,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL,
                               db, svn_dirent_join(local_abspath, "F",
                                                   pool),
                               pool, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "F");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_THREE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);

  /* Alternate repository is not relocated. */
  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL,
                               &repos_relpath, &repos_root_url, &repos_uuid,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL,
                               db, svn_dirent_join(local_abspath, "G",
                                                   pool),
                               pool, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "G-alt");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_TWO);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_TWO);

  return SVN_NO_ERROR;
}


static int
detect_work_item(const svn_skel_t *work_item)
{
  /* Test work items are a list with one integer atom as operation */
  if (!work_item->children)
    return -1;
  work_item = work_item->children;

  if (!work_item->is_atom || work_item->len != 1)
    return -1;
  return work_item->data[0] - '0';
}


static svn_error_t *
test_work_queue(apr_pool_t *pool)
{
  svn_wc__db_t *db;
  const char *local_abspath;
  svn_skel_t *work_item;
  int run_count[3] = { 4, 7, 2 };  /* run the work 13 times, total.  */
  int fetches = 0;
  apr_int64_t last_id = 0;

  SVN_ERR(create_open(&db, &local_abspath, "test_work_queue", pool));

  /* Create three work items.  */
  work_item = svn_skel__make_empty_list(pool);
  svn_skel__prepend_int(0, work_item, pool);
  SVN_ERR(svn_wc__db_wq_add(db, local_abspath, work_item, pool));

  work_item = svn_skel__make_empty_list(pool);
  svn_skel__prepend_int(1, work_item, pool);
  SVN_ERR(svn_wc__db_wq_add(db, local_abspath, work_item, pool));

  work_item = svn_skel__make_empty_list(pool);
  svn_skel__prepend_int(2, work_item, pool);
  SVN_ERR(svn_wc__db_wq_add(db, local_abspath, work_item, pool));

  while (TRUE)
    {
      apr_uint64_t id;
      int which;

      /* Fetch the next work item, or break when the work queue is empty.  */
      SVN_ERR(svn_wc__db_wq_fetch_next(&id, &work_item, db, local_abspath,
                                       last_id, pool, pool));
      if (work_item == NULL)
        break;

      /* Got one. We should never fetch work more than 13 times.  */
      ++fetches;
      SVN_TEST_ASSERT(fetches <= 13);

      /* Parse the work item to see which of the three we found.  */
      which = detect_work_item(work_item);
      SVN_TEST_ASSERT(which >= 0 && which <= 2);

      /* We should not see an item after we've run it enough times.

         Note: strictly speaking, "in the wild" a work item could remain
         after a call to wq_completed (ie. crash while that function was
         processing), but we don't really have a way to test that here.  */
      SVN_TEST_ASSERT(run_count[which] > 0);

      /* If we have run this particular item enough times, then go ahead
         and remove it from the work queue.  */
      if (--run_count[which] == 0)
        last_id = id;
      else
        last_id = 0;
    }

  /* Should have run precisely 13 work items.  */
  SVN_TEST_ASSERT(fetches == 13);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_externals_store(apr_pool_t *pool)
{
  svn_wc__db_t *db;
  const char *local_abspath;
  svn_checksum_t *orig_checksum;
  const char *file_external_path;
  const char *dir_external_path;
  const char *subdir;
  apr_hash_t *props = apr_hash_make(pool);
  svn_string_t *value = svn_string_create("value-data", pool);

  apr_hash_set(props, "key", APR_HASH_KEY_STRING, value);

  SVN_ERR(create_open(&db, &local_abspath, "test_externals_store", pool));

  /* Directory I exists in the standard test db */
  subdir = svn_dirent_join(local_abspath, "I", pool);

  SVN_ERR(svn_checksum_parse_hex(&orig_checksum, svn_checksum_sha1, SHA1_1,
                                 pool));

  file_external_path = svn_dirent_join(subdir, "file-external", pool);
  dir_external_path = svn_dirent_join(subdir, "dir-external", pool);

  SVN_ERR(svn_wc__db_external_add_file(db,
                                       file_external_path,
                                       local_abspath /* wri_abspath */,
                                       "some/location",
                                       "svn://some-repos/svn",
                                       "not-a-uuid",
                                       12,
                                       props,
                                       NULL,
                                       10,
                                       987654,
                                       "somebody",
                                       orig_checksum,
                                       NULL,
                                       subdir,
                                       "some/new-location",
                                       90,
                                       12,
                                       FALSE, NULL,
                                       FALSE,
                                       NULL,
                                       NULL,
                                       pool));

  SVN_ERR(svn_wc__db_external_add_dir(db,
                                      dir_external_path,
                                      local_abspath /* wri_abspath */,
                                      "svn://other-repos/nsv",
                                      "no-uuid-either",
                                      subdir,
                                      "some/other-location",
                                      70,
                                      32,
                                      NULL,
                                      pool));

  {
    svn_wc__db_status_t status;
    svn_node_kind_t kind;
    const char *repos_root_url;
    const char *repos_uuid;
    const char *defining_abspath;
    const char *recorded_repos_relpath;
    svn_revnum_t recorded_peg_revision;
    svn_revnum_t recorded_revision;

    SVN_ERR(svn_wc__db_external_read(&status, &kind, &defining_abspath,
                                     &repos_root_url, &repos_uuid,
                                     &recorded_repos_relpath,
                                     &recorded_peg_revision,
                                     &recorded_revision,
                                     db, file_external_path, local_abspath,
                                     pool, pool));

    SVN_TEST_ASSERT(status == svn_wc__db_status_normal);
    SVN_TEST_ASSERT(kind == svn_node_file);
    SVN_TEST_STRING_ASSERT(repos_root_url, "svn://some-repos/svn");
    SVN_TEST_STRING_ASSERT(repos_uuid, "not-a-uuid");
    SVN_TEST_STRING_ASSERT(defining_abspath, subdir);
    SVN_TEST_STRING_ASSERT(recorded_repos_relpath, "some/new-location");
    SVN_TEST_ASSERT(recorded_peg_revision == 90);
    SVN_TEST_ASSERT(recorded_revision == 12);

    {
      apr_hash_t *new_props;
      svn_string_t *v;

      SVN_ERR(svn_wc__db_base_get_props(&new_props, db,
                                        file_external_path,
                                        pool, pool));

      SVN_TEST_ASSERT(new_props != NULL);
      v = apr_hash_get(new_props, "key", APR_HASH_KEY_STRING);
      SVN_TEST_ASSERT(v != NULL);
      SVN_TEST_STRING_ASSERT(v->data, "value-data");
    }

    SVN_ERR(svn_wc__db_external_read(&status, &kind, &defining_abspath,
                                     &repos_root_url, &repos_uuid,
                                     &recorded_repos_relpath,
                                     &recorded_peg_revision,
                                     &recorded_revision,
                                     db, dir_external_path, local_abspath,
                                     pool, pool));

    SVN_TEST_ASSERT(status == svn_wc__db_status_normal);
    SVN_TEST_ASSERT(kind == svn_node_dir);
    SVN_TEST_STRING_ASSERT(repos_root_url, "svn://other-repos/nsv");
    SVN_TEST_STRING_ASSERT(repos_uuid, "no-uuid-either");
    SVN_TEST_STRING_ASSERT(defining_abspath, subdir);
    SVN_TEST_STRING_ASSERT(recorded_repos_relpath, "some/other-location");
    SVN_TEST_ASSERT(recorded_peg_revision == 70);
    SVN_TEST_ASSERT(recorded_revision == 32);
  }

  return SVN_NO_ERROR;
}

static int max_threads = 2;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_getting_info,
                   "get information from wc.db"),
    SVN_TEST_PASS2(test_inserting_nodes,
                   "insert different nodes into wc.db"),
    SVN_TEST_PASS2(test_children,
                   "getting the list of BASE or WORKING children"),
    SVN_TEST_PASS2(test_working_info,
                   "reading information about the WORKING tree"),
    SVN_TEST_PASS2(test_pdh,
                   "creation of per-directory handles"),
    SVN_TEST_PASS2(test_scan_addition,
                   "scanning added working nodes"),
    SVN_TEST_PASS2(test_scan_deletion,
                   "deletion introspection functions"),
    SVN_TEST_PASS2(test_global_relocate,
                   "relocating a node"),
    SVN_TEST_PASS2(test_work_queue,
                   "work queue processing"),
    SVN_TEST_PASS2(test_externals_store,
                   "externals store"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
