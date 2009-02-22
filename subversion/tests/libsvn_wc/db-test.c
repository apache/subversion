/*
 * db-test.c :  test the wc_db subsystem
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#include <apr_pools.h>
#include <apr_general.h>

#include "svn_types.h"
#include "svn_io.h"
#include "svn_dirent_uri.h"

#include "private/svn_sqlite.h"

#include "../../libsvn_wc/wc.h"
#include "../../libsvn_wc/wc_db.h"
#include "../../libsvn_wc/wc-metadata.h"

#include "../svn_test.h"


#define ROOT_ONE "http://example.com/one/"
#define ROOT_TWO "http://example.com/two/"

#define UUID_ONE "uuid1"
#define UUID_TWO "uuid2"

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

static const char * const data_loading_sql[] = {
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  (WC_METADATA_SQL
   " "
   "insert into repository values (1, '" ROOT_ONE "', '" UUID_ONE "'); "
   "insert into repository values (2, '" ROOT_TWO "', '" UUID_TWO "'); "
   "insert into wcroot values (1, null); "
   "insert into base_node values ("
   "  1, 1, '', 1, '', null, 'normal', 'dir', "
   "  1, null, null, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', 'infinity', null, '()', 0); "
   "insert into base_node values ("
   "  2, 1, 'A', null, null, 1, 'normal', 'file', "
   "  1, '$md5 $" MD5_1 "', 10, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, '()', null); "
   "insert into base_node values ("
   "  3, 1, 'B', null, null, 1, 'excluded', 'symlink', "
   "  null, null, null, "
   "  null, null, null, null, null, null, null); "
   "insert into base_node values ("
   "  4, 1, 'C', null, null, 1, 'absent', 'unknown', "
   "  null, null, null, "
   "  null, null, null, null, null, null, null); "
   "insert into base_node values ("
   "  5, 1, 'D', null, null, 1, 'not-present', 'unknown', "
   "  null, null, null, "
   "  null, null, null, null, null, null, null); "
   "insert into base_node values ("
   "  6, 1, 'E', null, null, 1, 'incomplete', 'unknown', "
   "  null, null, null, "
   "  null, null, null, null, null, null, null); "
   "insert into base_node values ("
   "  7, 1, 'F', null, null, 1, 'normal', 'file', "
   "  1, '$sha1$" SHA1_1 "', 15, "
   "  1, " TIME_1s ", '" AUTHOR_1 "', null, null, '()', null); "
   "insert into base_node values ("
   "  8, 1, 'G', 2, 'G-alt', 1, 'normal', 'file', "
   "  1, '$sha1$" SHA1_1 "', 15, "
   "  5, " TIME_2s ", '" AUTHOR_2 "', null, null, '()', null); "
   " "
   )
};


static svn_error_t *
create_fake_wc(apr_pool_t *scratch_pool)
{
  svn_sqlite__db_t *sdb;

  SVN_ERR(svn_io_make_dir_recursively("fake-wc/.svn", scratch_pool));
  svn_error_clear(svn_io_remove_file("fake-wc/.svn/wc.db", scratch_pool));
  SVN_ERR(svn_sqlite__open(&sdb, "fake-wc/.svn/wc.db",
                           svn_sqlite__mode_rwcreate, NULL,
                           SVN_WC__VERSION_EXPERIMENTAL, data_loading_sql,
                           scratch_pool, scratch_pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_getting_info(const char **msg,
                  svn_boolean_t msg_only,
                  svn_test_opts_t *opts,
                  apr_pool_t *pool)
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
  svn_depth_t depth;
  const svn_checksum_t *checksum;
  svn_filesize_t translated_size;
  svn_wc__db_t *db = NULL;  /* ### for now, it doesn't look at this param */
  svn_error_t *err;

  *msg = "get information from wc.db";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(create_fake_wc(pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, "fake-wc", pool));

  /* Test: basic fetching of data. */
  SVN_ERR(svn_wc__db_base_get_info(
            &kind, &status, &revision,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &changed_rev, &changed_date, &changed_author,
            &depth, &checksum, &translated_size,
            db, local_abspath,
            pool, pool));
  SVN_ERR_ASSERT(kind == svn_wc__db_kind_dir);
  SVN_ERR_ASSERT(status == svn_wc__db_status_normal);
  SVN_ERR_ASSERT(revision == 1);
  SVN_ERR_ASSERT(strcmp(repos_relpath, "") == 0);
  SVN_ERR_ASSERT(strcmp(repos_root_url, ROOT_ONE) == 0);
  SVN_ERR_ASSERT(strcmp(repos_uuid, UUID_ONE) == 0);
  SVN_ERR_ASSERT(changed_rev == 1);
  SVN_ERR_ASSERT(changed_date == TIME_1a);
  SVN_ERR_ASSERT(strcmp(changed_author, AUTHOR_1) == 0);
  SVN_ERR_ASSERT(depth == svn_depth_infinity);
  SVN_ERR_ASSERT(checksum == NULL);
  SVN_ERR_ASSERT(translated_size == SVN_INVALID_FILESIZE);

  /* Test: NULL params, file-specific values, inherit repos info. */
  SVN_ERR(svn_wc__db_base_get_info(
            &kind, NULL, NULL,
            &repos_relpath, &repos_root_url, &repos_uuid,
            NULL, NULL, NULL,
            NULL, &checksum, &translated_size,
            db, svn_dirent_join(local_abspath, "A", pool),
            pool, pool));
  SVN_ERR_ASSERT(kind == svn_wc__db_kind_file);
  SVN_ERR_ASSERT(strcmp(repos_relpath, "A") == 0);
  SVN_ERR_ASSERT(strcmp(repos_root_url, ROOT_ONE) == 0);
  SVN_ERR_ASSERT(strcmp(repos_uuid, UUID_ONE) == 0);
  SVN_ERR_ASSERT(strcmp(MD5_1, svn_checksum_to_cstring(checksum, pool)) == 0);
  SVN_ERR_ASSERT(translated_size == 10);

  /* Test: symlink kind, excluded presence, default values for columns. */
  SVN_ERR(svn_wc__db_base_get_info(
            &kind, &status, &revision,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &changed_rev, &changed_date, &changed_author,
            &depth, &checksum, &translated_size,
            db, svn_dirent_join(local_abspath, "B", pool),
            pool, pool));
  SVN_ERR_ASSERT(kind == svn_wc__db_kind_symlink);
  SVN_ERR_ASSERT(status == svn_wc__db_status_excluded);
  SVN_ERR_ASSERT(!SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(strcmp(repos_relpath, "B") == 0);
  SVN_ERR_ASSERT(strcmp(repos_root_url, ROOT_ONE) == 0);
  SVN_ERR_ASSERT(strcmp(repos_uuid, UUID_ONE) == 0);
  SVN_ERR_ASSERT(!SVN_IS_VALID_REVNUM(changed_rev));
  SVN_ERR_ASSERT(changed_date == 0);
  SVN_ERR_ASSERT(changed_author == NULL);
  SVN_ERR_ASSERT(depth == svn_depth_unknown);
  SVN_ERR_ASSERT(checksum == NULL);
  SVN_ERR_ASSERT(translated_size == SVN_INVALID_FILESIZE);

  /* Test: unknown kind, absent presence. */
  SVN_ERR(svn_wc__db_base_get_info(
            &kind, &status, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "C", pool),
            pool, pool));
  SVN_ERR_ASSERT(kind == svn_wc__db_kind_unknown);
  SVN_ERR_ASSERT(status == svn_wc__db_status_absent);

  /* Test: not-present presence. */
  SVN_ERR(svn_wc__db_base_get_info(
            NULL, &status, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "D", pool),
            pool, pool));
  SVN_ERR_ASSERT(status == svn_wc__db_status_not_present);

  /* Test: incomplete presence. */
  SVN_ERR(svn_wc__db_base_get_info(
            NULL, &status, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "E", pool),
            pool, pool));
  SVN_ERR_ASSERT(status == svn_wc__db_status_incomplete);

  /* Test: SHA1 checksum. */
  SVN_ERR(svn_wc__db_base_get_info(
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, &checksum, &translated_size,
            db, svn_dirent_join(local_abspath, "F", pool),
            pool, pool));
  SVN_ERR_ASSERT(strcmp(SHA1_1, svn_checksum_to_cstring(checksum, pool)) == 0);
  SVN_ERR_ASSERT(translated_size == 15);

  /* Test: alternate repository (switched file). */
  SVN_ERR(svn_wc__db_base_get_info(
            NULL, NULL, NULL,
            &repos_relpath, &repos_root_url, &repos_uuid,
            &changed_rev, &changed_date, &changed_author,
            NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "G", pool),
            pool, pool));
  SVN_ERR_ASSERT(strcmp(repos_relpath, "G-alt") == 0);
  SVN_ERR_ASSERT(strcmp(repos_root_url, ROOT_TWO) == 0);
  SVN_ERR_ASSERT(strcmp(repos_uuid, UUID_TWO) == 0);
  SVN_ERR_ASSERT(changed_rev == 5);
  SVN_ERR_ASSERT(changed_date == TIME_2a);
  SVN_ERR_ASSERT(strcmp(changed_author, AUTHOR_2) == 0);

  /* Test: missing node. */
  err = svn_wc__db_base_get_info(
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            NULL, NULL, NULL,
            db, svn_dirent_join(local_abspath, "missing-file", pool),
            pool, pool);
  SVN_ERR_ASSERT(err != NULL && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND);
  svn_error_clear(err);

  return SVN_NO_ERROR;
}


struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_getting_info),
    SVN_TEST_NULL
  };
