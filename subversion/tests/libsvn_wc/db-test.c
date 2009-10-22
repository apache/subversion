/*
 * db-test.c :  test the wc_db subsystem
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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

#include "../../libsvn_wc/wc.h"
#include "../../libsvn_wc/wc_db.h"
#include "../../libsvn_wc/wc-metadata.h"

#include "private/svn_wc_private.h"

#include "../svn_test.h"


#define ROOT_ONE "http://example.com/one/"
#define ROOT_TWO "http://example.com/two/"
#define ROOT_THREE "http://example.com/three/"

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

#define MD5_1 "2d18c5e57e84c5b8a5e9a6e13fa394dc"
#define MD5_2 "5d41402abc4b2a76b9719d911017c592"
#define SHA1_1 "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d"

#define I_TC_DATA "((conflict F file update edited deleted (version 23 " ROOT_ONE " 1 2 branch1/ft/F none) (version 23 " ROOT_ONE " 1 3 branch1/ft/F file)) (conflict G file update edited deleted (version 23 " ROOT_ONE " 1 2 branch1/ft/F none) (version 23 " ROOT_ONE " 1 3 branch1/ft/F file)) )"

static const char * const data_loading_sql[] = {
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  (
   /* Load the table and index definitions. */
   WC_METADATA_SQL_12
   " "

   /* Load our test data.

      Note: do not use named-column insertions. This allows us to test
      the column count in the schema matches our expectation here. */

   "insert into repository values (1, '" ROOT_ONE "', '" UUID_ONE "'); "
   "insert into repository values (2, '" ROOT_TWO "', '" UUID_TWO "'); "
   "insert into wcroot values (1, null); "

   /* ### The file_externals column in BASE_NODE is temporary, and will be
      ### removed.  However, to keep the tests passing, we need to add it
      ### to the following insert statements.  *Be sure to remove it*. */
   "insert into base_node values ("
   "  1, '', 1, '', null, 'normal', 'dir', "
   "  1, null, null, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', 'infinity', null, null, '()', null, 0, "
   "  null); "
   "insert into base_node values ("
   "  1, 'A', null, null, '', 'normal', 'file', "
   "  1, '$md5 $" MD5_1 "', 10, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'B', null, null, '', 'excluded', 'symlink', "
   "  null, null, null, "
   "  null, null, null, null, null, null, null, null, null, null); "
   "insert into base_node values ("
   "  1, 'C', null, null, '', 'absent', 'unknown', "
   "  null, null, null, "
   "  null, null, null, null, null, null, null, null, null, null); "
   "insert into base_node values ("
   "  1, 'D', null, null, '', 'not-present', 'unknown', "
   "  null, null, null, "
   "  null, null, null, null, null, null, null, null, null, null); "
   "insert into base_node values ("
   "  1, 'E', null, null, '', 'incomplete', 'unknown', "
   "  null, null, null, "
   "  null, null, null, null, null, null, null, null, null, null); "
   "insert into base_node values ("
   "  1, 'F', null, null, '', 'normal', 'file', "
   "  1, '$sha1$" SHA1_1 "', 15, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'G', 2, 'G-alt', '', 'normal', 'file', "
   "  1, '$sha1$" SHA1_1 "', 15, "
   "  2, " TIME_2s ", '" AUTHOR_2 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'H', null, null, '', 'normal', 'symlink', "
   "  1, null, null, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, 'H-target', null, '()', null, "
   "  null, null); "
   "insert into base_node values ("
   "  1, 'I', null, null, '', 'normal', 'dir', "
   "  1, null, null, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'J', null, null, '', 'normal', 'dir', "
   "  1, null, null, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'J/J-e', null, null, 'J', 'normal', 'dir', "
   "  1, null, null, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'J/J-e/J-e-a', null, null, 'J/J-e', 'normal', 'file', "
   "  1, '$sha1$" SHA1_1 "', 15, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'J/J-e/J-e-b', null, null, 'J/J-e', 'normal', 'dir', "
   "  1, null, null, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'J/J-e/J-e-b/Jeba', null, null, 'J/J-e/J-e-b', 'normal', 'file', "
   "  1, '$sha1$" SHA1_1 "', 15, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'J/J-f', null, null, 'J', 'normal', 'dir', "
   "  1, null, null, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'J/J-f/J-f-a', null, null, 'J/J-f', 'normal', 'dir', "
   "  1, null, null, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'K', null, null, '', 'normal', 'dir', "
   "  1, null, null, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'K/K-a', null, null, 'K', 'normal', 'file', "
   "  1, '$sha1$" SHA1_1 "', 15, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   "insert into base_node values ("
   "  1, 'K/K-b', null, null, 'K', 'normal', 'file', "
   "  1, '$sha1$" SHA1_1 "', 15, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, null, '()', null, null, "
   "  null); "
   " "
   "insert into working_node values ("
   "  1, 'I', '', 'normal', 'dir', "
   "  null, null, "
   "  2, " TIME_2s ", '" AUTHOR_2 "', 'immediates', null, "
   "  2, 'some/dir', 2, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J', '', 'normal', 'dir', "
   "  null, null, "
   "  null, null, null, 'immediates', null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-a', 'J', 'normal', 'file', "
   "  null, null, "
   "  null, null, null, null, null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-b', 'J', 'normal', 'dir', "
   "  null, null, "
   "  2, " TIME_2s ", '" AUTHOR_2 "', 'infinity', null, "
   "  2, 'some/dir', 2, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-b/J-b-a', 'J/J-b', 'normal', 'dir', "
   "  null, null, "
   "  2, " TIME_2s ", '" AUTHOR_2 "', 'infinity', null, "
   "  2, 'another/dir', 2, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-b/J-b-b', 'J/J-b', 'normal', 'file', "
   "  null, null, "
   "  null, null, null, null, null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-c', 'J', 'not-present', 'dir', "
   "  null, null, "
   "  null, null, null, null, null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-c/J-c-a', 'J/J-c', 'not-present', 'dir', "
   "  null, null, "
   "  null, null, null, null, null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-d', 'J', 'normal', 'file', "
   "  '$md5 $" MD5_1 "', 10, "
   "  2, " TIME_2s ", '" AUTHOR_2 "', null, null, "
   "  2, 'moved/file', 2, 1, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-e', 'J', 'not-present', 'dir', "
   "  null, null, "
   "  null, null, null, null, null, "
   "  null, null, null, 0, 'other/place', null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-e/J-e-a', 'J/J-e', 'not-present', 'file', "
   "  null, null, "
   "  null, null, null, null, null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-e/J-e-b', 'J/J-e', 'not-present', 'dir', "
   "  null, null, "
   "  null, null, null, null, null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-e/J-e-b/Jeba', 'J/J-e/J-e-b', 'base-deleted', 'file', "
   "  null, null, "
   "  null, null, null, null, null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-f', 'J', 'normal', 'dir', "
   "  null, null, "
   "  null, null, null, 'immediates', null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'J/J-f/J-f-a', 'J/J-f', 'base-deleted', 'dir', "
   "  null, null, "
   "  null, null, null, 'immediates', null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'K', '', 'base-deleted', 'dir', "
   "  null, null, "
   "  null, null, null, null, null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'K/K-a', 'K', 'base-deleted', 'file', "
   "  null, null, "
   "  null, null, null, null, null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'K/K-b', 'K', 'base-deleted', 'file', "
   "  null, null, "
   "  null, null, null, null, null, "
   "  null, null, null, 0, 'moved/away', null, '()', 0); "
   "insert into working_node values ("
   "  1, 'L', '', 'normal', 'dir', "
   "  null, null, "
   "  null, null, null, 'immediates', null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'L/L-a', 'L', 'not-present', 'dir', "
   "  null, null, "
   "  null, null, null, 'immediates', null, "
   "  null, null, null, 0, null, null, '()', 0); "
   "insert into working_node values ("
   "  1, 'L/L-a/L-a-a', 'L', 'not-present', 'dir', "
   "  null, null, "
   "  null, null, null, 'immediates', null, "
   "  null, null, null, 0, null, null, '()', 0); "
   " "
   "insert into actual_node values ("
   "  1, 'I', '', null, null, null, null, null, 'changelist', null, "
   "'" I_TC_DATA "');"
   ),
  WC_METADATA_SQL_13
};


