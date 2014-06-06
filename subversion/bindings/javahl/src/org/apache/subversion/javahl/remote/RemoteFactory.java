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

import org.apache.subversion.javahl.callback.*;
import org.apache.subversion.javahl.types.*;

import org.apache.subversion.javahl.ISVNRemote;
import org.apache.subversion.javahl.NativeResources;
import org.apache.subversion.javahl.ClientException;
import org.apache.subversion.javahl.SubversionException;

import java.util.HashSet;


/**
 * Factory class for creating ISVNRemote instances.
 * @since 1.9
 */
public class RemoteFactory
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    /**
     * Default constructor.
     */
    public RemoteFactory() {}

    /**
     * Initializing constructor. Any or all of its arguments may be null.
     */
    public RemoteFactory(String configDirectory,
                         String username, String password,
                         AuthnCallback prompt,
                         ProgressCallback progress,
                         ConfigEvent configHandler,
                         TunnelAgent tunnelAgent)
    {
        setConfigDirectory(configDirectory);
        setUsername(username);
        setPassword(password);
        setPrompt(prompt);
        setProgressCallback(progress);
        setConfigEventHandler(configHandler);
        setTunnelAgent(tunnelAgent);
    }

    /**
     * Initializing constructor. Any or all of its arguments may be null.
     */
    @SuppressWarnings("deprecation")
    public RemoteFactory(String configDirectory,
                         String username, String password,
                         UserPasswordCallback prompt,
                         ProgressCallback progress,
                         ConfigEvent configHandler,
                         TunnelAgent tunnelAgent)
    {
        setConfigDirectory(configDirectory);
        setUsername(username);
        setPassword(password);
        setPrompt(prompt);
        setProgressCallback(progress);
        setConfigEventHandler(configHandler);
        setTunnelAgent(tunnelAgent);
    }

    /**
     * Sets the username used for authentication.
     * @param username The username; Set to the <code>null</code> to clear it.
     * @throws IllegalArgumentException If <code>username</code> is empty.
     * @see #password(String)
     */
    public void setUsername(String username)
    {
        if (username != null && username.isEmpty())
            throw new IllegalArgumentException("username must not be empty");
        this.username = username;
    }

    /**
     * Sets the password used for authentication.
     * @param password The passwordp Set <code>null</code> to clear it.
     * @throws IllegalArgumentException If <code>password</code> is empty.
     * @see #username(String)
     */
    public void setPassword(String password)
    {
        if (password != null && password.isEmpty())
            throw new IllegalArgumentException("password must not be empty");
        this.password = password;
    }

    /**
     * Register callback interface to supply username and password on demand.
     * This callback can also be used to provide theequivalent of the
     * <code>--no-auth-cache</code> and <code>--non-interactive</code>
     * arguments accepted by the command-line client.
     * @param prompt the callback interface
     */
    public void setPrompt(AuthnCallback prompt)
    {
        assert(this.deprecatedPrompt == null);
        this.prompt = prompt;
    }

    /**
     * Register callback interface to supply username and password on demand.
     * This callback can also be used to provide theequivalent of the
     * <code>--no-auth-cache</code> and <code>--non-interactive</code>
     * arguments accepted by the command-line client.
     * @param prompt the callback interface
     */
    @SuppressWarnings("deprecation")
    public void setPrompt(UserPasswordCallback prompt)
    {
        assert(this.prompt == null);
        this.deprecatedPrompt = prompt;
    }

    /**
     * Set the progress callback for new sessions.
     *
     * @param progress The progress callback.
     */
    public void setProgressCallback(ProgressCallback progress)
    {
        this.progress = progress;
    }

    /**
     * Set directory for the configuration information.
     */
    public void setConfigDirectory(String configDirectory)
    {
        this.configDirectory = configDirectory;
    }

    /**
     * Set an event handler that will be called every time the
     * configuration is loaded by the ISVNRemote objects created by
     * this factory.
     */
    public void setConfigEventHandler(ConfigEvent configHandler)
    {
        this.configHandler = configHandler;
    }

    /**
     * Set callbacks for ra_svn tunnel handling.
     */
    public void setTunnelAgent(TunnelAgent tunnelAgent)
    {
        this.tunnelAgent = tunnelAgent;
    }

    /**
     * Open a persistent session to a repository.
     * <p>
     * <b>Note:</b> The URL can point to a subtree of the repository.
     * <p>
     * <b>Note:</b> The session object inherits the progress callback,
     * configuration directory and authentication info.
     *
     * @param url The initial session root URL.
     * @throws RetryOpenSession If the session URL was redirected
     * @throws SubversionException If an URL redirect cycle was detected
     * @throws ClientException
     */
    public ISVNRemote openRemoteSession(String url)
            throws ClientException, SubversionException
    {
        return open(1, url, null, configDirectory,
                    username, password, prompt, deprecatedPrompt, progress,
                    configHandler, tunnelAgent);
    }

    /**
     * Open a persistent session to a repository.
     * <p>
     * <b>Note:</b> The URL can point to a subtree of the repository.
     * <p>
     * <b>Note:</b> The session object inherits the progress callback,
     * configuration directory and authentication info.
     *
     * @param url The initial session root URL.
     * @param retryAttempts The number of times to retry the operation
     *        if the given URL is redirected.
     * @throws IllegalArgumentException If <code>retryAttempts</code>
     *         is not positive
     * @throws RetryOpenSession If the session URL was redirected
     * @throws SubversionException If an URL redirect cycle was detected
     * @throws ClientException
     */
    public ISVNRemote openRemoteSession(String url, int retryAttempts)
            throws ClientException, SubversionException
    {
        if (retryAttempts <= 0)
            throw new IllegalArgumentException(
                "retryAttempts must be positive");
        return open(retryAttempts, url, null, configDirectory,
                    username, password, prompt, deprecatedPrompt, progress,
                    configHandler, tunnelAgent);
    }

    /**
     * Open a persistent session to a repository.
     * <p>
     * <b>Note:</b> The URL can point to a subtree of the repository.
     * <p>
     * <b>Note:</b> If the UUID does not match the repository,
     * this function fails.
     * <p>
     * <b>Note:</b> The session object inherits the progress callback,
     * configuration directory and authentication info.
     *
     * @param url The initial session root URL.
     * @param reposUUID The expected repository UUID; may not be null..
     * @throws IllegalArgumentException If <code>reposUUID</code> is null.
     * @throws RetryOpenSession If the session URL was redirected
     * @throws SubversionException If an URL redirect cycle was detected
     * @throws ClientException
     */
    public ISVNRemote openRemoteSession(String url, String reposUUID)
            throws ClientException, SubversionException
    {
        if (reposUUID == null)
            throw new IllegalArgumentException("reposUUID may not be null");
        return open(1, url, reposUUID, configDirectory,
                    username, password, prompt, deprecatedPrompt, progress,
                    configHandler, tunnelAgent);
    }

    /**
     * Open a persistent session to a repository.
     * <p>
     * <b>Note:</b> The URL can point to a subtree of the repository.
     * <p>
     * <b>Note:</b> If the UUID does not match the repository,
     * this function fails.
     * <p>
     * <b>Note:</b> The session object inherits the progress callback,
     * configuration directory and authentication info.
     *
     * @param url The initial session root URL.
     * @param reposUUID The expected repository UUID; may not be null..
     * @param retryAttempts The number of times to retry the operation
     *        if the given URL is redirected.
     * @throws IllegalArgumentException If <code>reposUUID</code> is null
     *         or <code>retryAttempts</code> is not positive
     * @throws RetryOpenSession If the session URL was redirected
     * @throws SubversionException If an URL redirect cycle was detected
     * @throws ClientException
     */
    public ISVNRemote openRemoteSession(String url, String reposUUID,
                                        int retryAttempts)
            throws ClientException, SubversionException
    {
        if (reposUUID == null)
            throw new IllegalArgumentException("reposUUID may not be null");
        if (retryAttempts <= 0)
            throw new IllegalArgumentException(
                "retryAttempts must be positive");
        return open(retryAttempts, url, reposUUID, configDirectory,
                    username, password, prompt, deprecatedPrompt, progress,
                    configHandler, tunnelAgent);
    }

    private String configDirectory;
    private String username;
    private String password;
    private AuthnCallback prompt;
    @SuppressWarnings("deprecation")
    private UserPasswordCallback deprecatedPrompt;
    private ProgressCallback progress;
    private ConfigEvent configHandler;
    private TunnelAgent tunnelAgent;

    /* Native factory implementation. */
    @SuppressWarnings("deprecation")
    private static native ISVNRemote open(int retryAttempts,
                                          String url, String reposUUID,
                                          String configDirectory,
                                          String username, String password,
                                          AuthnCallback prompt,
                                          UserPasswordCallback deprecatedPompt,
                                          ProgressCallback progress,
                                          ConfigEvent configHandler,
                                          TunnelAgent tunnelAgent)
            throws ClientException, SubversionException;
}
