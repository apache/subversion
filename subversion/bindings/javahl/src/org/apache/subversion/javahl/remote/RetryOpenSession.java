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

package org.apache.subversion.javahl.remote;

import org.apache.subversion.javahl.SubversionException;

/**
 * This checked exception is thrown only from ISVNClient.openRemoteSession
 * or RemoteFactory.openRemoteSession if a session could not be opened
 * due to a redirect.
 */
public class RetryOpenSession extends SubversionException
{
    // Update the serialVersionUID when there is a incompatible change
    // made to this class.
    private static final long serialVersionUID = 1L;

    /**
     * This constructor is only called from native code.
     */
    protected RetryOpenSession(String message, String correctedUrl)
    {
        super(message);
        this.correctedUrl = correctedUrl;
    }

    /**
     * @return the corrected URL for the session.
     */
    public String getCorrectedUrl()
    {
        return correctedUrl;
    }

    private final String correctedUrl;
}