static svn_error_t *
create_fake_wc(const char *subdir, int format, apr_pool_t *scratch_pool)
{
  const char *dirpath = svn_dirent_join_many(scratch_pool,
                                             "fake-wc", subdir, ".svn", NULL);
  const char *dbpath = svn_dirent_join(dirpath, "wc.db", scratch_pool);
  svn_sqlite__db_t *sdb;

  SVN_ERR(svn_io_make_dir_recursively(dirpath, scratch_pool));
  svn_error_clear(svn_io_remove_file(dbpath, scratch_pool));
  SVN_ERR(svn_sqlite__open(&sdb, dbpath, svn_sqlite__mode_rwcreate, NULL,
                           format, data_loading_sql,
                           svn_wc__db_upgrade_func, NULL,
                           scratch_pool, scratch_pool));

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


static svn_boolean_t
validate_abspath(const char *wcroot_abspath,
                 const char *expected_relpath,
                 const char *actual_abspath,
                 apr_pool_t *scratch_pool)
{
  if (actual_abspath == NULL)
    return FALSE;
  return strcmp(svn_dirent_join(wcroot_abspath,
                                expected_relpath,
                                scratch_pool),
                actual_abspath) == 0;
}


static svn_error_t *
test_getting_info(apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc__db_kind_t kind;
  svn_wc__db_status_t status;
  svn_revnum_t revision;
  const char *repos_relpath;
  const char *repos_root_url;
  const char *repos_uuid;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  apr_time_t last_mod_time;
  svn_depth_t depth;
  svn_checksum_t *checksum;
  svn_filesize_t translated_size;
  const char *target;
  svn_wc__db_lock_t *lock;
  svn_wc__db_t *db;
  svn_error_t *err;

  SVN_ERR(create_fake_wc("test_getting_info", SVN_WC__VERSION, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  "fake-wc/test_getting_info",
                                  pool));

  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readonly,
                          NULL, pool, pool));

  /* Test: basic fetching of data. */
  SVN_ERR(svn_wc__db_base_get_info(
            &status, &kind, &revision,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &changed_rev, &changed_date, &changed_author, &last_mod_time,
            &depth, &checksum, &translated_size, &target, &lock,
            db, local_abspath,
            pool, pool));
  SVN_TEST_ASSERT(kind == svn_wc__db_kind_dir);
  SVN_TEST_ASSERT(status == svn_wc__db_status_normal);
  SVN_TEST_ASSERT(revision == 1);
  SVN_TEST_STRING_ASSERT(repos_relpath, "");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_ASSERT(changed_rev == 1);
  SVN_TEST_ASSERT(changed_date == TIME_1a);
  SVN_TEST_STRING_ASSERT(changed_author, AUTHOR_1);
  SVN_TEST_ASSERT(last_mod_time == 0);
  SVN_TEST_ASSERT(depth == svn_depth_infinity);
  SVN_TEST_ASSERT(checksum == NULL);
  SVN_TEST_ASSERT(translated_size == SVN_INVALID_FILESIZE);
  SVN_TEST_ASSERT(target == NULL);
  SVN_TEST_ASSERT(lock == NULL);

  /* Test: NULL params, file-specific values, inherit repos info. */
  SVN_ERR(svn_wc__db_base_get_info(
            NULL, &kind, NULL,
            &repos_relpath, &repos_root_url, &repos_uuid,
            NULL, NULL, NULL, NULL,
            NULL, &checksum, &translated_size, NULL, NULL,
            db, svn_dirent_join(local_abspath, "A", pool),
            pool, pool));
  SVN_TEST_ASSERT(kind == svn_wc__db_kind_file);
  SVN_TEST_ASSERT(repos_relpath == NULL);
  SVN_TEST_ASSERT(repos_root_url == NULL);
  SVN_TEST_ASSERT(repos_uuid == NULL);
  SVN_TEST_STRING_ASSERT(MD5_1, svn_checksum_to_cstring(checksum, pool));
  SVN_TEST_ASSERT(translated_size == 10);

  /* Grab the inherited info. */
  SVN_ERR(svn_wc__db_scan_base_repos(
            &repos_relpath, &repos_root_url, &repos_uuid,
            db, svn_dirent_join(local_abspath, "A", pool),
            pool, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "A");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);

  /* Test: symlink kind, excluded presence, default values for columns. */
  SVN_ERR(svn_wc__db_base_get_info(
            &status, &kind, &revision,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &changed_rev, &changed_date, &changed_author, &last_mod_time,
            &depth, &checksum, &translated_size, &target, &lock,
            db, svn_dirent_join(local_abspath, "B", pool),
            pool, pool));
  SVN_TEST_ASSERT(kind == svn_wc__db_kind_symlink);
  SVN_TEST_ASSERT(status == svn_wc__db_status_excluded);
  SVN_TEST_ASSERT(!SVN_IS_VALID_REVNUM(revision));
  SVN_TEST_ASSERT(repos_relpath == NULL);
  SVN_TEST_ASSERT(repos_root_url == NULL);
  SVN_TEST_ASSERT(repos_uuid == NULL);
  SVN_TEST_ASSERT(!SVN_IS_VALID_REVNUM(changed_rev));
  SVN_TEST_ASSERT(changed_date == 0);
  SVN_TEST_ASSERT(changed_author == NULL);
  SVN_TEST_ASSERT(depth == svn_depth_unknown);
  SVN_TEST_ASSERT(checksum == NULL);
  SVN_TEST_ASSERT(translated_size == SVN_INVALID_FILESIZE);
  SVN_TEST_ASSERT(target == NULL);
  SVN_TEST_ASSERT(lock == NULL);

  /* Test: unknown kind, absent presence. */
  SVN_ERR(svn_wc__db_base_get_info(
            &status, &kind, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "C", pool),
            pool, pool));
  SVN_TEST_ASSERT(kind == svn_wc__db_kind_unknown);
  SVN_TEST_ASSERT(status == svn_wc__db_status_absent);

  /* Test: not-present presence. */
  SVN_ERR(svn_wc__db_base_get_info(
            &status, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "D", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_not_present);

  /* Test: incomplete presence. */
  SVN_ERR(svn_wc__db_base_get_info(
            &status, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "E", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_incomplete);

  /* Test: SHA1 checksum. */
  SVN_ERR(svn_wc__db_base_get_info(
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, &checksum, &translated_size, NULL, NULL,
            db, svn_dirent_join(local_abspath, "F", pool),
            pool, pool));
  SVN_TEST_STRING_ASSERT(SHA1_1,
                         svn_checksum_to_cstring(checksum, pool));
  SVN_TEST_ASSERT(translated_size == 15);

  /* Test: alternate repository (switched file). */
  SVN_ERR(svn_wc__db_base_get_info(
            NULL, NULL, NULL,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &changed_rev, &changed_date, &changed_author,
            NULL, NULL, NULL, NULL, NULL, NULL,
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
            NULL, NULL, &checksum, &translated_size, &target, NULL,
            db, svn_dirent_join(local_abspath, "H", pool),
            pool, pool));
  SVN_TEST_ASSERT(checksum == NULL);
  SVN_TEST_ASSERT(translated_size == SVN_INVALID_FILESIZE);
  SVN_TEST_STRING_ASSERT(target, "H-target");

  /* Test: missing node. */
  err = svn_wc__db_base_get_info(
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "missing-file", pool),
            pool, pool);
  SVN_TEST_ASSERT(err != NULL && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND);
  svn_error_clear(err);

  return SVN_NO_ERROR;
}


