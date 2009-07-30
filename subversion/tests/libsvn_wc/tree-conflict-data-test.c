/*
 * tree-conflict-data-test.c -- test the storage of tree conflict data
 */

#include <stdio.h>
#include <string.h>
#include <apr_hash.h>
#include <apr_tables.h>

#include "svn_pools.h"
#include "svn_hash.h"
#include "svn_types.h"
#include "svn_wc.h"
#include "private/svn_wc_private.h"
#include "../svn_test.h"
#include "../../libsvn_wc/tree_conflicts.h"

/* A quick way to create error messages.  */
static svn_error_t *
fail(apr_pool_t *pool, const char *fmt, ...)
{
  va_list ap;
  char *msg;

  va_start(ap, fmt);
  msg = apr_pvsprintf(pool, fmt, ap);
  va_end(ap);

  return svn_error_create(SVN_ERR_TEST_FAILED, 0, msg);
}

static svn_error_t *
test_read_tree_conflict(apr_pool_t *pool)
{
  svn_wc_conflict_description_t *conflict;
  apr_hash_t *conflicts;
  svn_wc_conflict_description_t *exp_conflict;
  const char *tree_conflict_data;
  apr_hash_index_t *hi;

  tree_conflict_data = "((conflict Foo.c file update deleted edited "
                         "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )))";

  exp_conflict = svn_wc_conflict_description_create_tree("Foo.c", NULL,
                                                         svn_node_file,
                                                         svn_wc_operation_update,
                                                         NULL, NULL, pool);
  exp_conflict->action = svn_wc_conflict_action_delete;
  exp_conflict->reason = svn_wc_conflict_reason_edited;

  SVN_ERR(svn_wc__read_tree_conflicts(&conflicts, tree_conflict_data, "",
                                      pool));

  hi = apr_hash_first(pool, conflicts);
  conflict = svn_apr_hash_index_val(hi);

  if ((conflict->node_kind != exp_conflict->node_kind) ||
      (conflict->action    != exp_conflict->action) ||
      (conflict->reason    != exp_conflict->reason) ||
      (conflict->operation != exp_conflict->operation) ||
      (strcmp(conflict->path, exp_conflict->path) != 0))
    return fail(pool, "Unexpected tree conflict");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_read_2_tree_conflicts(apr_pool_t *pool)
{
  const char *tree_conflict_data;
  svn_wc_conflict_description_t *conflict1, *conflict2;
  apr_hash_t *conflicts;
  svn_wc_conflict_description_t *exp_conflict1, *exp_conflict2;
  apr_hash_index_t *hi;

  tree_conflict_data =
    "((conflict Foo.c file update deleted edited "
      "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )) "
     "(conflict Bar.h file update edited deleted "
      "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )))";

  exp_conflict1 = svn_wc_conflict_description_create_tree("Foo.c", NULL,
                                                          svn_node_file,
                                                          svn_wc_operation_update,
                                                          NULL, NULL, pool);
  exp_conflict1->action = svn_wc_conflict_action_delete;
  exp_conflict1->reason = svn_wc_conflict_reason_edited;

  exp_conflict2 = svn_wc_conflict_description_create_tree("Bar.h", NULL,
                                                          svn_node_file,
                                                          svn_wc_operation_update,
                                                          NULL, NULL, pool);
  exp_conflict2->action = svn_wc_conflict_action_edit;
  exp_conflict2->reason = svn_wc_conflict_reason_deleted;

  SVN_ERR(svn_wc__read_tree_conflicts(&conflicts, tree_conflict_data, "",
                                      pool));

  hi = apr_hash_first(pool, conflicts);
  conflict1 = svn_apr_hash_index_val(hi);
  if ((conflict1->node_kind != exp_conflict1->node_kind) ||
      (conflict1->action    != exp_conflict1->action) ||
      (conflict1->reason    != exp_conflict1->reason) ||
      (conflict1->operation != exp_conflict1->operation) ||
      (strcmp(conflict1->path, exp_conflict1->path) != 0))
    return fail(pool, "Tree conflict struct #1 has bad data");

  hi = apr_hash_next(hi);
  conflict2 = svn_apr_hash_index_val(hi);
  if ((conflict2->node_kind != exp_conflict2->node_kind) ||
      (conflict2->action    != exp_conflict2->action) ||
      (conflict2->reason    != exp_conflict2->reason) ||
      (conflict2->operation != exp_conflict2->operation) ||
      (strcmp(conflict2->path, exp_conflict2->path) != 0))
    return fail(pool, "Tree conflict struct #2 has bad data");

  return SVN_NO_ERROR;
}

/* This needs to be adjusted in case the constants for the
 * delimiters change... */
static const char* broken_tree_conflict_test_data[] = {
  /* Missing descriptions */
  "|Bar.h:file:update:edited:deleted::::::::",
  "Foo.c:file:update:deleted:edited::::::::|",
  "|||||||",
  "",
  /* Missing fields */
  "Foo.c:fileupdate:deleted:edited::::::::",
  "Foo.c",
  "::::",
  ":::",
  "Foo.c:::::::::::::;",
  /* Bad separators */
  "Foo.c:file:update:deleted:edited::::::::$Bar.h:file:update:edited:deleted::::::::",
  "Foo.c|file|update|deleted|edited:::::::::Bar.h|file|update|edited|deleted::::::::",
  /* Missing separators */
  "Foo.c:file:update:deleted:edited::::::::Bar.h:file:update:edited:deleted::::::::",
  "Foo.c:fileupdate:deleted:edited::::::::",
  /* Unescaped separators */
  "F|oo.c:file:update:deleted:edited::::::::",
  "F:oo.c:file:update:deleted:edited::::::::",
  /* Unescaped escape */
  "Foo.c\\:file:update:deleted:edited::::::::",
  "Foo.c\\",
  /* Illegally escaped char */
  "\\Foo.c:file:update:deleted:edited::::::::",
  NULL
};

static svn_error_t *
test_read_invalid_tree_conflicts(apr_pool_t *pool)
{
  int i;
  const char *tree_conflict_data;
  apr_hash_t *conflicts;
  svn_error_t *err;

  for (i = 0; broken_tree_conflict_test_data[i] != NULL; i++)
    {
      tree_conflict_data = broken_tree_conflict_test_data[i];
      err = svn_wc__read_tree_conflicts(&conflicts, tree_conflict_data, "",
                                        pool);
      if (err == SVN_NO_ERROR)
        return fail(pool,
                    "Error in broken tree conflict data was not detected:\n"
                    "  %s", tree_conflict_data);
      svn_error_clear(err);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
test_write_tree_conflict(apr_pool_t *pool)
{
  svn_wc_conflict_description_t *conflict;
  const char *tree_conflict_data;
  apr_hash_t *conflicts;
  const char *expected;

  conflict = svn_wc_conflict_description_create_tree("Foo.c", NULL,
                                                     svn_node_file,
                                                     svn_wc_operation_update,
                                                     NULL, NULL, pool);
  conflict->action = svn_wc_conflict_action_delete;
  conflict->reason = svn_wc_conflict_reason_edited;

  conflicts = apr_hash_make(pool);
  apr_hash_set(conflicts, conflict->path, APR_HASH_KEY_STRING, conflict);

  expected = "((conflict Foo.c file update deleted edited "
               "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )))";

  SVN_ERR(svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool));

  if (strcmp(expected, tree_conflict_data) != 0)
    return fail(pool, "Unexpected text from tree conflict\n"
                      "  Expected: %s\n"
                      "  Actual:   %s\n", expected, tree_conflict_data);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_write_2_tree_conflicts(apr_pool_t *pool)
{
  svn_wc_conflict_description_t *conflict1, *conflict2;
  apr_hash_t *conflicts;
  const char *tree_conflict_data;
  const char *expected1;
  const char *expected2;

  conflict1 = svn_wc_conflict_description_create_tree("Foo.c", NULL,
                                                      svn_node_file,
                                                      svn_wc_operation_update,
                                                      NULL, NULL, pool);
  conflict1->action = svn_wc_conflict_action_delete;
  conflict1->reason = svn_wc_conflict_reason_edited;

  conflict2 = svn_wc_conflict_description_create_tree("Bar.h", NULL,
                                                      svn_node_file,
                                                      svn_wc_operation_update,
                                                      NULL, NULL, pool);
  conflict2->action = svn_wc_conflict_action_edit;
  conflict2->reason = svn_wc_conflict_reason_deleted;

  conflicts = apr_hash_make(pool);
  apr_hash_set(conflicts, conflict1->path, APR_HASH_KEY_STRING, conflict1);
  apr_hash_set(conflicts, conflict2->path, APR_HASH_KEY_STRING, conflict2);

  /* We don't know the order the hash will spit out the data, so just test
     for both possibilities. */
  expected1 = "((conflict Foo.c file update deleted edited "
                 "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )) "
               "(conflict Bar.h file update edited deleted "
                 "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )))";
  expected2 = "((conflict Bar.h file update edited deleted "
                 "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )) "
               "(conflict Foo.c file update deleted edited "
                 "(version 0  2 -1 0  0 ) (version 0  2 -1 0  0 )))";

  SVN_ERR(svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool));

  if (strcmp(expected1, tree_conflict_data) != 0
        && strcmp(expected2, tree_conflict_data) != 0)
    return fail(pool, "Unexpected text from tree conflict\n"
                      "  Expected: %s\n"
                      "         OR %s\n"
                      "  Actual:   %s\n", expected1, expected2,
                                          tree_conflict_data);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_write_invalid_tree_conflicts(apr_pool_t *pool)
{
  svn_wc_conflict_description_t *conflict;
  apr_hash_t *conflicts;
  const char *tree_conflict_data;
  svn_error_t *err;

  /* Configure so that we can test for errors caught by SVN_ERR_ASSERT. */
  svn_error_set_malfunction_handler(svn_error_raise_on_malfunction);

  /* victim path */
  conflict = svn_wc_conflict_description_create_tree("", NULL,
                                                     svn_node_file,
                                                     svn_wc_operation_update,
                                                     NULL, NULL, pool);
  conflict->action = svn_wc_conflict_action_delete;
  conflict->reason = svn_wc_conflict_reason_edited;

  conflicts = apr_hash_make(pool);
  apr_hash_set(conflicts, conflict->path, APR_HASH_KEY_STRING, conflict);

  err = svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool);
  if (err == SVN_NO_ERROR)
    return fail(pool,
                "Failed to detect blank conflict victim path");
  svn_error_clear(err);
  svn_hash__clear(conflicts);

  /* node_kind */
  conflict = svn_wc_conflict_description_create_tree("Foo", NULL,
                                                     svn_node_none,
                                                     svn_wc_operation_update,
                                                     NULL, NULL, pool);
  conflict->action = svn_wc_conflict_action_delete;
  conflict->reason = svn_wc_conflict_reason_edited;

  apr_hash_set(conflicts, conflict->path, APR_HASH_KEY_STRING, conflict);

  err = svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool);
  if (err == SVN_NO_ERROR)
    return fail(pool,
                "Failed to detect invalid conflict node_kind");
  svn_error_clear(err);
  svn_hash__clear(conflicts);

  /* operation */
  conflict = svn_wc_conflict_description_create_tree("Foo.c", NULL,
                                                     svn_node_file,
                                                     99,
                                                     NULL, NULL, pool);
  conflict->action = svn_wc_conflict_action_delete;
  conflict->reason = svn_wc_conflict_reason_edited;

  apr_hash_set(conflicts, conflict->path, APR_HASH_KEY_STRING, conflict);

  err = svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool);
  if (err == SVN_NO_ERROR)
    return fail(pool,
                "Failed to detect invalid conflict operation");
  svn_error_clear(err);
  svn_hash__clear(conflicts);

  /* action */
  conflict = svn_wc_conflict_description_create_tree("Foo.c", NULL,
                                                     svn_node_file,
                                                     svn_wc_operation_update,
                                                     NULL, NULL, pool);
  conflict->action = 99;
  conflict->reason = svn_wc_conflict_reason_edited;

  apr_hash_set(conflicts, conflict->path, APR_HASH_KEY_STRING, conflict);

  err = svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool);
  if (err == SVN_NO_ERROR)
    return fail(pool,
                "Failed to detect invalid conflict action");
  svn_error_clear(err);
  svn_hash__clear(conflicts);

  /* reason */
  conflict = svn_wc_conflict_description_create_tree("Foo.c", NULL,
                                                     svn_node_file,
                                                     svn_wc_operation_update,
                                                     NULL, NULL, pool);
  conflict->action = svn_wc_conflict_action_delete;
  conflict->reason = 99;

  apr_hash_set(conflicts, conflict->path, APR_HASH_KEY_STRING, conflict);

  err = svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts, pool);
  if (err == SVN_NO_ERROR)
    return fail(pool,
                "Failed to detect invalid conflict reason");
  svn_error_clear(err);
  svn_hash__clear(conflicts);

  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_read_tree_conflict,
                   "read 1 tree conflict"),
    SVN_TEST_PASS2(test_read_2_tree_conflicts,
                   "read 2 tree conflicts"),
    SVN_TEST_XFAIL2(test_read_invalid_tree_conflicts,
                    "detect broken tree conflict data"),
    SVN_TEST_PASS2(test_write_tree_conflict,
                   "write 1 tree conflict"),
    SVN_TEST_PASS2(test_write_2_tree_conflicts,
                   "write 2 tree conflicts"),
    SVN_TEST_PASS2(test_write_invalid_tree_conflicts,
                   "detect broken tree conflict data while writing"),
    SVN_TEST_NULL
  };

