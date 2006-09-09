WINDOWS INSTALLER FOR SUBVERSION PYTHON BINDINGS
================================================

To build a Windows installer for the Subversion Python bindings:

 1. Build the Subversion Python bindings for Windows using Visual C++ 6.0.

 2. Update the version in setup.py to match your version of Subversion.
 
 3. Update the target_version in setup.py to match your version of Python.

 4. Create "libsvn" and "svn" directories. Copy the necessary Python (*.py)
    and DLL files (*.dll) into these directories.

 5. Run python setup.py bdist_wininst

That's it! Python will put the installer in the 'dist' directory.


List of required files (as of Subversion 1.4.0)
-----------------------------------------------

  libsvn:
    __init__.py
    _client.dll
    _core.dll
    _delta.dll
    _fs.dll
    _ra.dll
    _repos.dll
    _wc.dll
    client.py
    core.py
    delta.py
    fs.py
    libapr.dll
    libapriconv.dll
    libaprutil.dll
    libdb44.dll
    libeay32.dll
    libsvn_swig_py-1.dll
    ra.py
    repos.py
    ssleay32.dll
    wc.py

  svn:
    client.py
    core.py
    delta.py
    fs.py
    ra.py
    repos.py
    wc.py
    __init__.py 
