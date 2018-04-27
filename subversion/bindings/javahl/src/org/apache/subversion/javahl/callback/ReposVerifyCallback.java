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

package org.apache.subversion.javahl.callback;

import org.apache.subversion.javahl.ClientException;
import org.apache.subversion.javahl.types.Revision;
import org.apache.subversion.javahl.callback.ReposNotifyCallback;
import org.apache.subversion.javahl.callback.ReposVerifyCallback;

import java.util.EventListener;

/**
 * Error notifications from
 * {@link ISVNRepos#verify(File,Revision,Revision,boolean,boolean,ReposNotifyCallback,ReposVerifyCallback)}.
 *
 * @since 1.9
 */
public interface ReposVerifyCallback extends EventListener
{
    /**
     * This callback method is invoked every time {@link ISVNRepos#verify}
     * encounters an error.
     *<p>
     * The implementation can either consume <code>verifyError</code>
     * and return normally to continue verifying the repository after
     * an error, or throw <code>verifyError</code> (or some other
     * exception) to indicate that verification should stop. In the
     * second case, the thrown exception will propagate to the caller
     * of {@link ISVNRepos#verify}.
     *
     * @param revision The revision that caused the error.
     *          If <code>revision</code> is {@link Revision#SVN_INVALID_REVNUM},
     *          the error occurred during metadata verification.
     * @param verifyError The verification error.
     * @throws ClientException
     */
    void onVerifyError(long revision, ClientException verifyError)
        throws ClientException;
}
