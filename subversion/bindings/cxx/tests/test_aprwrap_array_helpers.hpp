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

#ifndef __cplusplus
#error "This is a C++ header file."
#endif

#ifndef SVNXX_TEST_APRWRAP_ARRAY_HELPERS_HPP
#define SVNXX_TEST_APRWRAP_ARRAY_HELPERS_HPP

namespace {
// Create a randomly-ordered array of constant strings.
apr_array_header_t* fill_array(APR::Pool& pool)
{
  apr_array_header_t* a = apr_array_make(pool.get(), 0, sizeof(const char*));
  APR_ARRAY_PUSH(a, const char*) = "primus";
  APR_ARRAY_PUSH(a, const char*) = "secundus";
  APR_ARRAY_PUSH(a, const char*) = "tertius";
  APR_ARRAY_PUSH(a, const char*) = "quartus";
  APR_ARRAY_PUSH(a, const char*) = "quintus";
  APR_ARRAY_PUSH(a, const char*) = "sextus";
  APR_ARRAY_PUSH(a, const char*) = "septimus";
  std::random_shuffle(&APR_ARRAY_IDX(a, 0, const char*),
                      &APR_ARRAY_IDX(a, a->nelts, const char*));
  return a;
}
} // anonymous namespace

#endif  // SVNXX_TEST_APRWRAP_ARRAY_HELPERS_HPP
