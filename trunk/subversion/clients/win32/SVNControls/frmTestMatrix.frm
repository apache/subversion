VERSION 5.00
Object = "{396F7AC0-A0DD-11D3-93EC-00C0DFE7442A}#1.0#0"; "vbalIml6.ocx"
Object = "*\ASGrid.vbp"
Begin VB.Form frmMatrixDemo 
   Caption         =   "Matrix Sample"
   ClientHeight    =   3480
   ClientLeft      =   3648
   ClientTop       =   1920
   ClientWidth     =   6384
   BeginProperty Font 
      Name            =   "Tahoma"
      Size            =   8.4
      Charset         =   0
      Weight          =   400
      Underline       =   0   'False
      Italic          =   0   'False
      Strikethrough   =   0   'False
   EndProperty
   Icon            =   "frmTestMatrix.frx":0000
   LinkTopic       =   "Form1"
   ScaleHeight     =   3480
   ScaleWidth      =   6384
   Begin vbalIml6.vbalImageList ilsIcons 
      Left            =   5640
      Top             =   2880
      _ExtentX        =   762
      _ExtentY        =   762
      Size            =   3760
      Images          =   "frmTestMatrix.frx":0442
      KeyCount        =   4
      Keys            =   "ÿÿÿ"
   End
   Begin VB.ComboBox cboArticle 
      Height          =   300
      Left            =   5040
      TabIndex        =   0
      Text            =   "Combo1"
      Top             =   240
      Visible         =   0   'False
      Width           =   1275
   End
   Begin SVNControls.SGrid grdMatrix 
      Height          =   3012
      Left            =   120
      TabIndex        =   1
      Top             =   120
      Width           =   6132
      _ExtentX        =   10816
      _ExtentY        =   5313
      BackgroundPictureHeight=   0
      BackgroundPictureWidth=   0
      BeginProperty Font {0BE35203-8F91-11CE-9DE3-00AA004BB851} 
         Name            =   "Tahoma"
         Size            =   8.4
         Charset         =   0
         Weight          =   400
         Underline       =   0   'False
         Italic          =   0   'False
         Strikethrough   =   0   'False
      EndProperty
      DisableIcons    =   -1  'True
   End
End
Attribute VB_Name = "frmMatrixDemo"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = True
Attribute VB_Exposed = False
Option Explicit

Private m_sArticles() As String
Private m_iArticleCount As Long
Private m_sTypes() As String
Private m_iTypeCOunt As Long
Private m_sLinks() As String
Private m_iLinkCount As Long

Private Sub pLoadInfo()
Dim iType As Long
Dim iLink As Long
   
   ' In a real application you would use a database to store this information.
   pLoadDelimitedFile App.Path & "\article.dat", m_sArticles(), m_iArticleCount
   pLoadDelimitedFile App.Path & "\type.dat", m_sTypes(), m_iTypeCOunt
   pLoadDelimitedFile App.Path & "\link.dat", m_sLinks(), m_iLinkCount
   
   iLink = 1
   With grdMatrix
      For iType = 1 To m_iTypeCOunt
         .AddRow
         .CellDetails .Rows, 1, m_sTypes(2, iType), DT_WORD_ELLIPSIS Or DT_SINGLELINE, , vbButtonFace, , , , 4
         .CellDetails .Rows, 2, CLng(m_sTypes(1, iType))
         .CellDetails .Rows, 3, 0
         If (iLink <= m_iLinkCount) Then
            Do While m_sLinks(3, iLink) = m_sTypes(1, iType)
               .AddRow
               .CellDetails .Rows, 1, m_sArticles(2, CLng(m_sLinks(2, iLink))), DT_WORD_ELLIPSIS Or DT_SINGLELINE, , , , , 16
               .CellDetails .Rows, 2, CLng(m_sTypes(1, iType))
               .CellDetails .Rows, 3, CLng(m_sArticles(1, CLng(m_sLinks(2, iLink)))), DT_WORD_ELLIPSIS Or DT_SINGLELINE
               iLink = iLink + 1
               If (iLink > m_iLinkCount) Then
                  Exit Do
               End If
            Loop
         End If
         .AddRow
         .CellDetails .Rows, 1, "Click here to add another article...", DT_WORD_ELLIPSIS Or DT_SINGLELINE, , , vbButtonFace, , 16
         .CellDetails .Rows, 2, CLng(m_sTypes(1, iType))
         .CellDetails .Rows, 3, -2
      Next iType
   End With
      
End Sub

