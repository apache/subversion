Build Instructions
==================



External Tools and Utilities:
-----------------------------

* ActiveState ActivePerl (tested with v5.88 build 820)

	Modules:

	* Win32::Guidgen
	* Win32::GUI
	* XML::XPath
	* Digest::MD5
	* Digest::SHA1

* SharpDevelop (tested with v3.1)

* KDiff3 (or any diff program that can diff subfolders)

* gnupg



Documentation Build Tools
-------------------------

* zlib (tested with v1.2.3.0)

* LibIconv for Windows (tested with v1.9.0.0)

* Libxslt for Windows

* LibXML2 for Windows

* DocBook XSL Stylesheets (tested with v1.72.0)



Paths
-----

1. On the root of a drive, create a folder called SubversionBuildTools.  All subsequent folders should be created inside this folder

2. Create a folder called SubversionTrunk and checkout the latest Subversion source to this folder.

3. Create a folder called svnbook and in this folder, checkout the tagged version of the source relating to this Subversion release - or alternatively, checkout the trunk if there is no tag for this release.

4. Create a folder called work.  Download the previous version and current win32 binaries and extract to this folder.
	4a) Rename the current version folder to 'svn-win32-ap22x'.
	4b) Inside the folder structure for both versions, move the main structure down to the first folder level
	    (the binaries normally will extract two levels deep, we want the second level moved back to the first)
	    When setup correctly, the path to the bin folder will be \SubversionBuildEnvironment\work\svn-win32-ap22x\bin

5. Create a folder called InPath and copy selected binaries from the downloaded documentation build tools so that the
   folder contains the following binaries:

	iconv.dll
	iconv.exe
	libexslt.dll
	libxml2.dll
	libxslt.dll
	xmlcatalog.exe
	xmllint.exe
	xsltproc.exe
	zlib1.dll

6. Add the InPath folder location to the Path environment variable.

7. Extract the DocBook XSL Stylesheets to svnbook/tools/xsl.

8. gnupg should usually be placed in c:\gnupg.  Ensure it is added to the Path environment variable.



Prepare for Build
-----------------

1. Load the BuildSubversion solution into SharpDevelop.

2. Perform a diff between the new and old Subversion binaries including subfolders.  Note any added or removed files.

3. For any added or removed files, edit and save the wxs files in SharpDevelop (WiX knowledge required).



Build the Subversion Release
----------------------------

1. In SharpDevelop, check that the build configuration is set to 'Release'.

2. Build the solution.

3. Enter 

If the build fails from any pre-build events, try running prepare-distro.bat from the Tools folder on a command prompt
to ascertain whether the perl scripts are producing errors.