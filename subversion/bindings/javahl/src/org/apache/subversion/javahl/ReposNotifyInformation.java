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

package org.apache.subversion.javahl;

import java.util.Map;
import java.util.EventObject;

/**
 * The event passed to the {@link Notify2#onNotify(NotifyInformation)}
 * API to notify {@link SVNClientInterface} of relevant events.
 *
 * @since 1.2
 */
public class ReposNotifyInformation extends EventObject
{
    // Update the serialVersionUID when there is a incompatible change
    // made to this class.  See any of the following, depending upon
    // the Java release.
    // http://java.sun.com/j2se/1.3/docs/guide/serialization/spec/version.doc7.html
    // http://java.sun.com/j2se/1.4/pdf/serial-spec.pdf
    // http://java.sun.com/j2se/1.5.0/docs/guide/serialization/spec/version.html#6678
    // http://java.sun.com/javase/6/docs/platform/serialization/spec/version.html#6678
    private static final long serialVersionUID = 1L;

    /**
     * The {@link NotifyAction} which triggered this event.
     */
    private Action action;

    /**
     * The revision of the item.
     */
    private long revision;

    /**
     * The warning text.
     */
    private String warning;

    /**
     * This constructor is to be used by the native code.
     *
     * @param action The {@link NotifyAction} which triggered this event.
     * @param revision potentially the revision.
     */
    public ReposNotifyInformation(Action action, long revision, String warning)
    {
        super(action);
        this.action = action;
        this.revision = revision;
        this.warning = warning;
    }

    /**
     * @return The {@link NotifyAction} which triggered this event.
     */
    public Action getAction()
    {
        return action;
    }

    /**
     * @return The revision for the item.
     */
    public long getRevision()
    {
        return revision;
    }

    /**
     * The type of action triggering the notification
     */
    public enum Action
    {
        /** A warning message is waiting. */
        warning,

        /** A revision has finished being dumped. */
        dump_rev_end,

        /** A revision has finished being verified. */
        verify_rev_end;
    }
}