Private Sub pLoadDelimitedFile(ByVal sFile As String, ByRef sData() As String, ByRef iCount As Long)
Dim sLines() As String
Dim iLineCount As Long
Dim sItems() As String
Dim iItemCount As Long
Dim iLine As Long
Dim iItem As Long
Dim sDat As String
Dim iFIle As Long
Dim iCol As Long
Dim iColCount As Long
Dim bDoIt As Boolean

   Erase sData
   iCount = 0
   
   iFIle = FreeFile
   Open sFile For Binary Access Read As #iFIle
   sDat = Space$(LOF(iFIle))
   Get #iFIle, , sDat
   Close #iFIle
   
   SplitDelimitedString sDat, vbCrLf, sLines(), iLineCount
   If (iLineCount > 1) Then
      For iLine = 2 To iLineCount
         SplitDelimitedString sLines(iLine), vbTab, sItems(), iItemCount
         If (iItemCount > 0) Then
            bDoIt = False
            If (iColCount = 0) Then
               iColCount = iItemCount
               iCount = 1
               ReDim sData(1 To iColCount, 1 To 1) As String
               bDoIt = True
            Else
               If (iItemCount >= iColCount) Then
                  iCount = iCount + 1
                  ReDim Preserve sData(1 To iColCount, 1 To iCount) As String
                  bDoIt = True
               End If
            End If
            If (bDoIt) Then
               For iCol = 1 To iColCount
                  sData(iCol, iCount) = sItems(iCol)
               Next iCol
            End If
         End If
      Next iLine
   End If
   
End Sub
Private Sub SplitDelimitedString( _
        ByVal sToSplit As String, _
        ByVal sDelim As String, _
        ByRef sItems() As String, _
        ByRef iItemCount As Long _
        )
' ==================================================================
' Splits the string provided in sToSplit at the boundaries of
' sDelim, returning the result as a 1D Array in sItems().
' ==================================================================
Dim iLastPos As Long
Dim iNextPos As Long
Dim iDelimLen As Long
    
    ' Setup:
    Erase sItems
    iItemCount = 0
    iDelimLen = Len(sDelim)
    
    ' Run the split:
    iLastPos = 1
    iNextPos = InStr(sToSplit, sDelim)
    Do While iNextPos <> 0
        iItemCount = iItemCount + 1
        ReDim Preserve sItems(1 To iItemCount) As String
        sItems(iItemCount) = Mid$(sToSplit, iLastPos, (iNextPos - iLastPos))
        iLastPos = iNextPos + iDelimLen
        iNextPos = InStr(iLastPos, sToSplit, sDelim)
    Loop
    iItemCount = iItemCount + 1
    ReDim Preserve sItems(1 To iItemCount) As String
    sItems(iItemCount) = Mid$(sToSplit, iLastPos)
    
End Sub

Private Sub cboArticle_Click()
Dim iType As Long
Dim iArticle As Long
Dim lRow As Long

   If (cboArticle.ListIndex > -1) Then
      With grdMatrix
         iType = .CellText(.SelectedRow, 2)
         iArticle = .CellText(.SelectedRow, 3)
         lRow = .SelectedRow
         If (iArticle = -2) Then
            .AddRow lRow
            .CellIndent(lRow, 1) = 16
            .CellText(lRow, 2) = iType
         End If
         .CellText(lRow, 1) = cboArticle.List(cboArticle.ListIndex)
         .CellText(lRow, 3) = cboArticle.ItemData(cboArticle.ListIndex)
         .SetFocus
      End With
   End If
End Sub

Private Sub cboArticle_KeyDown(KeyCode As Integer, Shift As Integer)
   If (KeyCode = vbKeyEscape) Then
      cboArticle.ListIndex = -1
      grdMatrix.SetFocus
   End If
End Sub

Private Sub cboArticle_LostFocus()
   grdMatrix.CancelEdit
End Sub

Private Sub Form_Load()
   With grdMatrix
      .Redraw = False
      .ImageList = ilsIcons
      .GridLineColor = &HC0C0C0
      .GridLines = True
      .Editable = True
      .AddColumn "tasks", , , , .Width \ Screen.TwipsPerPixelX - 8
      .AddColumn "typeid", , , , , False
      .AddColumn "articleid", , , , , False
      .DefaultRowHeight = cboArticle.Height \ Screen.TwipsPerPixelY
      
      pLoadInfo
                  
      .Redraw = True
   End With
   
End Sub

Private Sub Form_Resize()
On Error Resume Next
   grdMatrix.Move grdMatrix.Left, grdMatrix.Top, Me.ScaleWidth - grdMatrix.Left * 2, Me.ScaleHeight - grdMatrix.Top * 2
   grdMatrix.ColumnWidth("tasks") = grdMatrix.Width \ Screen.TwipsPerPixelX - 24
End Sub

Private Sub grdMatrix_CancelEdit()
   cboArticle.Visible = False
End Sub

Private Sub grdMatrix_DblClick(ByVal lRow As Long, ByVal lCol As Long)
   If (lRow > 0) Then
      If (grdMatrix.CellText(lRow, 3) = 0) Then
         pSetExpand grdMatrix.CellText(lRow, 2), lRow
      End If
   End If
End Sub

