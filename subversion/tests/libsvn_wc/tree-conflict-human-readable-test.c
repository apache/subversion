/*
 * tree-conflict-human-readable-test.c -- test the generation of
 * human-readable tree conflict reports.
 */

#include <stdio.h>
#include <string.h>
#include <apr_hash.h>
#include <apr_tables.h>

#include "svn_pools.h"
#include "svn_types.h"
#include "svn_wc.h"
#include "../svn_test.h"

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
test_get_one_human_readable_tree_conflict_description(const char **msg,
                                                      svn_boolean_t msg_only,
                                                      svn_test_opts_t *opts,
                                                      apr_pool_t *pool)
{
  svn_stringbuf_t *description;
  svn_wc_conflict_description_t *conflict;
  const char *expected;

  *msg = "append 1 human-readable desc";

  if (msg_only)
    return SVN_NO_ERROR;

  conflict = apr_pcalloc(pool, sizeof(svn_wc_conflict_description_t));
  conflict->victim_path  = "Foo.c";
  conflict->node_kind    = svn_node_file;
  conflict->operation    = svn_wc_operation_update;
  conflict->action       = svn_wc_conflict_action_delete;
  conflict->reason       = svn_wc_conflict_reason_edited;

  /*
   * If subversion/libsvn_wc/tree_conflicts.c:new_tree_conflict_phrases()
   * is changed, don't forget to update this string!
   */
  expected = "The update attempted to delete 'Foo.c'\n"
             "(possibly as part of a rename operation).\n"
             "You have edited 'Foo.c' locally.\n";

  description = svn_stringbuf_create("", pool);

  SVN_ERR(svn_wc_append_human_readable_tree_conflict_description(description,
                                                                 conflict,
                                                                 pool));

  if (strcmp(expected, description->data) != 0)
    return fail(pool, "Unexpected text from tree conflict");

  return SVN_NO_ERROR;
}

/* Test data for test_write_tree_conflict_desc. */
static struct svn_wc_conflict_description_t write_test_descriptions[] = {
  /* Test 1 */
  {"",                             /* path */
    svn_node_file,                 /* node_kind */
    svn_wc_conflict_kind_tree,     /* kind */
    NULL,                          /* property_name */
    FALSE,                         /* is_binary */
    NULL,                          /* mime_type */
    NULL,                          /* access */
    svn_wc_conflict_action_delete, /* action */
    svn_wc_conflict_reason_edited, /* reason */
    NULL,                          /* base_file */
    NULL,                          /* their_file */
    NULL,                          /* my_file */
    NULL,                          /* merged_file */
    svn_wc_operation_update,       /* operation */
    "Foo.c"                        /* victim_path */
  },
  /* Test 2 */
  {"",                             /* path */
    svn_node_file,                 /* node_kind */
    svn_wc_conflict_kind_tree,     /* kind */
    NULL,                          /* property_name */
    FALSE,                         /* is_binary */
    NULL,                          /* mime_type */
    NULL,                          /* access */
    svn_wc_conflict_action_edit,   /* action */
    svn_wc_conflict_reason_deleted,/* reason */
    NULL,                          /* base_file */
    NULL,                          /* their_file */
    NULL,                          /* my_file */
    NULL,                          /* merged_file */
    svn_wc_operation_update,       /* operation */
    "Foo.c"                        /* victim_path */
  },
  /* Test 3 */
  {"",                             /* path */
    svn_node_file,                 /* node_kind */
    svn_wc_conflict_kind_tree,     /* kind */
    NULL,                          /* property_name */
    FALSE,                         /* is_binary */
    NULL,                          /* mime_type */
    NULL,                          /* access */
    svn_wc_conflict_action_edit,   /* action */
    svn_wc_conflict_reason_missing,/* reason */
    NULL,                          /* base_file */
    NULL,                          /* their_file */
    NULL,                          /* my_file */
    NULL,                          /* merged_file */
    svn_wc_operation_merge,        /* operation */
    "Foo.c"                        /* victim_path */
  },
  /* end */
  {NULL,                           /* path */
    -1,                            /* node_kind */
    -1,                            /* kind */
    NULL,                          /* property_name */
    -1,                            /* is_binary */
    NULL,                          /* mime_type */
    NULL,                          /* access */
    -1,                            /* action */
    -1,                            /* reason */
    NULL,                          /* base_file */
    NULL,                          /* their_file */
    NULL,                          /* my_file */
    NULL,                          /* merged_file */
    -1,                            /* operation */
    NULL                           /* victim_path */
  },
};

/* Expected output for test_write_tree_conflict_desc.
 * Keep this in sync with the test data above.
 */
static const char* write_test_expected_output =
  /* Test 1 */
  "The update attempted to delete 'Foo.c'\n"
  "(possibly as part of a rename operation).\n"
  "You have edited 'Foo.c' locally.\n"
  "\n"
  /* Test 2 */
  "The update attempted to edit 'Foo.c'.\n"
  "You have deleted 'Foo.c' locally.\n"
  "Maybe you renamed it?\n"
  "\n"
  /* Test 3 */
  "The merge attempted to edit 'Foo.c'.\n"
  "'Foo.c' does not exist locally.\n"
  "Maybe you renamed it? Or has it been"
  " renamed in the history of the branch\n"
  "you are merging into?\n"
  /* end */
  "\n";

static svn_error_t *
test_get_multiple_human_readable_tree_conflict_descriptions(const char **msg,
                                                         svn_boolean_t msg_only,
                                                         svn_test_opts_t *opts,
                                                         apr_pool_t *pool)
{
  svn_stringbuf_t *descriptions;
  svn_wc_conflict_description_t *conflict;

  *msg = "append human-readable descs";

  if (msg_only)
    return SVN_NO_ERROR;

  /*
   * If subversion/libsvn_wc/tree_conflicts.c:new_tree_conflict_phrases()
   * is changed, don't forget to update this string!
   */
  descriptions = svn_stringbuf_create("", pool);

  conflict = &write_test_descriptions[0];

  while (conflict->victim_path != NULL)
    {
      SVN_ERR(svn_wc_append_human_readable_tree_conflict_description(
                                                                 descriptions,
                                                                 conflict,
                                                                 pool));
      svn_stringbuf_appendcstr(descriptions, "\n");
      conflict++;
    }

  if (strcmp(write_test_expected_output, descriptions->data) != 0)
    return fail(pool, "Unexpected text from tree conflict:\n"
                "expected: '%s'\nactual: '%s'\n",
                write_test_expected_output,
                descriptions->data);

  return SVN_NO_ERROR;
}

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_get_one_human_readable_tree_conflict_description),
    SVN_TEST_PASS(test_get_multiple_human_readable_tree_conflict_descriptions),
    SVN_TEST_NULL
  };

