VERSION 1.0 CLASS
BEGIN
  MultiUse = -1  'True
  Persistable = 0  'NotPersistable
  DataBindingBehavior = 0  'vbNone
  DataSourceBehavior  = 0  'vbNone
  MTSTransactionMode  = 0  'NotAnMTSObject
END
Attribute VB_Name = "grdHelper"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = True
Attribute VB_PredeclaredId = False
Attribute VB_Exposed = False
Attribute VB_Description = "ListView helper objects"
Attribute VB_Ext_KEY = "SavedWithClassBuilder6" ,"Yes"
Attribute VB_Ext_KEY = "Top_Level" ,"Yes"
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

Private WithEvents WC As SVNWorkingCopy
Attribute WC.VB_VarHelpID = -1
Private grdFiles As SGrid
Private Sub InitSGrid()
    With grdFiles
        ' Turn drawaing off for optimization.
        .Redraw = False
        
        .ImageList = fMainForm.imlSGrid.hIml
        
        ' Allow multiple rows to be selected.
        .MultiSelect = True
        ' Make rows selectable as opposed to individual cells.
        .RowMode = True
        
        ' Have a DevStudio 6 looking grid view.
        .HeaderFlat = True
        .HeaderButtons = True
        
        .DisableIcons = False
        .HighlightSelectedIcons = False
        .DrawFocusRectangle = False
        
        .AddColumn "file", "Name", ecgHdrTextALignLeft, , , , , , , , , CCLSortString
        .AddColumn "size", "Size", ecgHdrTextALignLeft
        .AddColumn "date", "Last Modified", ecgHdrTextALignLeft
        .SetHeaders
        
        ' This enables moving the selection based on subsequent keystrokes
        ' in the SGrid.
        .KeySearchColumn = .ColumnIndex("file")
        
        ' Turn drawing back on
        .Redraw = True
    End With
End Sub
Private Sub AssignIcons(iRow As Long, status As SVNCOMLib.SVNStatus)
    With status
        If .text_status = eWCStatus_Modified Or _
           .prop_status = eWCStatus_Modified Then
            grdFiles.Cell(iRow, grdFiles.ColumnIndex("file")).IconIndex = 6
        ElseIf .text_status = eWCStatus_Deleted Or _
               .prop_status = eWCStatus_Deleted Then
            grdFiles.Cell(iRow, grdFiles.ColumnIndex("file")).IconIndex = 8
        ElseIf .text_status = eWCStatus_Conflicted Or _
               .prop_status = eWCStatus_Conflicted Then
            grdFiles.Cell(iRow, grdFiles.ColumnIndex("file")).IconIndex = 2
        ElseIf .text_status = eWCStatus_Merged Or _
               .prop_status = eWCStatus_Merged Then
                grdFiles.Cell(iRow, grdFiles.ColumnIndex("file")).IconIndex = 6
        ElseIf .text_status = eWCStatus_Added Or _
               .prop_status = eWCStatus_Added Then
            grdFiles.Cell(iRow, grdFiles.ColumnIndex("file")).IconIndex = 6
        ElseIf .text_status = eWCStatus_NotInWC Or _
               .prop_status = eWCStatus_NotInWC Then
            grdFiles.Cell(iRow, grdFiles.ColumnIndex("file")).IconIndex = 9
        Else
            grdFiles.Cell(iRow, grdFiles.ColumnIndex("file")).IconIndex = 7
        End If
    End With
End Sub
Public Function FindRow(sCol As String, s As String) As Long
    Dim iCol As Long
    Dim i As Long
    
    iCol = grdFiles.ColumnIndex(sCol)
    For i = 1 To grdFiles.Rows
        If grdFiles.Cell(i, iCol).Key = s Then
            FindRow = i
            Exit Function
        End If
    Next i
    FindRow = -1
End Function
Public Sub PopulateSGrid(sPath As String)
    
    Dim fldr As Folder
    Dim f As File
    Dim i As Long
    Dim aStatus() As SVNCOMLib.SVNStatus
    Dim iRow As Long
    Dim IsSVNDir As Boolean
    
    grdFiles.Redraw = False
    
    grdFiles.Clear
    i = 1
    Set fldr = g_fs.GetFolder(sPath)
    IsSVNDir = WC.check_wc(sPath)
    ' Populate the grid.
    For Each f In fldr.Files
        grdFiles.AddRow , f.Name
        grdFiles.Cell(i, grdFiles.ColumnIndex("file")).Text = f.Name
        grdFiles.Cell(i, grdFiles.ColumnIndex("date")).Text = FormatDateTime(f.DateLastModified)
        grdFiles.Cell(i, grdFiles.ColumnIndex("size")).Text = Str(f.Size)
        If Not IsSVNDir Then
            grdFiles.Cell(i, grdFiles.ColumnIndex("file")).IconIndex = 9
        End If
        i = i + 1
    Next
    If IsSVNDir Then
        ' Lookup file statuses
        WC.wc_statuses sPath, aStatus
        ' Assign the appropriate icons:
        For i = 0 To UBound(aStatus)
            iRow = FindRow("file", aStatus(i).Name)
            If iRow <> -1 Then
                AssignIcons iRow, aStatus(i)
            Else
                grdFiles.AddRow , aStatus(i).Name
                iRow = FindRow("file", aStatus(i).Name)
                grdFiles.Cell(iRow, grdFiles.ColumnIndex("file")).Text = aStatus(i).Name
                If aStatus(i).text_status = eWCStatus_Deleted Then
                   grdFiles.Cell(iRow, grdFiles.ColumnIndex("file")).IconIndex = 8
                Else
                   grdFiles.Cell(iRow, grdFiles.ColumnIndex("file")).IconIndex = 4
                End If
                   
            End If
        Next i
        ' Fixup any icons for files we don't know about
        For i = 1 To grdFiles.Rows
            If grdFiles.CellIcon(i, grdFiles.ColumnIndex("file")) = -1 Then
                grdFiles.CellIcon(i, grdFiles.ColumnIndex("file")) = 9
            End If
        Next i
    End If
    If g_sListViewPath <> sPath Then
        WC.watch_dir sPath
        g_sListViewPath = sPath
    End If
    grdFiles.Redraw = True
    
End Sub


Private Sub Class_Initialize()
    Set WC = Startup.g_SVN_WC
    Set grdFiles = fMainForm.grdFiles
    InitSGrid
End Sub

Private Sub WC_RefreshFiles(ByVal bstrDir As String)
    If bstrDir <> g_sListViewPath Then
        Exit Sub
    End If
    PopulateSGrid bstrDir
End Sub
