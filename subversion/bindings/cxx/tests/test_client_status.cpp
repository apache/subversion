/*
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

#include <boost/test/unit_test.hpp>

#include "fixture_init.hpp"

#include <iostream>

#include "svnxx/client/status.hpp"

namespace svn = ::apache::subversion::svnxx;

BOOST_AUTO_TEST_SUITE(client_status,
                      * boost::unit_test::fixture<init>());

BOOST_AUTO_TEST_CASE(example,
                     * boost::unit_test::disabled())
{
  const char working_copy_root[] = "/Users/brane/src/svn/repos/trunk";

  const auto callback = [](const char* path,
                           const svn::client::status_notification&)
                          {
                            std::cout << "status on: " << path << std::endl;
                          };
  svn::client::context ctx;

  const auto revnum = svn::client::status(ctx, working_copy_root,
                                          svn::revision(),
                                          svn::depth::unknown,
                                          svn::client::status_flags::empty,
                                          callback);
  std::cout << "got revision: " << long(revnum) << std::endl;
}

BOOST_AUTO_TEST_SUITE_END();
