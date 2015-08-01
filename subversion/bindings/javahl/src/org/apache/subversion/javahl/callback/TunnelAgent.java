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

import java.nio.channels.ReadableByteChannel;
import java.nio.channels.WritableByteChannel;

/**
 * Callback interface for creating and managing tunnels for ra_svn
 * connections.
 *
 * Note that tunnel agent implementations should run in a separate
 * thread of control than the one that invokes an ISVNClient or
 * RemoteSession method that requires a tunnel, otherwise the method
 * will deadlock.
 *
 * @since 1.9
 */
public interface TunnelAgent
{
    /**
     * This callback method is called before a tunnel is created, to
     * determine whether to use this tunnel implementation, or revert
     * to the default (native) tunnel implementation.
     * @param name the name of the tunnel, as in
     *     <tt>svn+</tt><em>name</em><tt>://...</tt>
     * @return <code>false</code> to defer to the default implementation.
     */
    boolean checkTunnel(String name);

    /**
     * Callback interface returned from {@link #openTunnel()}.
     */
    public static interface CloseTunnelCallback
    {
        /**
         * This callback method is called when a tunnel needs to be closed
         * and the request and response streams detached from it.
         * <p>
         * <b>Note:</b> Errors on connection-close are not propagated
         * to the implementation, therefore this method cannot throw
         * any exceptions.
         */
        void closeTunnel();
    }

    /**
     * This callback method is called when a tunnel needs to be
     * created and the request and response streams attached to it.
     * @param request The request stream of the tunnel. The tunnel
     *     agent implementation will read requests from this channel
     *     and send them to the tunnel process.
     * @param response The request stream of the tunnel. The tunnel
     *     agent implementation will read requests from this channel
     *     and send them to the tunnel process.
     * @param name the name of the tunnel, as in
     *     <tt>svn+</tt><em>name</em><tt>://...</tt>
     * @param user the tunnel username
     * @param hostname the host part of the svn+tunnel:// URL
     * @param port the port part of the svn+tunnel:// URL
     *
     * @return an instance od {@link CloseTunnelCallback}, which will
     *         be invoked when the connection is closed, or
     *         <code>null</code>.
     *
     * @throws any exception will abort the connection
     */
    CloseTunnelCallback openTunnel(ReadableByteChannel request,
                                   WritableByteChannel response,
                                   String name, String user,
                                   String hostname, int port)
        throws Throwable;
}
