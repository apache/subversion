Attribute VB_Name = "ExplorerViewHelpers"
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

Public Sub InitTreeView()
    Dim oNode As Node
    Dim col As Collection
    Dim item As Variant
    Dim sType As String
    Dim oNode2 As Node
    
    Set oNode = fMainForm.tvTreeView.Nodes.Add(, tvwFirst, "Computer", g_sComputerName, "COMPUTER")
    Set col = GetDriveList()
    
    For Each item In col
        sType = GetDriveTypeImageListKey(item)
        ' Worrying about working copy data on a CD
        ' just doesn't make sense...
        If Len(sType) > 0 And sType <> "CD" Then
            Set oNode2 = fMainForm.tvTreeView.Nodes.Add(oNode, tvwChild, item, item, sType)
            AddLazyNode oNode2
        End If
    Next
End Sub

Public Sub AddLazyNode(oNode As Node)
    Dim oNode2 As Node
    
    Set oNode2 = fMainForm.tvTreeView.Nodes.Add(oNode, tvwChild)
    oNode2.Tag = "LAZY"
End Sub

Public Function StripRootNode(s As String) As String
    Dim sPath As String
    
    ' Strip off the computer name
    sPath = PathSkipRoot(s)
    ' Ensure \ is at the end.
    If Right(sPath, 1) <> "\" Then
        sPath = sPath & "\"
    End If
    
    StripRootNode = sPath
End Function