static svn_error_t *
validate_node(svn_wc__db_t *db,
              const char *local_abspath,
              const char *relpath,
              svn_wc__db_kind_t expected_kind,
              svn_wc__db_status_t expected_status,
              apr_pool_t *scratch_pool)
{
  const char *path = svn_dirent_join(local_abspath, relpath, scratch_pool);
  svn_wc__db_kind_t kind;
  svn_wc__db_status_t status;
  apr_hash_t *props;
  const svn_string_t *value;

  SVN_ERR(svn_wc__db_base_get_info(
            &status, &kind, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            db, path,
            scratch_pool, scratch_pool));
  SVN_TEST_ASSERT(kind == expected_kind);
  SVN_TEST_ASSERT(status == expected_status);

  SVN_ERR(svn_wc__db_base_get_props(&props, db, path,
                                    scratch_pool, scratch_pool));
  switch (status)
    {
    case svn_wc__db_status_absent:
    case svn_wc__db_status_excluded:
    case svn_wc__db_status_incomplete:
    case svn_wc__db_status_not_present:
      /* Our tests aren't setting properties on these node types, so
         short-circuit examination of name/value pairs. */
      return SVN_NO_ERROR;

    default:
      SVN_TEST_ASSERT(props != NULL);
    }

  value = apr_hash_get(props, "p1", APR_HASH_KEY_STRING);
  SVN_TEST_ASSERT(value != NULL && strcmp(value->data, "v1") == 0);
  SVN_ERR(svn_wc__db_base_get_prop(&value, db, path, "p1",
                                   scratch_pool, scratch_pool));
  SVN_TEST_ASSERT(value != NULL && strcmp(value->data, "v1") == 0);

  value = apr_hash_get(props, "for-file", APR_HASH_KEY_STRING);
  SVN_TEST_ASSERT(value != NULL && strcmp(value->data, relpath) == 0);
  SVN_ERR(svn_wc__db_base_get_prop(&value, db, path, "for-file",
                                   scratch_pool, scratch_pool));
  SVN_TEST_ASSERT(value != NULL && strcmp(value->data, relpath) == 0);

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

  SVN_ERR(create_fake_wc("test_inserting_nodes", SVN_WC__VERSION, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  "fake-wc/test_inserting_nodes",
                                  pool));

  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readwrite,
                          NULL, pool, pool));

  props = apr_hash_make(pool);
  set_prop(props, "p1", "v1", pool);

  children = svn_cstring_split("N-a N-b N-c", " ", FALSE, pool);

  SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_md5, MD5_1, pool));

  /* Create a new directory and several child nodes. */
  set_prop(props, "for-file", "N", pool);
  SVN_ERR(svn_wc__db_base_add_directory(
            db, svn_dirent_join(local_abspath, "N", pool),
            "N", ROOT_ONE, UUID_ONE, 3,
            props,
            1, TIME_1a, AUTHOR_1,
            children, svn_depth_infinity,
            pool));

  /* Replace an incomplete node with a file node. */
  set_prop(props, "for-file", "N/N-a", pool);
  SVN_ERR(svn_wc__db_base_add_file(
            db, svn_dirent_join(local_abspath, "N/N-a", pool),
            "N/N-a", ROOT_ONE, UUID_ONE, 3,
            props,
            1, TIME_1a, AUTHOR_1,
            checksum, 10,
            pool));

  /* Create a new symlink node. */
  set_prop(props, "for-file", "O", pool);
  SVN_ERR(svn_wc__db_base_add_symlink(
            db, svn_dirent_join(local_abspath, "O", pool),
            "O", ROOT_ONE, UUID_ONE, 3,
            props,
            1, TIME_1a, AUTHOR_1,
            "O-target",
            pool));

  /* Replace an incomplete node with an absent file node. */
  SVN_ERR(svn_wc__db_base_add_absent_node(
            db, svn_dirent_join(local_abspath, "N/N-b", pool),
            "N/N-b", ROOT_ONE, UUID_ONE, 3,
            svn_wc__db_kind_file, svn_wc__db_status_absent,
            pool));

  /* Create a new excluded directory node. */
  SVN_ERR(svn_wc__db_base_add_absent_node(
            db, svn_dirent_join(local_abspath, "P", pool),
            "P", ROOT_ONE, UUID_ONE, 3,
            svn_wc__db_kind_dir, svn_wc__db_status_excluded,
            pool));

  /* Create a new not-present symlink node. */
  SVN_ERR(svn_wc__db_base_add_absent_node(
            db, svn_dirent_join(local_abspath, "Q", pool),
            "Q", ROOT_ONE, UUID_ONE, 3,
            svn_wc__db_kind_symlink, svn_wc__db_status_not_present,
            pool));

  /* Create a new absent unknown-kind node. */
  SVN_ERR(svn_wc__db_base_add_absent_node(
            db, svn_dirent_join(local_abspath, "R", pool),
            "R", ROOT_ONE, UUID_ONE, 3,
            svn_wc__db_kind_unknown, svn_wc__db_status_absent,
            pool));


  /* Are all the nodes where we expect them to be? */
  SVN_ERR(validate_node(db, local_abspath, "N",
                        svn_wc__db_kind_dir, svn_wc__db_status_normal,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "N/N-a",
                        svn_wc__db_kind_file, svn_wc__db_status_normal,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "N/N-b",
                        svn_wc__db_kind_file, svn_wc__db_status_absent,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "N/N-c",
                        svn_wc__db_kind_unknown, svn_wc__db_status_incomplete,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "O",
                        svn_wc__db_kind_symlink, svn_wc__db_status_normal,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "P",
                        svn_wc__db_kind_dir, svn_wc__db_status_excluded,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "Q",
                        svn_wc__db_kind_symlink, svn_wc__db_status_not_present,
                        pool));
  SVN_ERR(validate_node(db, local_abspath, "R",
                        svn_wc__db_kind_unknown, svn_wc__db_status_absent,
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

  SVN_ERR(create_fake_wc("test_children", SVN_WC__VERSION, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  "fake-wc/test_children",
                                  pool));

  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readonly,
                          NULL, pool, pool));

  SVN_ERR(svn_wc__db_base_get_children(&children,
                                       db, local_abspath,
                                       pool, pool));
  SVN_TEST_ASSERT(children->nelts == 11);
  for (i = children->nelts; i--; )
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);

      SVN_TEST_ASSERT(strlen(name) == 1);
      /* ### check the actual values */
    }

  SVN_ERR(svn_wc__db_read_children(&children,
                                   db, local_abspath,
                                   pool, pool));
  SVN_TEST_ASSERT(children->nelts == 12);
  for (i = children->nelts; i--; )
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);

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
  svn_wc__db_kind_t kind;
  svn_wc__db_status_t status;
  svn_revnum_t revision;
  const char *repos_relpath;
  const char *repos_root_url;
  const char *repos_uuid;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  apr_time_t last_mod_time;
  svn_depth_t depth;
  svn_checksum_t *checksum;
  svn_filesize_t translated_size;
  const char *target;
  const char *changelist;
  const char *original_repos_relpath;
  const char *original_root_url;
  const char *original_uuid;
  svn_revnum_t original_revnum;
  svn_boolean_t text_mod;
  svn_boolean_t props_mod;
  svn_boolean_t base_shadowed;
  const char *conflict_old;
  const char *conflict_new;
  const char *conflict_working;
  const char *prop_reject_file;
  svn_wc__db_lock_t *lock;
  const char *tree_conflict_data;
  svn_wc__db_t *db;

  SVN_ERR(create_fake_wc("test_working_info", SVN_WC__VERSION, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  "fake-wc/test_working_info",
                                  pool));

  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readonly,
                          NULL, pool, pool));

  /* Test: basic fetching of data. */
  SVN_ERR(svn_wc__db_read_info(
            &status, &kind, &revision,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &changed_rev, &changed_date, &changed_author, &last_mod_time,
            &depth, &checksum, &translated_size, &target,
            &changelist, &original_repos_relpath, &original_root_url,
            &original_uuid, &original_revnum,
            &text_mod, &props_mod, &base_shadowed,
            &conflict_old, &conflict_new, &conflict_working,
            &prop_reject_file, &lock, &tree_conflict_data,
            db, svn_dirent_join(local_abspath, "I", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_added);
  SVN_TEST_ASSERT(kind == svn_wc__db_kind_dir);
  SVN_TEST_ASSERT(revision == SVN_INVALID_REVNUM);
  SVN_TEST_ASSERT(repos_relpath == NULL);
  SVN_TEST_ASSERT(repos_root_url == NULL);
  SVN_TEST_ASSERT(repos_uuid == NULL);
  SVN_TEST_ASSERT(changed_rev == 2);
  SVN_TEST_ASSERT(changed_date == TIME_2a);
  SVN_TEST_STRING_ASSERT(changed_author, AUTHOR_2);
  SVN_TEST_ASSERT(depth == svn_depth_immediates);
  SVN_TEST_ASSERT(checksum == NULL);
  SVN_TEST_ASSERT(translated_size == SVN_INVALID_FILESIZE);
  SVN_TEST_ASSERT(target == NULL);
  SVN_TEST_STRING_ASSERT(changelist, "changelist");
  SVN_TEST_STRING_ASSERT(original_repos_relpath, "some/dir");
  SVN_TEST_STRING_ASSERT(original_root_url, ROOT_TWO);
  SVN_TEST_STRING_ASSERT(original_uuid, UUID_TWO);
  SVN_TEST_ASSERT(original_revnum == 2);
  SVN_TEST_ASSERT(text_mod == FALSE);
  SVN_TEST_ASSERT(props_mod == FALSE);
  SVN_TEST_ASSERT(base_shadowed == TRUE);
  SVN_TEST_ASSERT(conflict_old == NULL);
  SVN_TEST_ASSERT(conflict_new == NULL);
  SVN_TEST_ASSERT(conflict_working == NULL);
  SVN_TEST_ASSERT(prop_reject_file == NULL);
  SVN_TEST_ASSERT(lock == NULL);
  SVN_TEST_STRING_ASSERT(tree_conflict_data, I_TC_DATA);


  /* ### we need a hojillion more tests in here. I just want to get this
     ### round checked in, so I'm skipping more tests at this point.  */


  return SVN_NO_ERROR;
}


static svn_error_t *
test_pdh(apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc__db_t *db;

  SVN_ERR(create_fake_wc("test_pdh", SVN_WC__VERSION, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  "fake-wc/test_pdh",
                                  pool));

  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readwrite,
                          NULL, pool, pool));

  /* NOTE: this test doesn't do anything apparent -- it simply exercises
     some internal functionality of wc_db.  This is a handy driver for
     debugging wc_db to ensure it manages per-directory handles properly.  */

  SVN_ERR(svn_wc__db_base_add_absent_node(
            db, svn_dirent_join(local_abspath, "sub/A/B", pool),
            "sub/A/B", ROOT_ONE, UUID_ONE, 1,
            svn_wc__db_kind_file, svn_wc__db_status_absent,
            pool));

  SVN_ERR(svn_wc__db_base_add_absent_node(
            db, svn_dirent_join(local_abspath, "sub/A/B/C/D", pool),
            "sub/A/B/C/D", ROOT_ONE, UUID_ONE, 1,
            svn_wc__db_kind_file, svn_wc__db_status_absent,
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

  SVN_ERR(create_fake_wc("test_scan_addition", SVN_WC__VERSION, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  "fake-wc/test_scan_addition",
                                  pool));
  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readonly,
                          NULL, pool, pool));

  /* Simple addition of a directory. */
  SVN_ERR(svn_wc__db_scan_addition(
            &status, &op_root_abspath,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &original_repos_relpath, &original_root_url, &original_uuid,
            &original_revision,
            db, svn_dirent_join(local_abspath, "J", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_added);
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J", op_root_abspath, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "J");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_ASSERT(original_repos_relpath == NULL);
  SVN_TEST_ASSERT(original_root_url == NULL);
  SVN_TEST_ASSERT(original_uuid == NULL);
  SVN_TEST_ASSERT(original_revision == SVN_INVALID_REVNUM);

  /* Simple addition of a file (affects how scan-up is started). */
  SVN_ERR(svn_wc__db_scan_addition(
            &status, &op_root_abspath,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &original_repos_relpath, &original_root_url, &original_uuid,
            &original_revision,
            db, svn_dirent_join(local_abspath, "J/J-a", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_added);
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J", op_root_abspath, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "J/J-a");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_ASSERT(original_repos_relpath == NULL);
  SVN_TEST_ASSERT(original_root_url == NULL);
  SVN_TEST_ASSERT(original_uuid == NULL);
  SVN_TEST_ASSERT(original_revision == SVN_INVALID_REVNUM);

  /* Node was moved here. */
  SVN_ERR(svn_wc__db_scan_addition(
            &status, &op_root_abspath,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &original_repos_relpath, &original_root_url, &original_uuid,
            &original_revision,
            db, svn_dirent_join(local_abspath, "J/J-d", pool),
            pool, pool));
  SVN_TEST_ASSERT(status == svn_wc__db_status_moved_here);
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J/J-d",
                                   op_root_abspath, pool));
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
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J/J-b",
                                   op_root_abspath, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "J/J-b");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_STRING_ASSERT(original_repos_relpath, "some/dir");
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
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J/J-b/J-b-a",
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
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J/J-b",
                                   op_root_abspath, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "J/J-b/J-b-b");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);
  SVN_TEST_STRING_ASSERT(original_repos_relpath, "some/dir");
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
  svn_boolean_t base_replaced;
  const char *work_del_abspath;
  const char *moved_to_abspath;

  SVN_ERR(create_fake_wc("test_scan_deletion", SVN_WC__VERSION, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  "fake-wc/test_scan_deletion",
                                  pool));
  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readonly,
                          NULL, pool, pool));

  /* Node was moved elsewhere. */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &base_replaced,
            &moved_to_abspath,
            &work_del_abspath,
            db, svn_dirent_join(local_abspath, "J/J-e", pool),
            pool, pool));
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J/J-e",
                                   base_del_abspath, pool));
  SVN_TEST_ASSERT(base_replaced);
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "other/place",
                                   moved_to_abspath, pool));
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J/J-e",
                                   work_del_abspath, pool));

  /* Node was moved elsewhere (child of operation root). */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &base_replaced,
            &moved_to_abspath,
            &work_del_abspath,
            db, svn_dirent_join(local_abspath, "J/J-e/J-e-a", pool),
            pool, pool));
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J/J-e",
                                   base_del_abspath, pool));
  SVN_TEST_ASSERT(base_replaced);
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "other/place",
                                   moved_to_abspath, pool));
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J/J-e",
                                   work_del_abspath, pool));

  /* Root of delete. Parent is a WORKING node. */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &base_replaced,
            &moved_to_abspath,
            &work_del_abspath,
            db, svn_dirent_join(local_abspath, "J/J-c", pool),
            pool, pool));
  /* Implicit delete of "J" (via replacement).  */
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J",
                                   base_del_abspath, pool));
  SVN_TEST_ASSERT(base_replaced);
  SVN_TEST_ASSERT(moved_to_abspath == NULL);
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J/J-c",
                                   work_del_abspath, pool));

  /* Child of a deleted root. */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &base_replaced,
            &moved_to_abspath,
            &work_del_abspath,
            db, svn_dirent_join(local_abspath, "J/J-c/J-c-a", pool),
            pool, pool));
  /* Implicit delete of "J" (via replacement).  */
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J",
                                   base_del_abspath, pool));
  SVN_TEST_ASSERT(base_replaced);
  SVN_TEST_ASSERT(moved_to_abspath == NULL);
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J/J-c",
                                   work_del_abspath, pool));

  /* Base-deleted tree extending past deleted WORKING subtree.  */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &base_replaced,
            &moved_to_abspath,
            &work_del_abspath,
            db, svn_dirent_join(local_abspath, "J/J-e/J-e-b/Jeba", pool),
            pool, pool));
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J/J-e",
                                   base_del_abspath, pool));
  SVN_TEST_ASSERT(base_replaced);
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "other/place",
                                   moved_to_abspath, pool));
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J/J-e",
                                   work_del_abspath, pool));

  /* Base-deleted tree extending past added WORKING tree.  */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &base_replaced,
            &moved_to_abspath,
            &work_del_abspath,
            db, svn_dirent_join(local_abspath, "J/J-f/J-f-a", pool),
            pool, pool));
  /* Implicit delete of "J" (via replacement).  */
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "J",
                                   base_del_abspath, pool));
  SVN_TEST_ASSERT(base_replaced);
  SVN_TEST_ASSERT(moved_to_abspath == NULL);
  SVN_TEST_ASSERT(work_del_abspath == NULL);

  /* Root of delete. Parent is a BASE node. */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &base_replaced,
            &moved_to_abspath,
            &work_del_abspath,
            db, svn_dirent_join(local_abspath, "K", pool),
            pool, pool));
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "K",
                                   base_del_abspath, pool));
  SVN_TEST_ASSERT(!base_replaced);
  SVN_TEST_ASSERT(moved_to_abspath == NULL);
  SVN_TEST_ASSERT(work_del_abspath == NULL);

  /* Base-deleted tree. Start below root.  */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &base_replaced,
            &moved_to_abspath,
            &work_del_abspath,
            db, svn_dirent_join(local_abspath, "K/K-a", pool),
            pool, pool));
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "K",
                                   base_del_abspath, pool));
  SVN_TEST_ASSERT(!base_replaced);
  SVN_TEST_ASSERT(moved_to_abspath == NULL);
  SVN_TEST_ASSERT(work_del_abspath == NULL);

  /* Base-deleted tree via move.  */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &base_replaced,
            &moved_to_abspath,
            &work_del_abspath,
            db, svn_dirent_join(local_abspath, "K/K-b", pool),
            pool, pool));
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "K/K-b",
                                   base_del_abspath, pool));
  SVN_TEST_ASSERT(!base_replaced);
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "moved/away",
                                   moved_to_abspath, pool));
  SVN_TEST_ASSERT(work_del_abspath == NULL);

  /* Subtree deletion of added tree. Start at child.  */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &base_replaced,
            &moved_to_abspath,
            &work_del_abspath,
            db, svn_dirent_join(local_abspath, "L/L-a/L-a-a", pool),
            pool, pool));
  SVN_TEST_ASSERT(base_del_abspath == NULL);
  SVN_TEST_ASSERT(!base_replaced);
  SVN_TEST_ASSERT(moved_to_abspath == NULL);
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "L/L-a",
                                   work_del_abspath, pool));

  /* Subtree deletion of added tree. Start at root.  */
  SVN_ERR(svn_wc__db_scan_deletion(
            &base_del_abspath,
            &base_replaced,
            &moved_to_abspath,
            &work_del_abspath,
            db, svn_dirent_join(local_abspath, "L/L-a", pool),
            pool, pool));
  SVN_TEST_ASSERT(base_del_abspath == NULL);
  SVN_TEST_ASSERT(!base_replaced);
  SVN_TEST_ASSERT(moved_to_abspath == NULL);
  SVN_TEST_ASSERT(validate_abspath(local_abspath, "L/L-a",
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
  
  SVN_ERR(create_fake_wc("test_global_relocate", SVN_WC__VERSION, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  "fake-wc/test_global_relocate",
                                  pool));
  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readonly,
                          NULL, pool, pool));

  /* Initial sanity check. */
  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL,
                               &repos_relpath, &repos_root_url, &repos_uuid,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               db, local_abspath,
                               pool, pool));

  SVN_TEST_STRING_ASSERT(repos_relpath, "");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_ONE);
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);

  /* Test relocating to a repos not existant in the db */
  SVN_ERR(svn_wc__db_global_relocate(db, local_abspath, ROOT_THREE, TRUE,
                                     pool));
  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL,
                               &repos_relpath, &repos_root_url, &repos_uuid,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
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
                               NULL, NULL, NULL, NULL,
                               db, svn_dirent_join(local_abspath, "G",
                                                   pool),
                               pool, pool));
  SVN_TEST_STRING_ASSERT(repos_relpath, "G-alt");
  SVN_TEST_STRING_ASSERT(repos_root_url, ROOT_THREE);
  /* The UUID should still be the same. */
  SVN_TEST_STRING_ASSERT(repos_uuid, UUID_ONE);

  return SVN_NO_ERROR;
}


static svn_error_t *
test_upgrading_to_f13(apr_pool_t *pool)
{
  SVN_ERR(create_fake_wc("test_f13_upgrade", 13, pool));

  return SVN_NO_ERROR;
}


struct svn_test_descriptor_t test_funcs[] =
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
    SVN_TEST_PASS2(test_upgrading_to_f13,
                   "upgrading to fromat 13"),
    SVN_TEST_NULL
  };
