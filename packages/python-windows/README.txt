WINDOWS INSTALLER FOR SUBVERSION PYTHON BINDINGS
================================================

To build a Windows installer for the Subversion Python bindings:

 1. Build the Subversion Python bindings for Windows.

 2. Update the version in setup.py to match your version of Subversion.
 
 3. Create "libsvn" and "svn" directories. Copy the necessary Python (*.py)
    and DLL (*.dll, *.pyd) files into these directories.

 4. Run python setup.py bdist_wininst --target-version=2.6 to build an
    installer for Python 2.6. If you build your bindings for a different
    version of Python, adjust that command-line appropriately.

That's it! Python will put the installer in the 'dist' directory.

The list of required files (as of Subversion 1.7.0) follows. It assumes you've
built Subversion with all possible dependencies; ignore the libraries you
didn't link to, if any.

  libsvn:
    __init__.py
    _client.pyd
    _core.pyd
    _delta.pyd
    _diff.pyd
    _fs.pyd
    _ra.pyd
    _repos.pyd
    _wc.pyd
    client.py
    core.py
    delta.py
    diff.py
    fs.py
    intl3_svn.dll
    libapr-1.dll
    libapriconv-1.dll
    libaprutil-1.dll
    libdb44.dll
    libeay32.dll
    libsasl.dll
    libsvn_client-1.dll
    libsvn_delta-1.dll
    libsvn_diff-1.dll
    libsvn_fs-1.dll
    libsvn_ra-1.dll
    libsvn_repos-1.dll
    libsvn_subr-1.dll
    libsvn_swig_py-1.dll
    libsvn_wc-1.dll
    ra.py
    repos.py
    ssleay32.dll
    wc.py

  svn:
    __init__.py 
    client.py
    core.py
    delta.py
    diff.py
    fs.py
    ra.py
    repos.py
    wc.py
