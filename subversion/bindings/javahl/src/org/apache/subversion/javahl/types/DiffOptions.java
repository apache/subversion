/*
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
 */

package org.apache.subversion.javahl.types;

/**
 * Options that control the output of {@link ISVNClient#diff}.
 * @since 1.8
 */
public class DiffOptions
{
    public enum Flag
    {
        /** Ignore all white space */
        IgnoreWhitespace  (0x01),

        /** Ignore changes in amount of white space */
        IgnoreSpaceChange (0x02),

        /** Ignore changes in EOL style */
        IgnoreEOLStyle    (0x04),

        /** Show C function name */
        ShowFunction      (0x08),

        /** Use git's extended diff format */
        GitFormat        (0x10),

        /** Show 'svn:mergeinfo' in human-readable form */
        FormatMergeinfo  (0x20);

        final int value;

        private Flag (int value)
        {
            this.value = value;
        }
    };

    /**
     * @param flagset any combination of the Flag enumeration values.
     */
    public DiffOptions(Flag... flagset)
    {
        int f = 0;
        for (Flag flag : flagset)
        {
            f |= flag.value;
        }
        this.flags = f;
    }

    /** @return whether IgnoreWhitespace is enabled */
    public boolean getIgnoreWhitespace()
    {
        return (0 != (flags & Flag.IgnoreWhitespace.value));
    }

    /** @return whether IgnoreSpaceChange is enabled */
    public boolean getIgnoreSpaceChange()
    {
        return (0 != (flags & Flag.IgnoreSpaceChange.value));
    }

    /** @return whether IgnoreEOLStyle is enabled */
    public boolean getIgnoreEOLStyle()
    {
        return (0 != (flags & Flag.IgnoreEOLStyle.value));
    }

    /** @return whether ShowFunction is enabled */
    public boolean getShowFunction()
    {
        return (0 != (flags & Flag.ShowFunction.value));
    }

    /** @return whether GitFormat is enabled */
    public boolean getGitFormat()
    {
        return (0 != (flags & Flag.GitFormat.value));
    }

    /** @return whether FormatMergeinfo is enabled */
    public boolean getFormatMergeinfo()
    {
        return (0 != (flags & Flag.FormatMergeinfo.value));
    }

    private final int flags;
}
