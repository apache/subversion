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

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.WritableByteChannel;
import java.nio.channels.ClosedChannelException;

/* The following channel subclasses are used by the native
   implementation of the tunnel management code. */

class ResponseChannel
    extends TunnelChannel
    implements WritableByteChannel
{
    private ResponseChannel(long nativeChannel)
    {
        super(nativeChannel);
    }

    public int write(ByteBuffer src) throws IOException
    {
        long channel = this.nativeChannel.get();
        if (channel != 0)
            try {
                return nativeWrite(channel, src);
            } catch (IOException ex) {
                nativeChannel.set(0); // Close the channel
                throw ex;
            }
        throw new ClosedChannelException();
    }

    private static native int nativeWrite(long nativeChannel, ByteBuffer src)
        throws IOException;
}
