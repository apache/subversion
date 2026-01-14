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

package org.apache.subversion.javahl;

/**
 * This checked exception is thrown whenever something goes wrong with
 * the Subversion JavaHL bindings.
 */
public class SubversionException extends Exception
{
    // Update the serialVersionUID when there is an incompatible change made to
    // this class.  See the Java documentation (following link or its counter-
    // part in your specific Java release) for when a change is incompatible.
    // https://docs.oracle.com/en/java/javase/11/docs/specs/serialization/version.html#type-changes-affecting-serialization
    private static final long serialVersionUID = 1L;

    /**
     * This constructor is only used by sub-classes.
     *
     * @param message A description of the problem.
     */
    protected SubversionException(String message)
    {
        super(message);
    }

    /**
     * This constructor is only used by sub-classes and the native
     * implementation.
     *
     * @param message A description of the problem.
     * @param cause The root cause of the exception.
     */
    protected SubversionException(String message, Throwable cause)
    {
        super(message, cause);
    }
}
