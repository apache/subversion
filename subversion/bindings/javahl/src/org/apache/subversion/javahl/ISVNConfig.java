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

import org.apache.subversion.javahl.types.Tristate;

/**
 * Interface for manipulating the in-memory configuration info.
 * @since 1.9
 */
public interface ISVNConfig
{
    /**
     * Returns a reference to the "config" configuration category.
     */
    Category config();

    /*
     * The following constants are section and option names from the
     * "config" configuration file.
     */
    public static final String SECTION_AUTH          = "auth";
    public static final String PASSWORD_STORES           = "password-stores";
    public static final String KWALLET_WALLET            = "kwallet-wallet";
    public static final String KWALLET_SVN_APPLICATION_NAME_WITH_PID = "kwallet-svn-application-name-with-pid";
    public static final String SSL_CLIENT_CERT_FILE_PROMPT = "ssl-client-cert-file-prompt";

    public static final String SECTION_HELPERS       = "helpers";
    public static final String EDITOR_CMD                = "editor-cmd";
    public static final String DIFF_CMD                  = "diff-cmd";
    public static final String DIFF_EXTENSIONS           = "diff-extensions";
    public static final String DIFF3_CMD                 = "diff3-cmd";
    public static final String DIFF3_HAS_PROGRAM_ARG     = "diff3-has-program-arg";
    public static final String MERGE_TOOL_CMD            = "merge-tool-cmd";

    public static final String SECTION_MISCELLANY    = "miscellany";
    public static final String GLOBAL_IGNORES            = "global-ignores";
    public static final String LOG_ENCODING              = "log-encoding";
    public static final String USE_COMMIT_TIMES          = "use-commit-times";
    public static final String ENABLE_AUTO_PROPS         = "enable-auto-props";
    public static final String ENABLE_MAGIC_FILE         = "enable-magic-file";
    public static final String NO_UNLOCK                 = "no-unlock";
    public static final String MIMETYPES_FILE            = "mime-types-file";
    public static final String PRESERVED_CF_EXTS         = "preserved-conflict-file-exts";
    public static final String INTERACTIVE_CONFLICTS     = "interactive-conflicts";
    public static final String MEMORY_CACHE_SIZE         = "memory-cache-size";
    public static final String DIFF_IGNORE_CONTENT_TYPE  = "diff-ignore-content-type";

    public static final String SECTION_TUNNELS       = "tunnels";

    public static final String SECTION_AUTO_PROPS    = "auto-props";

    public static final String SECTION_WORKING_COPY  = "working-copy";
    public static final String SQLITE_EXCLUSIVE          = "exclusive-locking";
    public static final String SQLITE_EXCLUSIVE_CLIENTS  = "exclusive-locking-clients";
    public static final String SQLITE_BUSY_TIMEOUT       = "busy-timeout";

    /**
     * Returns a reference to the "servers" configuration category.
     */
    Category servers();

    /*
     * The following constants are section and option names from the
     * "servers" configuration file.
     */
    public static final String SECTION_GROUPS        = "groups";
    public static final String SECTION_GLOBAL        = "global";

    public static final String HTTP_PROXY_HOST           = "http-proxy-host";
    public static final String HTTP_PROXY_PORT           = "http-proxy-port";
    public static final String HTTP_PROXY_USERNAME       = "http-proxy-username";
    public static final String HTTP_PROXY_PASSWORD       = "http-proxy-password";
    public static final String HTTP_PROXY_EXCEPTIONS     = "http-proxy-exceptions";
    public static final String HTTP_TIMEOUT              = "http-timeout";
    public static final String HTTP_COMPRESSION          = "http-compression";
    public static final String NEON_DEBUG_MASK           = "neon-debug-mask";
    public static final String HTTP_AUTH_TYPES           = "http-auth-types";
    public static final String SSL_AUTHORITY_FILES       = "ssl-authority-files";
    public static final String SSL_TRUST_DEFAULT_CA      = "ssl-trust-default-ca";
    public static final String SSL_CLIENT_CERT_FILE      = "ssl-client-cert-file";
    public static final String SSL_CLIENT_CERT_PASSWORD  = "ssl-client-cert-password";
    public static final String SSL_PKCS11_PROVIDER       = "ssl-pkcs11-provider";
    public static final String HTTP_LIBRARY              = "http-library";
    public static final String STORE_PASSWORDS           = "store-passwords";
    public static final String STORE_PLAINTEXT_PASSWORDS = "store-plaintext-passwords";
    public static final String STORE_AUTH_CREDS          = "store-auth-creds";
    public static final String STORE_SSL_CLIENT_CERT_PP  = "store-ssl-client-cert-pp";
    public static final String STORE_SSL_CLIENT_CERT_PP_PLAINTEXT = "store-ssl-client-cert-pp-plaintext";
    public static final String USERNAME                  = "username";
    public static final String HTTP_BULK_UPDATES         = "http-bulk-updates";
    public static final String HTTP_MAX_CONNECTIONS      = "http-max-connections";
    public static final String HTTP_CHUNKED_REQUESTS     = "http-chunked-requests";
    public static final String SERF_LOG_COMPONENTS       = "serf-log-components";
    public static final String SERF_LOG_LEVEL            = "serf-log-level";