Private Sub grdMatrix_KeyDown(KeyCode As Integer, Shift As Integer, bDoDefault As Boolean)
   If grdMatrix.SelectedRow > 0 And grdMatrix.SelectedRow <= grdMatrix.Rows Then
   If grdMatrix.CellText(grdMatrix.SelectedRow, 3) > 0 Then
      If (KeyCode = vbKeyDelete) Or (KeyCode = vbKeyBack) Then
         If (vbYes = MsgBox("Are you sure you want to delete this item?", vbYesNo Or vbQuestion)) Then
            grdMatrix.RemoveRow grdMatrix.SelectedRow
         End If
      End If
   Else
      If (KeyCode = vbKeyReturn) Or (KeyCode = vbKeySpace) Then
         If (grdMatrix.CellText(grdMatrix.SelectedRow, 3) = 0) Then
            pSetExpand grdMatrix.CellText(grdMatrix.SelectedRow, 2), grdMatrix.SelectedRow
         End If
      End If
   End If
   End If
End Sub

Private Sub grdMatrix_MouseDown(Button As Integer, Shift As Integer, x As Single, y As Single, bDoDefault As Boolean)
Dim lCol As Long, lRow As Long
Dim lLeft As Long, lTop As Long, lWidth As Long, lHeight As Long
Dim lType As Long, lClause As Long
Dim lIconIndex As Long
Dim i As Long

   grdMatrix.CellFromPoint x \ Screen.TwipsPerPixelX, y \ Screen.TwipsPerPixelY, lRow, lCol
   If (lCol = 1) And (lRow > 0) Then
      lType = grdMatrix.CellText(lRow, 3)
      lClause = grdMatrix.CellText(lRow, 2)
      If (lType = 0) Then
         grdMatrix.CellBoundary lRow, lCol, lLeft, lTop, lWidth, lHeight
         If (x < lLeft + 20) Then
            pSetExpand lClause, lRow
         End If
      End If
   End If
End Sub
Private Sub pSetExpand(ByVal lClause As Long, ByVal lRow As Long)
Dim lIconIndex As Long
Dim i As Long

   ' Set .Redraw = False to loose the animation effect
   ' when doing this (it might be too slow otherwise)
   
   lIconIndex = grdMatrix.CellExtraIcon(lRow, 1)
   If (lIconIndex = 3) Then
      ' Expand
      lIconIndex = 4
      ' Reverse order only so the "animation" looks nice!
      For i = grdMatrix.Rows To 1 Step -1
         If (grdMatrix.CellText(i, 2) = lClause) Then
            If (grdMatrix.CellText(i, 3) <> 0) Then
               grdMatrix.RowVisible(i) = True
            End If
         End If
      Next i
   Else
      ' Collapse
      lIconIndex = 3
      For i = 1 To grdMatrix.Rows
         If (grdMatrix.CellText(i, 2) = lClause) Then
            If (grdMatrix.CellText(i, 3) <> 0) Then
               grdMatrix.RowVisible(i) = False
            End If
         End If
      Next i
   End If
   grdMatrix.CellExtraIcon(lRow, 1) = lIconIndex
End Sub

Private Sub grdMatrix_RequestEdit(ByVal lRow As Long, ByVal lCol As Long, ByVal iKeyAscii As Integer, bCancel As Boolean)
Dim lLeft As Long, lTop As Long, lWidth As Long, lHeight As Long
Dim iArt As Long, iRow As Long, iType As Long, iArticle As Long, iLink As Long
Dim bDontAdd As Boolean

   If (grdMatrix.CellText(lRow, 3) <> "0") Then
      grdMatrix.CellBoundary lRow, lCol, lLeft, lTop, lWidth, lHeight
      With cboArticle
         .Move lLeft, lTop, lWidth
         ' Add the relevant articles to the cbo box:
         iType = grdMatrix.CellText(lRow, 2)
         iArticle = grdMatrix.CellText(lRow, 3)
         Debug.Print iType, iArticle
         .Clear
         iLink = 1
         For iArt = 1 To m_iArticleCount
            bDontAdd = False
            For iRow = 1 To grdMatrix.Rows
               If grdMatrix.CellText(iRow, 3) > 0 Then
                  iLink = iLink + 1
               End If
               If (grdMatrix.CellText(iRow, 2) = iType) Then
                  If (iArt = grdMatrix.CellText(iRow, 3)) Then
                     If (iArt <> iArticle) Then
                        bDontAdd = True
                     End If
                     Exit For
                  End If
               ElseIf (iLink <= m_iLinkCount) Then
                  If (m_sLinks(3, iLink) > iType) Then
                     Exit For
                  End If
               End If
            Next iRow
            If Not (bDontAdd) Then
               .AddItem m_sArticles(2, iArt)
               .ItemData(.NewIndex) = CLng(m_sArticles(1, iArt))
            End If
         Next iArt
         
         If (grdMatrix.CellText(lRow, 3) <> -2) Then
            .Text = grdMatrix.CellText(lRow, 1)
         Else
            .ListIndex = -1
         End If
         .Visible = True
         .ZOrder
         .SetFocus
      End With
   Else
      bCancel = True
   End If
End Sub
