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

package org.apache.subversion.javahl.ra;

import org.apache.subversion.javahl.JNIObject;

public class SVNRa extends JNIObject implements ISVNRa
{
    @Override
    public native long getLatestRevision();

    @Override
    public native void finalize() throws Throwable;

    @Override
    public native void dispose();

    /*
     * NOTE: This field is accessed from native code for callbacks.
     */
    private RaContext sessionContext = new RaContext();

    /**
     * This constructor is called from JNI to get an instance call getRaSession
     * method of ISVNClient
     * 
     * @param cppAddr
     */
    protected SVNRa(long cppAddr)
    {
        super(cppAddr);
    }
}
