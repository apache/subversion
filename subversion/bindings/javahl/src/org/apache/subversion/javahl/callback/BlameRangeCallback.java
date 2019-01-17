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

/**
 * This interface is used to the resolved revision range
 * the {@link ISVNClient#blame} call.
 * @since 1.12
 */
public interface BlameRangeCallback
{
    /**
     * This method will be called once before #{BlameLineCallback.singleLine}
     * is called for the first time.
     * @param startRevision     the resolved start of the blame range.
     * @param endRevision       the resolved end of the blame range.
     */
    public void setRange(long startRevision, long endRevision)
        throws ClientException;
}
