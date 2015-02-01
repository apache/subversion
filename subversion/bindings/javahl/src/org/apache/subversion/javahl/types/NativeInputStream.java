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

import org.apache.subversion.javahl.NativeResources;

import java.io.IOException;
import java.io.InputStream;

/**
 * Implementation class for {@link InputStream} objects returned from
 * JavaHL methods.
 *
 * @since 1.9
 */
public class NativeInputStream extends InputStream
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    /**
     * Flushes buffers, closes the underlying native stream, and
     * releases the native object.
     * @see InputStream.close()
     */
    @Override
    public native void close() throws IOException;

    /**
     * @see InputStream.markSupported()
     */
    @Override
    public native boolean markSupported();

    /**
     * @see InputStream.mark(int)
     */
    @Override
    public native void mark(int readlimit);

    /**
     * @see InputStream.reset()
     */
    @Override
    public native void reset() throws IOException;

    /**
     * Reads a single byte from the underyling native stream.
     * @see InputStream.read()
     */
    @Override
    public native int read() throws IOException;

    /**
     * Reads <code>len</code> bytes to offset <code>off</code> in
     * <code>b</code> from the underyling native stream.
     * @see InputStream.read(byte[],int,int)
     */
    @Override
    public native int read(byte[] b, int off, int len) throws IOException;

    /**
     * @see InputStream.skip(long)
     */
    @Override
    public native long skip(long count) throws IOException;


    private long cppAddr;

    private NativeInputStream(long cppAddr)
    {
        this.cppAddr = cppAddr;
    }

    private long getCppAddr()
    {
        return cppAddr;
    }

    public native void finalize();
}
