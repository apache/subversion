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

import java.util.Date;
import java.util.Map;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Encapsulates utility functions for substitution and translation
 * provided by the <code>svn_subst</code> module of
 * <code>libsvn_subr</code>.
 * @since 1.9
 */
public class SubstLib
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    /**
     * @see SVNUtil.EOL_LF
     */
    public static final byte[] EOL_LF = new byte[]{ (byte)10 };

    /**
     * @see SVNUtil.EOL_CR
     */
    public static final byte[] EOL_CR = new byte[]{ (byte)13 };

    /**
     * @see SVNUtil.EOL_CRLF
     */
    public static final byte[] EOL_CRLF = new byte[]{ EOL_CR[0], EOL_LF[0] };

    /**
     * @see SVNUtil.buildKeywords
     */
    public native Map<String, byte[]> buildKeywords(byte[] keywordsValue,
                                                    long revision,
                                                    String url,
                                                    String reposRootUrl,
                                                    Date date,
                                                    String author)
        throws SubversionException, ClientException;

    /**
     * @see SVNUtil.translateStream
     */
    public native InputStream translateInputStream(
                                  InputStream source,
                                  byte[] eolMarker,
                                  boolean repairEol,
                                  Map<String, byte[]> keywords,
                                  boolean useKeywordsMap,
                                  boolean expandKeywords,
                                  byte[] keywordsValue,
                                  long revision,
                                  String url,
                                  String reposRootUrl,
                                  Date date,
                                  String author)
        throws SubversionException, ClientException;

    /**
     * @see SVNUtil.translateStream
     */
    public native OutputStream translateOutputStream(
                                   OutputStream destination,
                                   byte[] eolMarker,
                                   boolean repairEol,
                                   Map<String, byte[]> keywords,
                                   boolean useKeywordsMap,
                                   boolean expandKeywords,
                                   byte[] keywordsValue,
                                   long revision,
                                   String url,
                                   String reposRootUrl,
                                   Date date,
                                   String author)
        throws SubversionException, ClientException;
}
