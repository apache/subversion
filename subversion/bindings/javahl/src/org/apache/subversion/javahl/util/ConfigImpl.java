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

import org.apache.subversion.javahl.ISVNConfig;
import org.apache.subversion.javahl.ClientException;
import org.apache.subversion.javahl.NativeResources;

import org.apache.subversion.javahl.types.*;
import org.apache.subversion.javahl.callback.*;


/**
 * Implementation of ISVNConfig.
 */
class ConfigImpl implements ISVNConfig
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    public Category config() { return this.configref; }
    public Category servers() { return this.serversref; }

    protected ConfigImpl(long context)
    {
        this.configref = new Category("config", context);
        this.serversref = new Category("servers", context);
    }
    protected Category configref;
    protected Category serversref;

    /** Called from JNI when the object is no longer live. */
    void dispose()
    {
        configref.dispose();
        configref = null;
        serversref.dispose();
        serversref = null;
    }

    static class Category implements ISVNConfig.Category
    {
        public String get(String section, String option, String defaultValue)
        {
            return get_str(category, context, section, option, defaultValue);
        }

        public boolean get(String section, String option, boolean defaultValue)
            throws ClientException
        {
            return get_bool(category, context, section, option, defaultValue);
        }

        public long get(String section, String option, long defaultValue)
            throws ClientException
        {
            return get_long(category, context, section, option, defaultValue);
        }

        public Tristate get(String section, String option,
                            String unknown, Tristate defaultValue)
            throws ClientException
        {
            return get_tri(category, context, section, option,
                           unknown, defaultValue);
        }

        public String getYesNoAsk(String section, String option,
                                  String defaultValue)
            throws ClientException
        {
            return get_yna(category, context, section, option, defaultValue);
        }

        public void set(String section, String option, String value)
        {
            set_str(category, context, section, option, value);
        }

        public void set(String section, String option, boolean value)
        {
            set_bool(category, context, section, option, value);
        }

        public void set(String section, String option, long value)
        {
            set_long(category, context, section, option, value);
        }

        public Iterable<String> sections()
        {
            return sections(category, context);
        }

        public void enumerate(String section, Enumerator handler)
        {
            enumerate(category, context, section, handler);
        }

        Category(String category, long context)
        {
            this.category = category;
            this.context = context;
        }
        protected String category;
        protected long context;

        /** Called when the object is no longer live. */
        void dispose()
        {
            category = null;
            context = 0;
        }

        private native String get_str(String category, long context,
                                      String secton, String option,
                                      String defaultValue);
        private native boolean get_bool(String category, long context,
                                        String secton, String option,
                                        boolean defaultValue)
            throws ClientException;
        private native long get_long(String category, long context,
                                     String secton, String option,
                                     long defaultValue)
            throws ClientException;
        private native Tristate get_tri(String category, long context,
                                        String secton, String option,
                                        String unknown, Tristate defaultValue)
            throws ClientException;
        private native String get_yna(String category, long context,
                                      String secton, String option,
                                      String defaultValue)
            throws ClientException;
        private native void set_str(String category, long context,
                                    String section, String option,
                                    String value);
        private native void set_bool(String category, long context,
                                     String section, String option,
                                     boolean value);
        private native void set_long(String category, long context,
                                     String section, String option,
                                     long value);
        private native Iterable<String> sections(String category,
                                                 long context);
        private native void enumerate(String category, long context,
                                      String section, Enumerator handler);

    }
}
