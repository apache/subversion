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
import java.nio.channels.Channel;
import java.util.concurrent.atomic.AtomicLong;

/* The following channel subclasses are used by the native
   implementation of the tunnel management code. */

abstract class TunnelChannel implements Channel
{
    protected TunnelChannel(long nativeChannel)
    {
        this.nativeChannel = new AtomicLong(nativeChannel);
    }

    public boolean isOpen()
    {
        return (nativeChannel.get() != 0);
    }

    public void close() throws IOException
    {
        long channel = nativeChannel.getAndSet(0);
        if (channel != 0)
            nativeClose(channel);
    }

    /**
     * Wait for current read/write to complete, then close() channel.
     * Compared to close(), it has the following differences:
     * <ul>
     *   <li>
     *     Prevents race condition where read/write could use incorrect file :
     *     <ol>
     *       <li>Writer thread extracts OS file descriptor from nativeChannel.</li>
     *       <li>Writer thread calls OS API to write to file, passing file descriptor.</li>
     *       <li>Writer thread is interrupted.</li>
     *       <li>Closer thread closes OS file descriptor. The file descriptor number is now free.</li>
     *       <li>Unrelated thread opens a new file. OS reuses the old file descriptor (currently free).</li>
     *       <li>Writer thread resumes inside OS API to write to file.</li>
     *       <li>Writer thread writes to unrelated file, corrupting it with unexpected data.</li>
     *     </ol>
     *   </li>
     *   <li>
     *     It can no longer cancel a read/write operation already in progress.
     *     The native implementation closes the other end of the pipe, breaking the pipe,
     *     which prevents the risk of never-completing read/write.
     *   </li>
     * <ul/>
     *
     * @throws IOException
     */
    public void syncClose() throws IOException
    {
        synchronized (nativeChannel)
        {
            close();
        }
    }

    private native static void nativeClose(long nativeChannel)
        throws IOException;

    protected AtomicLong nativeChannel;
}
