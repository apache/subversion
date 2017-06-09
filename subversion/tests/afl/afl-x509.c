/*
 * afl-x509.c an American Fuzz Lop test
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
 *
 */

/*  The input data can either be a file on disk or provided via stdin:

       afl-x509 some-file
       afl-x509 < some-file

    In practice the file simply contains random binary data. The data
    are interpreted as a (base64 decoded) x509 cert and a parse is
    attempted. */

#include "svn_x509.h"
#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_io.h"

#include <stdlib.h>

static svn_error_t *
parse(const char *filename, apr_pool_t *pool)
{
  svn_stringbuf_t *buf;
  svn_x509_certinfo_t *certinfo;

  SVN_ERR(svn_stringbuf_from_file2(&buf, filename, pool));
  SVN_ERR(svn_x509_parse_cert(&certinfo, buf->data, buf->len, pool, pool));

  return SVN_NO_ERROR;
}

int main(int argc, char **argv)
{
  apr_pool_t *pool;
  int exit_code = EXIT_SUCCESS;
  svn_error_t *err;
  const char *filename;

  if (argc == 2)
    filename = argv[1];
  else
    filename = "-";

  if (svn_cmdline_init("afl-x509", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;
  pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

  err = parse(filename, pool);
  if (err)
    exit_code = EXIT_FAILURE;
  svn_error_clear(err);
  svn_pool_destroy(pool);
  return exit_code;
}
