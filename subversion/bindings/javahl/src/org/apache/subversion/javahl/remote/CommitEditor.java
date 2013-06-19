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
import org.apache.subversion.javahl.JNIObject;
import org.apache.subversion.javahl.ClientException;

import java.io.InputStream;
import java.util.List;
import java.util.Map;

/**
 * Implementation of ISVNEditor that drives commits.
 * @since 1.9
 */
public class CommitEditor extends JNIObject implements ISVNEditor
{
    public void dispose() {/* TODO */}

    public void addDirectory(String relativePath,
                             Iterable<String> children,
                             Map<String, byte[]> properties,
                             long replacesRevision)
            throws ClientException
    {
        notimplemented("addDirectory");
    }

    public void addFile(String relativePath,
                        Checksum checksum,
                        InputStream contents,
                        Map<String, byte[]> properties,
                        long replacesRevision)
            throws ClientException
    {
        notimplemented("addFile");
    }

    public void addSymlink(String relativePath,
                           String target,
                           Map<String, byte[]> properties,
                           long replacesRevision)
            throws ClientException
    {
        notimplemented("addSymlink");
    }

    public void addAbsent(String relativePath,
                          NodeKind kind,
                          long replacesRevision)
            throws ClientException
    {
        notimplemented("addAbsent");
    }

    public void alterDirectory(String relativePath,
                               long revision,
                               Iterable<String> children,
                               Map<String, byte[]> properties)
            throws ClientException
    {
        notimplemented("alterDirectory");
    }

    public void alterFile(String relativePath,
                          long revision,
                          Checksum checksum,
                          InputStream contents,
                          Map<String, byte[]> properties)
            throws ClientException
    {
        notimplemented("alterFile");
    }

    public void alterSymlink(String relativePath,
                             long revision,
                             String target,
                             Map<String, byte[]> properties)
            throws ClientException
    {
        notimplemented("alterSymlink");
    }

    public void delete(String relativePath,
                       long revision)
            throws ClientException
    {
        notimplemented("delete");
    }

    public void copy(String sourceRelativePath,
                     long sourceRevision,
                     String destinationRelativePath,
                     long replacesRevision)
            throws ClientException
    {
        notimplemented("copy");
    }

    public void move(String sourceRelativePath,
                     long sourceRevision,
                     String destinationRelativePath,
                     long replacesRevision)
            throws ClientException
    {
        notimplemented("move");
    }

    public void rotate(List<RotatePair> elements) throws ClientException
    {
        notimplemented("rotate");
    }

    public void complete() throws ClientException
    {
        notimplemented("complete");
    }

    public void abort() throws ClientException
    {
        notimplemented("abort");
    }

    /**
     * This factory method called from RemoteSession.getCommitEditor.
     */
    static final CommitEditor createInstance(RemoteSession owner)
            throws ClientException
    {
        // FIXME: temporary implementation
        return new CommitEditor(0L);
    }

    /**
     * This constructor is called from JNI to get an instance.
     */
    protected CommitEditor(long cppAddr)
    {
        super(cppAddr);
    }

    private void notimplemented(String name)
    {
        throw new RuntimeException("Not implemented: " + name);
    }
}
