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

import org.apache.subversion.javahl.ISVNClient;
import org.apache.subversion.javahl.types.NodeKind;

/**
 * This interface is used to filter imported nodes in the
 * {@link ISVNClient#import} call.
 * @since 1.8
 */
public interface ImportFilterCallback
{
    /**
     * This method will be called for each node.  Returns true to filter the
     * node (and its descendants) from the import.
     */
    public boolean filter(String path, NodeKind kind, boolean special);
}
