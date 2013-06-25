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
 * This class is used internally by the JavaHL implementation and not considered
 * part part of the public API.
 */
public abstract class JNIObject
{
    /**
     * slot for the address of the native peer. The JNI code controls this
     * field. If it is set to 0 then underlying JNI object has been freed
     */
    protected final long cppAddr;

    protected JNIObject(long cppAddr)
    {
        this.cppAddr = cppAddr;
    }
}