    /**
     * "true" value in configuration. One of the values returned by
     * {@link Category#getYesNoAsk}.
     */
    public static final String TRUE = "TRUE";

    /**
     * "false" value in configuration. One of the values returned by
     * {@link Category#getYesNoAsk}.
     */
    public static final String FALSE = "FALSE";

    /**
     * "ask" value in configuration. One of the values returned by
     * {@link Category#getYesNoAsk}.
     */
    public static final String ASK = "ASK";

    /**
     * Interface for reading and modifying configuration
     * categories. Returned by {@link #config()} and
     * {@link #servers()}.
     */
    public interface Category
    {
        /**
         * Returns the value of a configuration option.
         * @param section      The section name
         * @param option       The option name
         * @param defaultValue Return this if the option was not found.
         */
        String get(String section,
                   String option,
                   String defaultValue);

        /**
         * Returns the boolean value of a configuration option. The
         * recognized representations are 'true'/'false', 'yes'/'no',
         * 'on'/'off', '1'/'0'; case does not matter.
         * @throws ClientException if the value cannot be parsed.
         * @see #get(String,String,String)
         */
        boolean get(String section,
                    String option,
                    boolean defaultValue)
            throws ClientException;

        /**
         * Returns the long integer value of a configuration option.
         * @see #get(String,String,boolean)
         */
        long get(String section,
                 String option,
                 long defaultValue)
            throws ClientException;

        /**
         * Returns the {@link Tristate} value of a configuration option.
         * @param unknown The value used for {@link Tristate#Unknown}.
         * @see #get(String,String,boolean)
         */
        Tristate get(String section,
                     String option,
                     String unknown,
                     Tristate defaultValue)
            throws ClientException;

        /**
         * Check that the configuration option's value is true, false
         * or "ask". The boolean representations are the same as those
         * understood by {@link #get(String,String,boolean)}. If the
         * option is not found, the default value will be parsed
         * instead.
         * @return {@link ISVNConfig#TRUE}, {@link ISVNConfig#FALSE}
         *         or {@link ISVNConfig#ASK}
         * @throws ClientException if the either the value or the
         *         default cannot be parsed.
         */
        String getYesNoAsk(String section,
                           String option,
                           String defaultValue)
            throws ClientException;

        /**
         * Set the value of a configuration option.
         * @param section  The section name
         * @param option   The option name
         * @param value    The value to set the option to; passing
         *                 <code>null</code> will delete the option.
         */
        void set(String section,
                 String option,
                 String value);

        /**
         * Set the value of a configuration option to represent a boolean.
         * @see #set(String,String,String)
         */
        void set(String section,
                 String option,
                 boolean value);

        /**
         * Set the value of a configuration option to represent a long integer.
         * @see #set(String,String,String)
         */
        void set(String section,
                 String option,
                 long value);

        /**
         * @return the names of all the sections in the
         * configuration category.
         */
        Iterable<String> sections();

        /**
         * Call <code>handler</code> once for each option in the
         * configuration category.
         */
        void enumerate(String section, Enumerator handler);
    }

    /**
     * Interface for {@link Category#enumerate} callback handlers.
     */
    public interface Enumerator
    {
        void option(String name, String value);
    }
}
