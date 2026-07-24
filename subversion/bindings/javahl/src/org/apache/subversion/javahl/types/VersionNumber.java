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

package org.apache.subversion.javahl.types;

/**
 * A package-level derivation of Version that can hold arbitrary
 * version numbers used by {@link Version::getInstance}.
 * @since 1.15
 */
class VersionNumber extends Version
{
    VersionNumber(int major, int minor, int patch)
    {
        this.major = major;
        this.minor = minor;
        this.patch = patch;
    }

    /**
     * @return The version string <code>MAJOR.MINOR.PATCH</code>.
     */
    @Override
    public String toString()
    {
        StringBuffer version = new StringBuffer();
        version.append(getMajor())
            .append('.').append(getMinor())
            .append('.').append(getPatch());
        return version.toString();
    }

    /**
     * @return The major version number..
     */
    @Override
    public int getMajor()
    {
        return major;
    }

    /**
     * @return The minor version number..
     */
    @Override
    public int getMinor()
    {
        return minor;
    }

    /**
     * @return The patch-level version number.
     */
    @Override
    public int getPatch()
    {
        return patch;
    }

    private final int major;
    private final int minor;
    private final int patch;
}
