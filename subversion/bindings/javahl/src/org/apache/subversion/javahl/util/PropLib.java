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

package org.apache.subversion.javahl.util;

import org.apache.subversion.javahl.SVNUtil;
import org.apache.subversion.javahl.ClientException;
import org.apache.subversion.javahl.SubversionException;
import org.apache.subversion.javahl.NativeResources;
import org.apache.subversion.javahl.types.ExternalItem;
import org.apache.subversion.javahl.types.NodeKind;
import org.apache.subversion.javahl.types.Revision;

import java.util.List;
import java.io.InputStream;

/**
 * Encapsulates utility functions for properties provided by libsvn_wc.
 * @since 1.9
 */
public class PropLib
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    /** @see SVNUtil#canonicalizeSvnProperty */
    public byte[] canonicalizeNodeProperty(String name, byte[] value,
                                           String path, NodeKind kind,
                                           String mimeType,
                                           InputStream fileContents)
        throws ClientException
    {
        if (!name.startsWith("svn:"))
            throw new IllegalArgumentException("Property name: " + name);
        return checkNodeProp(name, value, path, kind, mimeType, fileContents,
                             (kind != NodeKind.file || mimeType == null));
    }

    private native byte[] checkNodeProp(String name, byte[] value,
                                        String path, NodeKind kind,
                                        String mimeType,
                                        InputStream fileContents,
                                        boolean skipSomeChecks)
        throws ClientException;


    /** @see SVNUtil.parseExternals */
    public native List<ExternalItem> parseExternals(byte[] description,
                                                    String parentDirectory,
                                                    boolean canonicalizeUrl)
        throws ClientException;

    /** @see SVNUtil#unparseExternals */
    public native byte[] unparseExternals(List<ExternalItem> items,
                                          String parentDirectory,
                                          boolean old_format)
        throws SubversionException;


    /** @see SVNUtil#resolveExternalsUrl */
    public native String resolveExternalsUrl(ExternalItem external,
                                             String reposRootUrl,
                                             String parentDirUrl)
        throws ClientException;
}
