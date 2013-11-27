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
import java.io.OutputStream;

/**
 * Implementation class for {@link OutputStream} objects returned from
 * JavaHL methods.
 *
 * @since 1.9
 */
public class NativeOutputStream extends OutputStream
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
     * @see OutputStream.close()
     */
    @Override
    public native void close() throws IOException;

    /**
     * Writes a single byte to the underyling native stream.
     * @see OutputStream.write(int)
     */
    @Override
    public native void write(int b) throws IOException;

    /**
     * Writes <code>len</code> bytes at offset <code>off</code> from
     * <code>b</code> to the underyling native stream.
     * @see OutputStream.write(byte[],int,int)
     */
    @Override
    public native void write(byte[] b, int off, int len) throws IOException;


    private long cppAddr;

    private NativeOutputStream(long cppAddr)
    {
        this.cppAddr = cppAddr;
    }

    private long getCppAddr()
    {
        return cppAddr;
    }

    public native void finalize();
}
