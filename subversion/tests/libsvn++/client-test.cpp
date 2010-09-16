/*
 * util-test.c -- test the libsvn++ utilities
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

#include "../svn_test.h"

#include "../../bindings/c++/Client.h"

using namespace SVN;

static svn_error_t *
test_get_version(apr_pool_t *p)
{
  Client client;

  Version v = client.getVersion();
  SVN_TEST_ASSERT(v.getTag() == SVN_VER_NUMTAG);

  return SVN_NO_ERROR;
}

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_get_version,
                   "test get client version"),
    SVN_TEST_NULL
  };
