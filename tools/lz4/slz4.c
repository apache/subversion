/* slz4.c -- test driver for LZ4 de/compression
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


/*
 * This compression driver can create create files that can be
 * read by 'unlz4' and decmpress files created by 'lz4'.
 */


#include <stdio.h>
#include <stdlib.h>

#include <apr.h>

#include "svn_error.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "private/svn_io_private.h"


static void handle_error(svn_error_t *svn_err, int exitcode)
{
  if (svn_err)
    {
      svn_handle_error2(svn_err, stdout, FALSE, "slz4: ");
      exit(exitcode);
    }
}


int main(int argc, char *argv[])
{
  apr_pool_t *pool;
  svn_stream_t *istream;
  svn_stream_t *ostream;
  int compress = 1;

  if (argc >= 2)
    {
      int usage = 0;

      if (0 == strcmp(argv[1], "-d"))
        compress = 0;
      else if (0 == strcmp(argv[1], "-c"))
        compress = 1;
      else
        {
          fprintf(stderr, "error: unknown option: %s\n", argv[1]);
          usage = 1;
        }
      if (argc > 2)
        {
          fprintf(stderr, "error: too many arguments\n");
          usage = 1;
        }

      if (usage)
        {
          fprintf(stderr,
                  "Usage: %s [-c|-d] <{input} >{output}\n"
                  "   -c  compress {input} to {output}     [DEFAULT]\n"
                  "   -d  decompress {output} from {input}\n",
                  argv[0]);
          exit(2);
        }
    }

  apr_initialize();
  atexit(apr_terminate);

  pool = svn_pool_create(NULL);
  handle_error(svn_stream_for_stdin2(&istream, TRUE, pool), 2);
  handle_error(svn_stream_for_stdout(&ostream, pool), 2);

  if (compress)
    ostream = svn_stream__lz4_compressed(ostream, pool);
  else
    istream = svn_stream__lz4_compressed(istream, pool);

  handle_error(svn_stream_copy3(istream, ostream, NULL, NULL, pool), 1);
  return 0;
}
