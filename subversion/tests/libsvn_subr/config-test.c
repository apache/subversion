/*
 * config-test.c:  tests svn_config
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

/* ====================================================================
   To add tests, look toward the bottom of this file.

*/



#include <string.h>

#include <apr_getopt.h>
#include <apr_pools.h>

#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_config.h"
#include "private/svn_subr_private.h"
#include "private/svn_config_private.h"

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

  return svn_error_create(SVN_ERR_TEST_FAILED, SVN_NO_ERROR, msg);
}

static svn_error_t *
get_config_file_path(const char **cfg_file,
                     const svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  const char *srcdir;

  SVN_ERR(svn_test_get_srcdir(&srcdir, opts, pool));
  *cfg_file = svn_dirent_join(srcdir, "config-test.cfg", pool);

  return SVN_NO_ERROR;
}

static const char *config_keys[] = { "foo", "a", "b", "c", "d", "e", "f", "g",
                                     "h", "i", "m", NULL };
static const char *config_values[] = { "bar", "Aa", "100", "bar",
                                       "a %(bogus)s oyster bar",
                                       "%(bogus)s shmoo %(",
                                       "%Aa", "lyrical bard", "%(unterminated",
                                       "Aa 100", "foo bar baz", NULL };

static svn_error_t *
test_text_retrieval(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_config_t *cfg;
  int i;
  const char *cfg_file;

  SVN_ERR(get_config_file_path(&cfg_file, opts, pool));
  SVN_ERR(svn_config_read3(&cfg, cfg_file, TRUE, FALSE, FALSE, pool));

  /* Test values retrieved from our ConfigParser instance against
     values retrieved using svn_config. */
  for (i = 0; config_keys[i] != NULL; i++)
    {
      const char *key, *py_val, *c_val;

      key = config_keys[i];
      py_val = config_values[i];
      svn_config_get(cfg, &c_val, "section1", key, "default value");
#if 0
      printf("Testing expected value '%s' against '%s' for "
             "option '%s'\n", py_val, c_val, key);
#endif
      /* Fail iff one value is null, or the strings don't match. */
      if ((c_val == NULL) != (py_val == NULL)
          || (c_val != NULL && py_val != NULL && strcmp(c_val, py_val) != 0))
        return fail(pool, "Expected value '%s' not equal to '%s' for "
                    "option '%s'", py_val, c_val, key);
    }

  {
    const char *value = svn_config_get_server_setting(cfg, "server group",
                                                      "setting", "default");
    if (value == NULL || strcmp(value, "default") != 0)
      return svn_error_create(SVN_ERR_TEST_FAILED, SVN_NO_ERROR,
                              "Expected a svn_config_get_server_setting()"
                              "to return 'default'");
  }

  return SVN_NO_ERROR;
}


static const char *true_keys[] = {"true1", "true2", "true3", "true4",
                                  NULL};
static const char *false_keys[] = {"false1", "false2", "false3", "false4",
                                   NULL};

