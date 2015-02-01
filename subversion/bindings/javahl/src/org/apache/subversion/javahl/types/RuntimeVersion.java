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

package org.apache.subversion.javahl.types;

/**
 * Encapsulates the run-time version of the
 * <code>libsvn_clinent</code> library that was loaded by the current
 * process. The result may be different from that provided by {@link
 * Version}, because the latter returns compile-time, not run-time
 * information.
 */
public class RuntimeVersion extends Version
{
    /**
     * @return The full version string for the loaded library,
     * as defined by <code>MAJOR.MINOR.PATCH-NUMTAG</code>.
     */
    public String toString()
    {
        StringBuffer version = new StringBuffer();
        version.append(getMajor())
            .append('.').append(getMinor())
            .append('.').append(getPatch())
            .append(getNumberTag());
        return version.toString();
    }

    /**
     * @return The major version number for the loaded JavaHL library.
     */
    @Override
    public native int getMajor();

    /**
     * @return The minor version number for the loaded JavaHL library.
     */
    @Override
    public native int getMinor();

    /**
     * @return The patch-level version number for the loaded JavaHL
     * library.
     */
    @Override
    public native int getPatch();

    /**
     * @return Some text further describing the library version
     * (e.g. "r1234", "Alpha 1", "dev build", etc.).
     */
    private native String getNumberTag();
}
