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

#ifndef SVN_JAVAHL_JNIWRAPPER_JAVAHL_EXCEPTION_HPP
#define SVN_JAVAHL_JNIWRAPPER_JAVAHL_EXCEPTION_HPP

#include <exception>
#include <stdexcept>

namespace JavaHL {

/**
 * A C++ exception object for signalling that a Java exception has
 * been thrown.
 *
 * Thrown to unwind the stack while avoiding code clutter when a Java
 * exception is detected in the JNI environment.
 *
 * @since New in 1.9.
 */
class JavaException : public std::exception
{
public:
  explicit JavaException()
    {}

  virtual ~JavaException() throw();

  virtual const char* what() const throw();
};

} // namespace JavaHL

#endif // SVN_JAVAHL_JNIWRAPPER_JAVAHL_EXCEPTION_HPP