static svn_error_t *
test_boolean_retrieval(const svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  svn_config_t *cfg;
  int i;
  const char *cfg_file;

  SVN_ERR(get_config_file_path(&cfg_file, opts, pool));
  SVN_ERR(svn_config_read3(&cfg, cfg_file, TRUE, FALSE, FALSE, pool));

  for (i = 0; true_keys[i] != NULL; i++)
    {
      svn_boolean_t value;
      SVN_ERR(svn_config_get_bool(cfg, &value, "booleans", true_keys[i],
                                  FALSE));
      if (!value)
        return fail(pool, "Value of option '%s' is not true", true_keys[i]);
    }

  for (i = 0; false_keys[i] != NULL; i++)
    {
      svn_boolean_t value;
      SVN_ERR(svn_config_get_bool(cfg, &value, "booleans", false_keys[i],
                                  TRUE));
      if (value)
        return fail(pool, "Value of option '%s' is not true", false_keys[i]);
    }

  {
    svn_error_t *err;
    svn_boolean_t value;

    svn_error_clear((err = svn_config_get_bool(cfg, &value,
                                               "booleans", "bad_true",
                                               TRUE)));
    if (!err)
      return fail(pool, "No error on bad truth value");

    svn_error_clear((err = svn_config_get_bool(cfg, &value,
                                               "booleans", "bad_false",
                                               FALSE)));
    if (!err)
      return fail(pool, "No error on bad truth value");
  }

  {
    svn_boolean_t value;
    SVN_ERR(svn_config_get_server_setting_bool(cfg, &value, "server group",
                                               "setting", FALSE));
    if (value)
      return svn_error_create(SVN_ERR_TEST_FAILED, SVN_NO_ERROR,
                              "Expected a svn_config_get_server_setting_bool()"
                              "to return FALSE, but it returned TRUE");
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_has_section_case_insensitive(const svn_test_opts_t *opts,
                                  apr_pool_t *pool)
{
  svn_config_t *cfg;
  const char *cfg_file;

  SVN_ERR(get_config_file_path(&cfg_file, opts, pool));
  SVN_ERR(svn_config_read3(&cfg, cfg_file, TRUE, FALSE, FALSE, pool));

  if (! svn_config_has_section(cfg, "section1"))
    return fail(pool, "Failed to find section1");

  if (! svn_config_has_section(cfg, "SECTION1"))
    return fail(pool, "Failed to find SECTION1");

  if (! svn_config_has_section(cfg, "UpperCaseSection"))
    return fail(pool, "Failed to find UpperCaseSection");

  if (! svn_config_has_section(cfg, "uppercasesection"))
    return fail(pool, "Failed to find UpperCaseSection");

  if (svn_config_has_section(cfg, "notthere"))
    return fail(pool, "Returned true on missing section");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_has_section_case_sensitive(const svn_test_opts_t *opts,
                                apr_pool_t *pool)
{
  svn_config_t *cfg;
  const char *cfg_file;

  SVN_ERR(get_config_file_path(&cfg_file, opts, pool));
  SVN_ERR(svn_config_read3(&cfg, cfg_file, TRUE, TRUE, FALSE, pool));

  if (! svn_config_has_section(cfg, "section1"))
    return fail(pool, "Failed to find section1");

  if (svn_config_has_section(cfg, "SECTION1"))
    return fail(pool, "Returned true on missing section");

  if (! svn_config_has_section(cfg, "UpperCaseSection"))
    return fail(pool, "Failed to find UpperCaseSection");

  if (svn_config_has_section(cfg, "uppercasesection"))
    return fail(pool, "Returned true on missing section");

  if (svn_config_has_section(cfg, "notthere"))
    return fail(pool, "Returned true on missing section");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_has_option_case_sensitive(const svn_test_opts_t *opts,
                               apr_pool_t *pool)
{
  svn_config_t *cfg;
  const char *cfg_file;
  apr_int64_t value;
  int i;

  static struct test_dataset {
    const char *option;
    apr_int64_t value;
  } const test_data[] = {
    { "a", 1 },
    { "A", 2 },
    { "B", 3 },
    { "b", 4 }
  };
  static const int test_data_size = sizeof(test_data)/sizeof(*test_data);

  SVN_ERR(get_config_file_path(&cfg_file, opts, pool));
  SVN_ERR(svn_config_read3(&cfg, cfg_file, TRUE, TRUE, TRUE, pool));

  for (i = 0; i < test_data_size; ++i)
    {
      SVN_ERR(svn_config_get_int64(cfg, &value, "case-sensitive-option",
                                   test_data[i].option, -1));
      if (test_data[i].value != value)
        return fail(pool,
                    apr_psprintf(pool,
                                 "case-sensitive-option.%s != %"
                                 APR_INT64_T_FMT" but %"APR_INT64_T_FMT,
                                 test_data[i].option,
                                 test_data[i].value,
                                 value));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_stream_interface(const svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  svn_config_t *cfg;
  const char *cfg_file;
  svn_stream_t *stream;

  SVN_ERR(get_config_file_path(&cfg_file, opts, pool));
  SVN_ERR(svn_stream_open_readonly(&stream, cfg_file, pool, pool));

  SVN_ERR(svn_config_parse(&cfg, stream, TRUE, TRUE, pool));

  /* nominal test to make sure cfg is populated with something since
   * svn_config_parse will happily return an empty cfg if the stream is
   * empty. */
  if (! svn_config_has_section(cfg, "section1"))
    return fail(pool, "Failed to find section1");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_ignore_bom(apr_pool_t *pool)
{
  svn_config_t *cfg;
  svn_string_t *cfg_string = svn_string_create("\xEF\xBB\xBF[s1]\nfoo=bar\n",
                                               pool);
  svn_stream_t *stream = svn_stream_from_string(cfg_string, pool);

  SVN_ERR(svn_config_parse(&cfg, stream, TRUE, TRUE, pool));

  if (! svn_config_has_section(cfg, "s1"))
    return fail(pool, "failed to find section s1");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_read_only_mode(const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  svn_config_t *cfg;
  svn_config_t *cfg2;
  const char *cfg_file;

  SVN_ERR(get_config_file_path(&cfg_file, opts, pool));
  SVN_ERR(svn_config_read3(&cfg, cfg_file, TRUE, TRUE, FALSE, pool));

  /* setting CFG to r/o mode shall toggle the r/o mode and expand values */

  SVN_TEST_ASSERT(!svn_config__is_read_only(cfg));
  SVN_TEST_ASSERT(!svn_config__is_expanded(cfg, "section1", "i"));

  svn_config__set_read_only(cfg, pool);

  SVN_TEST_ASSERT(svn_config__is_read_only(cfg));
  SVN_TEST_ASSERT(svn_config__is_expanded(cfg, "section1", "i"));

  /* copies should be r/w with values */

  SVN_ERR(svn_config_dup(&cfg2, cfg, pool));
  SVN_TEST_ASSERT(!svn_config__is_read_only(cfg2));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_expand(const svn_test_opts_t *opts,
            apr_pool_t *pool)
{
  svn_config_t *cfg;
  const char *cfg_file, *val;

  SVN_ERR(get_config_file_path(&cfg_file, opts, pool));
  SVN_ERR(svn_config_read3(&cfg, cfg_file, TRUE, TRUE, FALSE, pool));

  /* Get expanded "g" which requires expanding "c". */
  svn_config_get(cfg, &val, "section1", "g", NULL);

  /* Get expanded "c". */
  svn_config_get(cfg, &val, "section1", "c", NULL);

  /* With pool debugging enabled this ensures that the expanded value
     of "c" was not created in a temporary pool when expanding "g". */
  SVN_TEST_STRING_ASSERT(val, "bar");

  /* Get expanded "j" and "k" which have cyclic definitions.
   * They must return empty values. */
  svn_config_get(cfg, &val, "section1", "j", NULL);
  SVN_TEST_STRING_ASSERT(val, "");
  svn_config_get(cfg, &val, "section1", "k", NULL);
  SVN_TEST_STRING_ASSERT(val, "");

  /* Get expanded "l" which depends on a cyclic definition.
   * So, it also considered "undefined" and will be normalized to "". */
  svn_config_get(cfg, &val, "section1", "l", NULL);
  SVN_TEST_STRING_ASSERT(val, "");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_invalid_bom(apr_pool_t *pool)
{
  svn_config_t *cfg;
  svn_error_t *err;
  svn_string_t *cfg_string;
  svn_stream_t *stream;

  cfg_string = svn_string_create("\xEF", pool);
  stream = svn_stream_from_string(cfg_string, pool);
  err = svn_config_parse(&cfg, stream, TRUE, TRUE, pool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_MALFORMED_FILE);

  cfg_string = svn_string_create("\xEF\xBB", pool);
  stream = svn_stream_from_string(cfg_string, pool);
  err = svn_config_parse(&cfg, stream, TRUE, TRUE, pool);
  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_MALFORMED_FILE);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_serialization(apr_pool_t *pool)
{
  svn_stringbuf_t *original_content;
  svn_stringbuf_t *written_content;
  svn_config_t *cfg;

  const struct
    {
      const char *section;
      const char *option;
      const char *value;
    } test_data[] =
    {
      { "my section", "value1", "some" },
      { "my section", "value2", "something" },
      { "another Section", "value1", "one" },
      { "another Section", "value2", "two" },
      { "another Section", "value 3", "more" },
    };
  int i;

  /* Format the original with the same formatting that the writer will use. */
  original_content = svn_stringbuf_create("\n[my section]\n"
                                          "value1=some\n"
                                          "value2=%(value1)sthing\n"
                                          "\n[another Section]\n"
                                          "value1=one\n"
                                          "value2=two\n"
                                          "value 3=more\n",
                                          pool);
  written_content = svn_stringbuf_create_empty(pool);

  SVN_ERR(svn_config_parse(&cfg,
                           svn_stream_from_stringbuf(original_content, pool),
                           TRUE, TRUE, pool));
  SVN_ERR(svn_config__write(svn_stream_from_stringbuf(written_content, pool),
                            cfg, pool));
  SVN_ERR(svn_config_parse(&cfg,
                           svn_stream_from_stringbuf(written_content, pool),
                           TRUE, TRUE, pool));

  /* The serialized and re-parsed config must have the expected contents. */
  for (i = 0; i < sizeof(test_data) / sizeof(test_data[0]); ++i)
    {
      const char *val;
      svn_config_get(cfg, &val, test_data[i].section, test_data[i].option,
                     NULL);
      SVN_TEST_STRING_ASSERT(val, test_data[i].value);
    }

  return SVN_NO_ERROR;
}

/*
   ====================================================================
   If you add a new test to this file, update this array.

   (These globals are required by our included main())
*/

/* An array of all test functions */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_text_retrieval,
                       "test svn_config"),
    SVN_TEST_OPTS_PASS(test_boolean_retrieval,
                       "test svn_config boolean conversion"),
    SVN_TEST_OPTS_PASS(test_has_section_case_insensitive,
                       "test svn_config_has_section (case insensitive)"),
    SVN_TEST_OPTS_PASS(test_has_section_case_sensitive,
                       "test svn_config_has_section (case sensitive)"),
    SVN_TEST_OPTS_PASS(test_has_option_case_sensitive,
                       "test case-sensitive option name lookup"),
    SVN_TEST_OPTS_PASS(test_stream_interface,
                       "test svn_config_parse"),
    SVN_TEST_PASS2(test_ignore_bom,
                   "test parsing config file with BOM"),
    SVN_TEST_OPTS_PASS(test_read_only_mode,
                       "test r/o mode"),
    SVN_TEST_OPTS_PASS(test_expand,
                       "test variable expansion"),
    SVN_TEST_PASS2(test_invalid_bom,
                   "test parsing config file with invalid BOM"),
    SVN_TEST_PASS2(test_serialization,
                   "test writing a config"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
