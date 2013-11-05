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

import java.util.Collection;
import java.util.Map;
import org.apache.subversion.javahl.ISVNClient;

/**
 * This interface is used to return regular and inherited property
 * lists for each path in a {@link ISVNClient#properties} call.
 *
 * @since 1.8
 */
public interface InheritedProplistCallback
{

    /**
     * Describes properties inherited from one parent.
     */
    public class InheritedItem
    {
        /**
         * The path or URL of the owner of the inherited property.
         */
        public final String path_or_url;

        /**
         * the inherited properties
         */
        public final Map<String, byte[]> properties;

        public InheritedItem(String path_or_url, Map<String, byte[]> properties)
        {
            this.path_or_url = path_or_url;
            this.properties = properties;
        }
    }

    /**
     * The method will be called once for every file.
     * @param path        the path.
     * @param properties  the properties on the path.
     * @param inherited_properties
     *        depth-first ordered array of inherited properties..
     */
    public void singlePath(String path,
                           Map<String, byte[]> properties,
                           Collection<InheritedItem> inherited_properties);
}
