VERSION 1.0 CLASS
BEGIN
  MultiUse = -1  'True
  Persistable = 0  'NotPersistable
  DataBindingBehavior = 0  'vbNone
  DataSourceBehavior  = 0  'vbNone
  MTSTransactionMode  = 0  'NotAnMTSObject
END
Attribute VB_Name = "ExplorerViewHelpers"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = True
Attribute VB_PredeclaredId = False
Attribute VB_Exposed = False
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
Private WithEvents tvTreeView As TreeView
Private Const k_gComputer = 0
Private Const k_gDrive = 1
Private Const k_gFolder = 2
Private lRootType As Long
Private sRootFolder As String

Private Sub tvTreeView_Collapse(ByVal Node As MSComctlLib.Node)
    Dim oChild As Node
    Dim oNext As Node
    
    ' We can't very well go deleting our drive nodes,
    ' now can we?
    If Node.Key <> "Computer" Then
        ' Since this is a virtual tree, remove the nodes
        ' when we collapse.
        Set oChild = Node.Child
        While Not oChild Is Nothing
            Set oNext = oChild.Next
            ' This recursively removes all of oChild's
            ' children as well.
            tvTreeView.Nodes.Remove oChild.Index
            Set oChild = oNext
        Wend
        AddLazyNode Node
    End If
End Sub
Private Sub ExpandNodes(ByVal Node As MSComctlLib.Node, sPath As String)
    Dim oNode2 As Node
    Dim f As Folder
    Dim fldrs As Folders
    
    ' TODO: Handle GetFolder() failing, it can for removable drives.
    On Error Resume Next
    Set f = g_fs.GetFolder(sPath)
    If Err.Number <> 0 Then
        'TODO: display error message here
        On Error GoTo 0
        GoTo NextLabel
    End If
    On Error GoTo 0
    Set fldrs = f.SubFolders
    For Each f In fldrs
        If f.Attributes And Hidden Or _
           f.Attributes And System Then
           GoTo NextLabel
        End If
        If g_SVN_WC.check_wc(f.Path) Then
            Set oNode2 = tvTreeView.Nodes.Add(Node, tvwChild, , f.Name, "SVNCLOSED", "SVNOPEN")
            oNode2.Tag = "WC"
        Else
            ' Hide SVN meta-data directories
            If Node.Tag = "WC" And _
               f.Name = "SVN" Then
                GoTo NextLabel
            End If
            Set oNode2 = tvTreeView.Nodes.Add(Node, tvwChild, , f.Name, "CLOSED", "OPEN")
        End If
        AddLazyNode oNode2
NextLabel:
    Next
End Sub
Private Sub tvTreeView_Expand(ByVal Node As MSComctlLib.Node)
    Dim sPath As String
    
    ' Need to virtally expand the nodes here.
    If Node.Children = 1 Then
        If Node.Child.Tag = "LAZY" Then
            ' Remove the lazy node
            tvTreeView.Nodes.Remove Node.Child.Index
            sPath = GetNodePath(Node)
            ExpandNodes Node, sPath
        End If
    End If
End Sub

Private Sub tvTreeView_NodeClick(ByVal Node As MSComctlLib.Node)
    Dim sPath As String
    
    sPath = GetNodePath(Node)
    fMainForm.g_oLVH.PopulateSGrid sPath
End Sub
Private Sub LoadTreeView(sPath As String, iType As Long)
    Dim oNode As Node
    Dim item As Variant
    Dim col As Collection
    Dim sType As String
    Dim oNode2 As Node
    
    tvTreeView.Nodes.Clear
    lRootType = iType
    
    If iType = k_gComputer Then
    
        Set oNode = tvTreeView.Nodes.Add(, tvwFirst, "Computer", g_sComputerName, "COMPUTER")
        Set col = GetDriveList()
        
        For Each item In col
            sType = GetDriveTypeImageListKey(item)
            ' Worrying about working copy data on a CD
            ' just doesn't make sense...
            If Len(sType) > 0 And sType <> "CD" Then
                Set oNode2 = tvTreeView.Nodes.Add(oNode, tvwChild, item, item, sType)
                AddLazyNode oNode2
            End If
        Next
    Else
        ' Drive and Folder case
        ' Drive just needs different icon.
        Set oNode = tvTreeView.Nodes.Add(, tvwFirst, sPath)
        If iType = k_gDrive Then
            oNode.Image = GetDriveTypeImageListKey(sPath)
            If g_SVN_WC.check_wc(sPath) Then
                oNode.Tag = "WC"
            End If
            ' When listing subfolders of drives, initial \'s are
            ' required. Otherwise, it gets treated as the CWD
            ' for that drive.
            sPath = sPath & "\"
            oNode.Text = UCase(sPath)
        Else
            If g_SVN_WC.check_wc(sPath) Then
                oNode.Image = "SVNCLOSED"
                oNode.SelectedImage = "SVNOPEN"
                oNode.Tag = "WC"
            Else
                oNode.Image = "CLOSED"
                oNode.SelectedImage = "OPEN"
            End If
            oNode.Text = PathHelpers.PathGetTail(sPath)
            sRootFolder = sPath
        End If
        ExpandNodes oNode, sPath
    End If
    fMainForm.g_oCDH.Update sPath, iType
End Sub
Public Sub InitTreeView()
    LoadTreeView "", k_gComputer
End Sub

Public Sub AddLazyNode(oNode As Node)
    Dim oNode2 As Node
    
    Set oNode2 = tvTreeView.Nodes.Add(oNode, tvwChild)
    oNode2.Tag = "LAZY"
End Sub
Private Function GetNodePath(Node As MSComctlLib.Node) As String
    If lRootType = k_gComputer Then
        GetNodePath = StripRootNode(Node.FullPath)
    ElseIf lRootType = k_gDrive Then
        GetNodePath = Node.FullPath
    Else
        GetNodePath = sRootFolder & "\" & StripRootNode(Node.FullPath)
    End If
End Function
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

Private Sub Class_Initialize()
    Set tvTreeView = fMainForm.tvTreeView
    lRootType = k_gComputer
End Sub
