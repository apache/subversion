@ECHO OFF
REM #######################################################################
REM FILE       mk_htmlhelp.bat
REM PURPOSE    Making MS HTML help file out of the svn-book sources
REM ====================================================================
REM  Licensed to the Subversion Corporation (SVN Corp.) under one
REM  or more contributor license agreements.  See the NOTICE file
REM  distributed with this work for additional information
REM  regarding copyright ownership.  The SVN Corp. licenses this file
REM  to you under the Apache License, Version 2.0 (the
REM  "License"); you may not use this file except in compliance
REM  with the License.  You may obtain a copy of the License at
REM
REM    http://www.apache.org/licenses/LICENSE-2.0
REM
REM  Unless required by applicable law or agreed to in writing,
REM  software distributed under the License is distributed on an
REM  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
REM  KIND, either express or implied.  See the License for the
REM  specific language governing permissions and limitations
REM  under the License.
REM ====================================================================

SET BOOKNAME=svn-book
SET HHC=<%tv_path_hhc%>
SET BOOKDEST=<%tv_bookdest%>

mkdir images
copy ..\en\book\images\*.* images

SET XLSPARAMS=--param suppress.navigation 0 --param htmlhelp.hhc.binary 1
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.show.advanced.search 1
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.chm '%BOOKNAME%.chm'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhp '%BOOKNAME%.hhp'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhc '%BOOKNAME%.hhc'
SET XLSPARAMS=%XLSPARAMS% --param html.stylesheet.type 'text/css'
SET XLSPARAMS=%XLSPARAMS% --param html.stylesheet 'svn-doc.css'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhp.tail 'svn_bck.png'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.use.hhk 0
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhc.show.root 0
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.autolabel 1

xsltproc %XLSPARAMS% ..\tools\xsl\htmlhelp\htmlhelp.xsl ..\en\book\book.xml

%HHC% %BOOKNAME%.hhp
move %BOOKNAME%.chm %BOOKDEST%

del /Q %BOOKNAME%.*
del /Q *.html
rmdir /S /Q images
