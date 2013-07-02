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
import java.io.InputStream;

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
        throw new RuntimeException("Not implemented: StatusEditor.addAbsent");
    }

    public void alterDirectory(String relativePath,
                               long revision,
                               Iterable<String> children,
                               Map<String, byte[]> properties)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.alterDirectory");
        checkState();
        receiver.modifiedDirectory(relativePath, (children != null),
                                   props_changed(properties));
    }

    public void alterFile(String relativePath,
                          long revision,
                          Checksum checksum,
                          InputStream contents,
                          Map<String, byte[]> properties)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.alterFile");
        checkState();
        receiver.modifiedFile(relativePath,
                              (checksum != null && contents != null),
                              props_changed(properties));
    }

    public void alterSymlink(String relativePath,
                             long revision,
                             String target,
                             Map<String, byte[]> properties)
    {
        //DEBUG:System.err.println("  [J] StatusEditor.alterSymlink");
        checkState();
        receiver.modifiedSymlink(relativePath, (target != null),
                                 props_changed(properties));
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
