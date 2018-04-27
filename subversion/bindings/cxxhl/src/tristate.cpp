/**
 * @copyright
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
 * @endcopyright
 */

#include "svncxxhl/tristate.hpp"

#include "svn_types.h"
#undef TRUE
#undef FALSE

namespace apache {
namespace subversion {
namespace cxxhl {

Tristate::Tristate(short value) throw()
    : m_value(value)
{}

const Tristate Tristate::TRUE = Tristate(svn_tristate_true);
const Tristate Tristate::FALSE = Tristate(svn_tristate_false);
const Tristate Tristate::UNKNOWN = Tristate(svn_tristate_unknown);

} // namespace cxxhl
} // namespace subversion
} // namespace apache
