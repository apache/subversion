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

#include <algorithm>
#include <iomanip>
#include <ios>
#include <iostream>

#include "svncxxhl.hpp"

#include <apr.h>
#include "svn_error.h"

namespace {
void trace(const svn::error::message& msg)
{
  std::cout << "    ";
  if (msg.first)
    std::cout << "test_exception: E"
              << std::setw(6) << std::setfill('0') << std::right
              << msg.first << ':' << ' ';
  std::cout << msg.second << std::endl;
}

void traceall(const char *message, const svn::error& err)
{
  typedef svn::error::message_list message_list;
  std::cout << message << std::endl;
  std::cout << "Traced Messages:" << std::endl;
  message_list ml = err.traced_messages();
  std::for_each(ml.begin(), ml.end(), trace);
  std::cout << "Just Messages:" << std::endl;
  ml = err.messages();
  std::for_each(ml.begin(), ml.end(), trace);
}
} // anonymous namespace


bool test_cancel()
{
  try
    {
      svn_error_t* err;
      err = svn_error_create(SVN_ERR_TEST_FAILED, NULL, "original message");
      err = svn_error_create(SVN_ERR_BASE, err, "wrapper message");
      err = svn_error_create(SVN_ERR_CANCELLED, err, NULL);
      err = svn_error_create(SVN_ERR_CANCELLED, err, NULL);
      err = svn_error_trace(err);
      svn::error::throw_svn_error(err);
    }
  catch (const svn::cancelled& err)
    {
      traceall("Caught: CANCEL", err);
      return true;
    }
  catch (const svn::error& err)
    {
      traceall("Caught: ERROR", err);
      return false;
    }
  catch (...)
    {
      return false;
    }
  return false;
}

int test_error()
{
  try
    {
      svn_error_t* err;
      err = svn_error_create(SVN_ERR_TEST_FAILED, NULL, "original message");
      err = svn_error_create(SVN_ERR_BASE, err, "wrapper message");
      err = svn_error_create(SVN_ERR_CANCELLED, err, NULL);
      err = svn_error_create(SVN_ERR_CANCELLED, err, NULL);
      err = svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, err, NULL);
      err = svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, err, NULL);
      err = svn_error_trace(err);
      svn::error::throw_svn_error(err);
    }
  catch (const svn::cancelled& err)
    {
      traceall("Caught: CANCEL", err);
      return false;
    }
  catch (const svn::error& err)
    {
      traceall("Caught: ERROR", err);
      return true;
    }
  catch (...)
    {
      return false;
    }
  return false;
}

int main()
{
  apr_initialize();

  const char *stat  = (test_cancel() ? "OK" : "ERROR");
  std::cerr << "test_cancel .... " << stat << std::endl;

  stat = (test_error() ? "OK" : "ERROR");
  std::cerr << "test_error ..... " << stat << std::endl;

  apr_terminate();
  return 0;
}
