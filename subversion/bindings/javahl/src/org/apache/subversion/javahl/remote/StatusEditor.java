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

package org.apache.subversion.javahl.remote;

import org.apache.subversion.javahl.types.*;
import org.apache.subversion.javahl.callback.*;

import org.apache.subversion.javahl.ISVNEditor;

import java.util.Map;
import java.util.GregorianCalendar;
import java.util.Locale;
import java.util.SimpleTimeZone;
import java.io.InputStream;
import java.io.IOException;
import java.nio.charset.Charset;

/**
 * Package-private editor implementation that converts an editor drive
 * to {@link RemoteStatus} callbacks.
 * @since 1.9
 */
class StatusEditor implements ISVNEditor
{
    StatusEditor(RemoteStatus receiver)
    {
        this.receiver = receiver;
    }
    protected RemoteStatus receiver = null;

    protected void checkState()
    {
        if (receiver == null)
            throw new IllegalStateException("Status editor is not active");
    }

    public void dispose()
    {
        //DEBUG:System.err.println("  [J] StatusEditor.dispose");
        if (this.receiver != null)
            abort();
    }

    public void addDirectory(String relativePath,
                             Iterable<String> children,
                             Map<String, byte[]> properties,
                             long replacesRevision)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.addDirectory");
        checkState();
        receiver.addedDirectory(relativePath);
    }

    public void addFile(String relativePath,
                        Checksum checksum,
                        InputStream contents,
                        Map<String, byte[]> properties,
                        long replacesRevision)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.addFile");
        if (contents != null) {
            try {
                contents.close();
            } catch (IOException ex) {
                throw new RuntimeException(ex);
            }
        }

        checkState();
        receiver.addedFile(relativePath);
    }

    public void addSymlink(String relativePath,
                           String target,
                           Map<String, byte[]> properties,
                           long replacesRevision)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.addSymlink");
        checkState();
        receiver.addedSymlink(relativePath);
    }

    public void addAbsent(String relativePath,
                          NodeKind kind,
                          long replacesRevision)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.addAbsent");
        checkState();
        // ignore this callback, as svn status -u does
    }

    public void alterDirectory(String relativePath,
                               long revision,
                               Iterable<String> children,
                               Map<String, byte[]> properties)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.alterDirectory");
        checkState();
        receiver.modifiedDirectory(relativePath, (children != null),
                                   props_changed(properties),
                                   make_entry(properties));
    }

    public void alterFile(String relativePath,
                          long revision,
                          Checksum checksum,
                          InputStream contents,
                          Map<String, byte[]> properties)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.alterFile");
        if (contents != null) {
            try {
                contents.close();
            } catch (IOException ex) {
                throw new RuntimeException(ex);
            }
        }

        checkState();
        receiver.modifiedFile(relativePath,
                              (checksum != null && contents != null),
                              props_changed(properties),
                              make_entry(properties));
    }

    public void alterSymlink(String relativePath,
                             long revision,
                             String target,
                             Map<String, byte[]> properties)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.alterSymlink");
        checkState();
        receiver.modifiedSymlink(relativePath, (target != null),
                                 props_changed(properties),
                                 make_entry(properties));
    }

    public void delete(String relativePath, long revision)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.delete");
        checkState();
        receiver.deleted(relativePath);
    }

    public void copy(String sourceRelativePath,
                     long sourceRevision,
                     String destinationRelativePath,
                     long replacesRevision)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.copy");
        checkState();
        throw new RuntimeException("Not implemented: StatusEditor.copy");
    }

    public void move(String sourceRelativePath,
                     long sourceRevision,
                     String destinationRelativePath,
                     long replacesRevision)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.move");
        checkState();
        throw new RuntimeException("Not implemented: StatusEditor.move");
    }

    public void complete()
    {
        //DEBUG:System.err.println("  [J] StatusEditor.complete");
        abort();
    }

    public void abort()
    {
        //DEBUG:System.err.println("  [J] StatusEditor.abort");
        checkState();
        receiver = null;
    }

    /*
     * Construct a RemoteStatus.Entry record from the given properties.
     */
    private static final Charset UTF8 = Charset.forName("UTF-8");
    private static final SimpleTimeZone UTC =
        new SimpleTimeZone(SimpleTimeZone.UTC_TIME, "UTC");
    private static final String entryprop_uuid = "svn:entry:uuid";
    private static final String entryprop_author = "svn:entry:last-author";
    private static final String entryprop_revision = "svn:entry:committed-rev";
    private static final String entryprop_timestamp = "svn:entry:committed-date";
    private final GregorianCalendar entry_calendar =
        new GregorianCalendar(UTC, Locale.ROOT);

    // FIXME: Room for improvement here. There are likely to be a lot
    // of duplicate entries and we should be able to avoid parsing the
    // duplicates all over again. Need a map <raw data> -> Entry to
    // just look up the entries instead of parsing them.
    private final RemoteStatus.Entry make_entry(Map<String, byte[]> properties)
    {
        final byte[] raw_uuid = properties.get(entryprop_uuid);
        final byte[] raw_author = properties.get(entryprop_author);
        final byte[] raw_revision = properties.get(entryprop_revision);
        final byte[] raw_timestamp = properties.get(entryprop_timestamp);

        long parsed_timestamp = -1;
        if (raw_timestamp != null)
        {
            // Parse: 2013-07-04T23:17:59.128366Z
            final String isodate = new String(raw_timestamp, UTF8);

            final int year = Integer.valueOf(isodate.substring(0,4), 10);
            final int month = Integer.valueOf(isodate.substring(5,7), 10);
            final int day = Integer.valueOf(isodate.substring(8,10), 10);
            final int hour = Integer.valueOf(isodate.substring(11,13), 10);
            final int minute = Integer.valueOf(isodate.substring(14,16), 10);
            final int second = Integer.valueOf(isodate.substring(17,19), 10);
            final int micro = Integer.valueOf(isodate.substring(20,26), 10);
            entry_calendar.set(year, month, day, hour, minute, second);

             // Use integer rounding to add milliseconds
            parsed_timestamp =
                (1000 * entry_calendar.getTimeInMillis() + micro + 500) / 1000;
        }

        return new RemoteStatus.Entry(
            (raw_uuid == null ? null : new String(raw_uuid, UTF8)),
            (raw_author == null ? null : new String(raw_author, UTF8)),
            (raw_revision == null ? Revision.SVN_INVALID_REVNUM
             : Long.valueOf(new String(raw_revision, UTF8), 10)),
            parsed_timestamp);
    }

    /*
     * Filter entry props from the incoming properties
     */
    private static final String wcprop_prefix = "svn:wc:";
    private static final String entryprop_prefix = "svn:entry:";
    private static final boolean props_changed(Map<String, byte[]> properties)
    {
        if (properties != null)
            for (String name : properties.keySet())
                if (!name.startsWith(wcprop_prefix)
                    && !name.startsWith(entryprop_prefix))
                    return true;
        return false;
    }
}
