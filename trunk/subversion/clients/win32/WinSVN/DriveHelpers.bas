Attribute VB_Name = "PathHelpers"
'
' ====================================================================
' Copyright (c) 2000 CollabNet.  All rights reserved.
'
' This software is licensed as described in the file COPYING, which
' you should have received as part of this distribution.  The terms
' are also available at http://subversion.tigris.org/license-1.html.
' If newer versions of this license are posted there, you may use a
' newer version instead, at your option.
' ====================================================================
'
Option Explicit

Public g_fs As New Scripting.FileSystemObject

Private Const DRIVE_UNKNOWN = 0
Private Const DRIVE_NO_ROOT_DIR = 1
Private Const DRIVE_REMOVABLE = 2
Private Const DRIVE_FIXED = 3
Private Const DRIVE_REMOTE = 4
Private Const DRIVE_CDROM = 5
Private Const DRIVE_RAMDISK = 6

Private Declare Function GetDriveType Lib "kernel32" Alias "GetDriveTypeA" (ByVal nDrive As String) As Long
Private Declare Function GetLogicalDrives Lib "kernel32" () As Long

Private Const MAX_COMPUTERNAME_LENGTH = 15

Private Declare Function GetComputerName32 Lib "kernel32" Alias "GetComputerNameA" (ByVal lpBuffer As String, nSize As Long) As Long

Private m_lPower2(32) As Long

Private Sub Init()
   m_lPower2(0) = &H1&
   m_lPower2(1) = &H2&
   m_lPower2(2) = &H4&
   m_lPower2(3) = &H8&
   m_lPower2(4) = &H10&
   m_lPower2(5) = &H20&
   m_lPower2(6) = &H40&
   m_lPower2(7) = &H80&
   m_lPower2(8) = &H100&
   m_lPower2(9) = &H200&
   m_lPower2(10) = &H400&
   m_lPower2(11) = &H800&
   m_lPower2(12) = &H1000&
   m_lPower2(13) = &H2000&
   m_lPower2(14) = &H4000&
   m_lPower2(15) = &H8000&
   m_lPower2(16) = &H10000
   m_lPower2(17) = &H20000
   m_lPower2(18) = &H40000
   m_lPower2(19) = &H80000
   m_lPower2(20) = &H100000
   m_lPower2(21) = &H200000
   m_lPower2(22) = &H400000
   m_lPower2(23) = &H800000
   m_lPower2(24) = &H1000000
   m_lPower2(25) = &H2000000
   m_lPower2(26) = &H4000000
   m_lPower2(27) = &H8000000
   m_lPower2(28) = &H10000000
   m_lPower2(29) = &H20000000
   m_lPower2(30) = &H40000000
   m_lPower2(31) = &H80000000
End Sub

Public Function GetDriveList() As Collection
    Dim col As New Collection
    Dim lDriveBitmap As Long
    Dim i As Long
    Static fIsInit As Boolean
    
    If Not fIsInit Then
        Init
        fIsInit = True
    End If
    
    lDriveBitmap = GetLogicalDrives()
    
    For i = 0 To 31
        If m_lPower2(i) And lDriveBitmap Then
            col.Add Chr(Asc("A") + i) & ":"
        End If
    Next i
    
    Set GetDriveList = col
End Function

Public Function GetDriveTypeImageListKey(ByVal s As String) As String
    Dim lType As Long
    Dim sRet As String
    
    lType = GetDriveType(s)
    Select Case lType
    Case DRIVE_UNKNOWN:
        sRet = ""
    Case DRIVE_NO_ROOT_DIR:
        sRet = ""
    Case DRIVE_REMOVABLE:
        sRet = "REMOVABLE"
    Case DRIVE_FIXED:
        sRet = "HD"
    Case DRIVE_REMOTE:
        sRet = "NETDRIVE"
    Case DRIVE_CDROM:
        sRet = "CD"
    Case DRIVE_RAMDISK:
        sRet = "HD"
    End Select
    GetDriveTypeImageListKey = sRet
End Function

Public Function GetComputerName() As String

    Dim s As String
    Dim l As Long
    
    ' Allocate more than enough space.
    s = Space(MAX_COMPUTERNAME_LENGTH + 3)
    l = Len(s)
    GetComputerName32 s, l
    ' Trim results based on # of characters used.
    s = Mid(s, 1, l)
    
    GetComputerName = s
End Function

Public Function PathSkipRoot(s As String) As String
    PathSkipRoot = Mid(s, InStr(1, s, "\") + 1)
End Function

Public Function PathSkipTail(s As String) As String
    PathSkipTail = Left(s, InStrRev(s, "\") - 1)
End Function

Public Function PathGetTail(s As String) As String
    PathGetTail = Mid(s, InStrRev(s, "\") + 1)
End Function
