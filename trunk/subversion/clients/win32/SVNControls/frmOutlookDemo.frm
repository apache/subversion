VERSION 5.00
Object = "{396F7AC0-A0DD-11D3-93EC-00C0DFE7442A}#1.0#0"; "vbalIml6.ocx"
Object = "*\ASGrid.vbp"
Begin VB.Form frmOutlookDemo 
   Caption         =   "Outlook Style Grid Demonstration"
   ClientHeight    =   3924
   ClientLeft      =   1476
   ClientTop       =   2172
   ClientWidth     =   7332
   BeginProperty Font 
      Name            =   "Tahoma"
      Size            =   8.4
      Charset         =   0
      Weight          =   400
      Underline       =   0   'False
      Italic          =   0   'False
      Strikethrough   =   0   'False
   EndProperty
   Icon            =   "frmOutlookDemo.frx":0000
   LinkTopic       =   "Form2"
   ScaleHeight     =   3924
   ScaleWidth      =   7332
   Begin SVNControls.SGrid grdOutlook 
      Height          =   3612
      Left            =   0
      TabIndex        =   0
      Top             =   120
      Width           =   7212
      _ExtentX        =   12721
      _ExtentY        =   6371
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
   Begin vbalIml6.vbalImageList ilsIcons 
      Left            =   6540
      Top             =   300
      _ExtentX        =   762
      _ExtentY        =   762
      Size            =   17860
      Images          =   "frmOutlookDemo.frx":014A
      KeyCount        =   19
      Keys            =   "ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ"
   End
   Begin VB.Menu mnuFileTOP 
      Caption         =   "&File"
      Begin VB.Menu mnuFile 
         Caption         =   "&Close"
         Index           =   0
      End
   End
   Begin VB.Menu mnuViewTOP 
      Caption         =   "&View"
      Begin VB.Menu mnuView 
         Caption         =   "&Columns"
         Index           =   0
         Begin VB.Menu mnuColumns 
            Caption         =   ""
            Index           =   0
         End
      End
      Begin VB.Menu mnuView 
         Caption         =   "&Auto-Preview"
         Index           =   1
         Begin VB.Menu mnuPreview 
            Caption         =   "&None"
            Index           =   0
         End
         Begin VB.Menu mnuPreview 
            Caption         =   "&Unread Messages"
            Checked         =   -1  'True
            Index           =   1
         End
         Begin VB.Menu mnuPreview 
            Caption         =   "&All Messages"
            Index           =   2
         End
      End
      Begin VB.Menu mnuView 
         Caption         =   "&Grouping..."
         Index           =   2
      End
   End
End
Attribute VB_Name = "frmOutlookDemo"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = True
Attribute VB_Exposed = False
Option Explicit

Private m_bGroup As Boolean

Public Sub DoGroup(ByVal iItems As Long, sGroupColumns() As String, eOrder() As SVNControls.cShellSortOrderCOnstants)
Dim i As Long
Dim iRow As Long
Dim iCol As Long
Dim iNumber As Long
Dim sFnt As StdFont
Dim iFnt As IFont
Dim sJunk() As String, eJunk() As cShellSortOrderCOnstants
Dim bForce As Boolean
Static iRefCount As Long

   iRefCount = iRefCount + 1
   iNumber = iItems - 1
   If (iNumber > 2) Then
      MsgBox "Can't do it - max grouping is restricted to 3 columns for this demo.", vbInformation
   Else
      ' Stop redraw for speed:
      If (iRefCount = 1) Then
         grdOutlook.Redraw = False
      End If
      
      If (iNumber < 0) Then
         m_bGroup = False
         ' Remove all existing group rows:
         For iRow = grdOutlook.Rows To 1 Step -1
            If (grdOutlook.CellItemData(iRow, 14) > 0) Then
               grdOutlook.RemoveRow iRow
            End If
         Next iRow
         For i = 0 To 2
            grdOutlook.ColumnVisible("group" & i + 1) = False
         Next i
         For iRow = 1 To grdOutlook.Rows
            grdOutlook.RowVisible(iRow) = True
         Next iRow
      Else

         ' Remove groupings:
         DoGroup 0, sJunk(), eJunk()

         m_bGroup = True
         ' Make the relevant headers visible:
         For i = 0 To 2
            If (i <= iNumber) Then
               grdOutlook.ColumnVisible("group" & i + 1) = True
            Else
               grdOutlook.ColumnVisible("group" & i + 1) = False
            End If
         Next i
         
         ' Sort the grid according to the groupings:
         With grdOutlook.SortObject
            .Clear
            For i = 0 To iNumber
               .SortColumn(i + 1) = grdOutlook.ColumnIndex(sGroupColumns(i))
               .SortOrder(i + 1) = eOrder(i)
               If grdOutlook.ColumnSortType(sGroupColumns(i)) = CCLSortDate Then
                  .SortType(i + 1) = CCLSortDateDayAccuracy
               Else
                  .SortType(i + 1) = grdOutlook.ColumnSortType(sGroupColumns(i))
               End If
            Next i
         End With
         grdOutlook.Sort
         
         ' Now add grouping rows:
         ReDim vLastItem(0 To iNumber) As Variant
         Set iFnt = grdOutlook.Font
         iFnt.Clone sFnt
         sFnt.Bold = True
         iRow = 1
         Do
            bForce = False
            For i = 0 To iNumber
               If Not grdOutlook.RowIsGroup(iRow) Then
                  iCol = grdOutlook.ColumnIndex(sGroupColumns(i))
                  Select Case grdOutlook.ColumnSortType(sGroupColumns(i))
                  Case CCLSortIcon
                     If grdOutlook.CellIcon(iRow, iCol) <> vLastItem(i) Or bForce Then
                        vLastItem(i) = grdOutlook.CellIcon(iRow, iCol)
                        grdOutlook.AddRow iRow, "GROUP", , , True, i + 1
                        grdOutlook.CellDetails iRow, 14, , , vLastItem(i), vbButtonFace, , sFnt, , 16, i + 1
                        bForce = True
                     End If
                  Case Else
                     If grdOutlook.CellText(iRow, iCol) <> vLastItem(i) Or bForce Then
                        vLastItem(i) = grdOutlook.CellText(iRow, iCol)
                        grdOutlook.AddRow iRow, "GROUP", , , True, i + 1
                        grdOutlook.CellDetails iRow, 14, vLastItem(i), , , vbButtonFace, , sFnt, , 16, i + 1
                        bForce = True
                     End If
                  End Select
               End If
            Next i
            iRow = iRow + 1
         Loop While iRow < grdOutlook.Rows
         For iRow = 1 To grdOutlook.Rows
            If Not grdOutlook.CellItemData(iRow, 14) = 1 Then
               grdOutlook.RowVisible(iRow) = False
            End If
         Next iRow
      End If
      
      ' Start redrawing again:
      If (iRefCount = 1) Then
         grdOutlook.Redraw = True
      End If
   End If
   iRefCount = iRefCount - 1
End Sub


Private Sub Form_Load()
Dim iRow As Long
Dim iIconUrgent As Long
Dim iIconAttach As Long
Dim iIconFlag As Long
Dim iIconType As Long
Dim iIdx As Long
Dim dDate As Date
Dim lCol As Long
Dim iCol As Long
Dim lHeight As Long
Dim cS As cGridCell
Dim cSUnread As cGridCell
Dim iMenu As Long

   m_bGroup = False
   With grdOutlook
      ' Turn redraw off for speed:
      .Redraw = False
   
      ' Set up the grid:
      
      ' Source of icons.  This can be vbAccelerator ImageList control, class or
      ' a VB ImageList
      .ImageList = ilsIcons
      ' Row mode - select the entire row:
      .RowMode = True
      ' Allow more than one row to be selected:
      .MultiSelect = True
      ' Set the default row height:
      .DefaultRowHeight = 18
      ' Outlook style for the header control:
      .HeaderFlat = True
      .HighlightSelectedIcons = True
      
      ' Add the columns:
      .AddColumn "group1", , , , 16, False, , , False
      .AddColumn "group2", , , , 16, False, , , False
      .AddColumn "group3", , , , 16, False, , , False
      .AddColumn "urgency", , , 9, 26, , , , False, , , CCLSortIcon
      .AddColumn "type", , , 10, 26, , , , False, , , CCLSortIcon
      .AddColumn "attach", , , 12, 26, , , , False, , , CCLSortIcon
      .AddColumn "flag", , , 11, 26, , , , False, , , CCLSortIcon
      .AddColumn "from", "From", , , 96
      .AddColumn "subject", "Subject", , , 256
      .AddColumn "received", "Received", , , 96, , , , , "dd/mm/yy hh:mm", , CCLSortDate
      .AddColumn "to", "To", , , 96
      ' Add two invisible columns to cache status information:
      .AddColumn "read", , , , , False
      .AddColumn "ID", , , , , False
      ' The special "rowcolumntext" column must be added to the end
      ' of the available columns.  This never appears as a column
      ' header, but the text in it is drawn underneath the row (assuming
      ' the row is high enough for it, starting at the column
      ' specified by .RowTextStartColumn:
      .AddColumn "body", , , , 96 + 256 + 96 + 96, , , , , , True
      .KeySearchColumn = .ColumnIndex("subject")
      ' You can specify specifically at which column the text will start
      ' like this:
      '   .RowTextStartColumn = .ColumnIndex("from")
      ' If you do this you need to track the ColumnOrderChanged event to
      ' ensure you are at the right column if the user moves this column
      ' to the end of the grid.  If you don't specify this setting, the
      ' grid will automatically start drawing rowtext at the position
      ' of the first column included in the select (bIncludeInSelect
      ' parameter of AddColumn)
         
      
      ' Once we have added the columns, we can set the headers up
      ' (if we are using headers)
      .SetHeaders
      
      ' Add some demonstration rows:
      
      ' Set up a bold font:
      Dim sFntUnread As New StdFont
      sFntUnread.Name = "Tahoma"
      sFntUnread.Size = 8
      sFntUnread.Bold = True
      
      Set cS = .NewCellFormatObject
      Set cSUnread = .NewCellFormatObject
      Set cSUnread.Font = sFntUnread
      
      ' Create some pretend text for From, Subject and Body
      Dim sFrom(1 To 10) As String
      sFrom(1) = "Carl Ridenhour"
      sFrom(2) = "Dale Winton"
      sFrom(3) = "Richard D James"
      sFrom(4) = "Luke Slater"
      sFrom(5) = "Mark Bell"
      sFrom(6) = "Frank Black"
      sFrom(7) = "Richard Clayderman"
      sFrom(8) = "James Last"
      sFrom(9) = "Thurston Moore"
      sFrom(10) = "Beth Gibbons"
      
      Dim sSubject(1 To 10) As String
      sSubject(1) = "Check out this demo"
      sSubject(2) = "RE: Sonic Bubblebath Remix"
      sSubject(3) = "FW: The secret world of plants"
      sSubject(4) = "U know u gonna dig this"
      sSubject(5) = "RE: FW: What Mandelson didn't say"
      sSubject(6) = "viz New York Trip"
      sSubject(7) = "Belated Happy Birthday"
      sSubject(8) = "RE: What's the score?"
      sSubject(9) = "vbAccelerator: Excellent site!"
      sSubject(10) = "Pass the peas..."
      
      Dim sBody(1 To 10) As String
      sBody(1) = "Impress passing airline passengers by painting a large blue rectangle in your back garden.  They will think that you have a swimming pool."
      sBody(2) = "Bus drivers: pretend to be an airline pilot by wedging the accelerator pedal down with a brick, tying the steering wheel to your seat with a rope and then walking up and down the aisle asking passengers if they are having a nice trip."
      sBody(3) = "A bloke walks into a butchers.  He says ""I bet you £100 that you can't get that meat down from the top shelf"".  The butcher looks up, thinks for a moment, then says ""Sorry mate, can't do it, the steaks are too high""."
      sBody(4) = "A skeleton walks into a bar.  He goes up to the barman and asks for a pint of beer and a mop."
      sBody(5) = "What's red and invisible?  Not a tomato."
      sBody(6) = "President Clinton was reviewing his Christmas shopping with Hilary.  He said ""Well, I think I did a bit better this year, but I wish I hadn't splashed out on that dress""."
      sBody(7) = "Jeffrey Archer Rhyming Slang Pt 1: Whistles and Flute - Shoplifting a Suit."
      sBody(8) = "Jeffrey Archer Rhyming Slang Pt 2: Trouble and Strife: Prostitute"
      sBody(9) = "Small ad (inadvertently) printed in Birmingham Evening Mail: 'For Sale: Blow-up Doll.  Almost as new, needs cleaning.  Slightly stained.  Easy clean plastic maids outfit.  Offers around £100.'"
      sBody(10) = "Say goodbye to Millenium Bug Fears with the Trouser Press 2000." & vbCrLf & "Belgian scientists have been working around the clock to find a solution to the Millenium's most worrying problem - what happens if your trousers are trapped in their press at midnight on December 31st 1999." & vbCrLf & vbCrLf & "Rest assured that thanks to this miracle of bug-free microchip technology you will be wearing a crisply-creased pair of your favourite trousers to greet the new Millenium. (Batteries extra)."
                           
      ' Now add the rows:
      For iRow = 1 To 200
         
         ' set the urgency:
         iIconUrgent = Rnd * 3
         Select Case iIconUrgent
         Case 1
            iIconUrgent = 7
         Case 2
            iIconUrgent = 8
         Case Else
            iIconUrgent = -1
         End Select
         .CellDetails iRow, 4, , , iIconUrgent
         
         ' set the type:
         If (iRow < 16) Then
            iIconType = 1
         Else
            iIconType = Rnd * 2 + 2
         End If
         .CellIcon(iRow, 5) = iIconType
         
         ' set the attachment:
         If Rnd * 20 > 17 Then
            iIconAttach = 14
         Else
            iIconAttach = -1
         End If
         .CellIcon(iRow, 6) = iIconAttach
         
         ' set the Flag:
         If Rnd * 20 > 18 Then
            iIconFlag = 13
         Else
            iIconFlag = -1
         End If
         .CellIcon(iRow, 7) = iIconFlag
         
         ' mark as irrelevant ("junk mail"):
         iIdx = Int(Rnd * 9) + 1
         If iIdx = 7 Or iIdx = 8 Then
            lCol = vbButtonFace
         Else
            lCol = -1
         End If
         
         ' from:
         If (iRow < 16) Then
            .CellDetails iRow, 8, sFrom(iIdx), , , , lCol, sFntUnread
         Else
            .CellDetails iRow, 8, sFrom(iIdx), , , , lCol
         End If
         
         ' subject:
         iIdx = Int(Rnd * 9) + 1
         If (iRow < 16) Then
            .CellDetails iRow, 9, sSubject(iIdx), , , , lCol, sFntUnread
         Else
            .CellDetails iRow, 9, sSubject(iIdx), , , , lCol
         End If
         
         ' date:
         dDate = Now
         dDate = DateAdd("m", -Rnd * 12, dDate)
         dDate = DateAdd("d", -Rnd * 31, dDate)
         dDate = dDate + TimeSerial(Rnd * 24, Rnd * 60, Rnd * 60)
         If (iRow < 16) Then
            .CellDetails iRow, 10, dDate, , , , lCol, sFntUnread
         Else
            .CellDetails iRow, 10, dDate, , , , lCol
         End If
         
         ' to:
         If (iRow < 16) Then
            .CellDetails iRow, 11, "Steve McMahon", , , , lCol, sFntUnread
         Else
            .CellDetails iRow, 11, "Steve McMahon", , , , lCol
         End If
         
         iIdx = Int(Rnd * 9) + 1
         .CellDetails iRow, 14, sBody(iIdx), DT_WORDBREAK, , , RGB(0, 0, &H80)
         lHeight = .EvaluateTextHeight(iRow, 14) + .DefaultRowHeight + 2

         ' Read/unread marker:
         If (iRow < 16) Then
            .CellDetails iRow, 12, "NOTREAD"
            .RowHeight(iRow) = lHeight
         Else
            .CellDetails iRow, 12, "READ"
         End If
         
         ' ID marker:
         .CellDetails iRow, 13, iRow
                  
         
      Next iRow
      
      
      ' Add the columns to the menu:
      For iCol = 1 To .Columns
         If (.ColumnVisible(iCol)) And (iCol <> 14) Then
            If (iMenu > 0) Then
               Load mnuColumns(iMenu)
               mnuColumns(iMenu).Visible = True
            End If
            If (.ColumnHeader(iCol) = "") Then
               mnuColumns(iMenu).Caption = StrConv(.ColumnKey(iCol), vbProperCase)
            Else
               mnuColumns(iMenu).Caption = .ColumnHeader(iCol)
            End If
            mnuColumns(iMenu).Tag = .ColumnKey(iCol)
            mnuColumns(iMenu).Checked = True
            iMenu = iMenu + 1
         End If
      Next iCol
      
      .Redraw = True
   End With
End Sub

Private Sub Form_Resize()
   grdOutlook.Move 2 * Screen.TwipsPerPixelX, 2 * Screen.TwipsPerPixelY, Me.ScaleWidth - 4 * Screen.TwipsPerPixelX, Me.ScaleHeight - 4 * Screen.TwipsPerPixelY
End Sub

Private Sub grdOutlook_ColumnClick(ByVal lCol As Long)
Dim iCol As Long
Dim sJunk() As String, eJunk() As cShellSortOrderCOnstants

   If m_bGroup Then
      If vbNo = MsgBox("Sorting by this column will remove your groupings.  Are you sure you want to do this?" & vbCrLf & vbCrLf & "Note: this problem is fixable in code, but I leave it as an exercise...", vbYesNo Or vbQuestion) Then
         Exit Sub
      Else
         DoGroup 0, sJunk(), eJunk()
      End If
   End If
      
   With grdOutlook.SortObject
      .Clear
      .SortColumn(1) = lCol
      If (grdOutlook.ColumnSortOrder(lCol) = CCLOrderNone) Or (grdOutlook.ColumnSortOrder(lCol) = CCLOrderDescending) Then
         .SortOrder(1) = CCLOrderAscending
      Else
         .SortOrder(1) = CCLOrderDescending
      End If
      grdOutlook.ColumnSortOrder(lCol) = .SortOrder(1)
      .SortType(1) = grdOutlook.ColumnSortType(lCol)
      
      ' Place ascending/descending icon:
      For iCol = 1 To grdOutlook.Columns
         If (iCol <> lCol) Then
            If grdOutlook.ColumnImage(iCol) > 16 Then
               grdOutlook.ColumnImage(iCol) = 0
            End If
         ElseIf grdOutlook.ColumnHeader(iCol) <> "" Then
            grdOutlook.ColumnImageOnRight(iCol) = True
            If (.SortOrder(1) = CCLOrderAscending) Then
               grdOutlook.ColumnImage(iCol) = 17
            Else
               grdOutlook.ColumnImage(iCol) = 18
            End If
         End If
      Next iCol
      
   End With
   
   Screen.MousePointer = vbHourglass
   grdOutlook.Sort
   Screen.MousePointer = vbDefault
   
End Sub

Private Sub grdOutlook_ColumnOrderChanged()
   '
End Sub

Private Sub grdOutlook_ColumnWidthChanging(ByVal lCol As Long, ByVal lWidth As Long, bCancel As Boolean)
   If (lWidth < 26) Then
      lWidth = 26
   End If
End Sub

Private Sub grdOutlook_DblClick(ByVal lRow As Long, ByVal lCol As Long)
Dim sKey As String
Dim bFound As Boolean
Dim lItemData As Long
Dim bIgnoreUntilNext As Boolean

   If (lRow > 0) And (lCol > 0) Then
      ' Dbl clicked on a valid cell.  Find out whether it is a group or
      ' not:
      sKey = grdOutlook.ColumnKey(lCol)
      If (sKey = "body") Then
         grdOutlook.Redraw = False
         ' Expand or collapse:
         lItemData = grdOutlook.CellItemData(lRow, 14)
         If (grdOutlook.CellExtraIcon(lRow, 14) = 15) Then
            ' collapse:
            grdOutlook.CellExtraIcon(lRow, 14) = 16
            lRow = lRow + 1
            Do While lRow <= grdOutlook.Rows And Not bFound
               If grdOutlook.CellItemData(lRow, 14) = 0 Or grdOutlook.CellItemData(lRow, 14) > lItemData Then
                  grdOutlook.RowVisible(lRow) = False
               Else
                  bFound = True
               End If
               lRow = lRow + 1
            Loop
         Else
            ' expand:
            grdOutlook.CellExtraIcon(lRow, 14) = 15
            lRow = lRow + 1
            Do While lRow <= grdOutlook.Rows And Not bFound
               If grdOutlook.CellItemData(lRow, 14) = 0 Then
                  If Not (bIgnoreUntilNext) Then
                     grdOutlook.RowVisible(lRow) = True
                  End If
               ElseIf grdOutlook.CellItemData(lRow, 14) > lItemData Then
                  grdOutlook.RowVisible(lRow) = True
                  bIgnoreUntilNext = (grdOutlook.CellExtraIcon(lRow, 14) = 16)
               Else
                  bFound = True
               End If
               lRow = lRow + 1
            Loop
         End If
         grdOutlook.Redraw = True
      End If
   End If
End Sub

Private Sub grdOutlook_HeaderRightClick(ByVal x As Single, ByVal y As Single)
    Debug.Print grdOutlook.HeaderColumnHitTest(x, y)
End Sub

Private Sub grdOutlook_RequestEdit(ByVal lRow As Long, ByVal lCol As Long, ByVal iKeyAscii As Integer, bCancel As Boolean)
Static sSearch As String
   Debug.Print "RequestEdit"
   If (iKeyAscii <> 0) Then
      Debug.Print iKeyAscii
      ' Search for the match:
      If (iKeyAscii <> 8) Then
         sSearch = sSearch & Chr$(iKeyAscii)
      Else
         If (Len(sSearch) > 0) Then
            sSearch = Left$(sSearch, Len(sSearch) - 1)
         End If
      End If
      Debug.Print sSearch
   End If
   bCancel = True
End Sub

Private Sub mnuColumns_Click(Index As Integer)
Dim bS As Long
   bS = Not (mnuColumns(Index).Checked)
   mnuColumns(Index).Checked = bS
   grdOutlook.ColumnVisible(mnuColumns(Index).Tag) = bS
End Sub

Private Sub mnuFile_Click(Index As Integer)
   Unload Me
End Sub

Private Sub mnuPreview_Click(Index As Integer)
Dim i As Long
Dim lHeight As Long

   For i = 0 To 2
      mnuPreview(i).Checked = (i = Index)
   Next i
   
   grdOutlook.Redraw = False
   If (Index = 0) Then
      ' No preview:
      For i = 1 To grdOutlook.Rows
         If Not grdOutlook.RowIsGroup(i) Then
            grdOutlook.RowHeight(i) = grdOutlook.DefaultRowHeight
         End If
      Next i
   ElseIf (Index = 1) Then
      ' Preview unread only:
      For i = 1 To grdOutlook.Rows
         If Not grdOutlook.RowIsGroup(i) Then
            If (grdOutlook.CellText(i, 12) = "NOTREAD") Then
               lHeight = grdOutlook.EvaluateTextHeight(i, 14) + grdOutlook.DefaultRowHeight
               grdOutlook.RowHeight(i) = lHeight
            Else
               grdOutlook.RowHeight(i) = grdOutlook.DefaultRowHeight
            End If
         End If
      Next i
   Else
      ' All preview:
      For i = 1 To grdOutlook.Rows
         If Not grdOutlook.RowIsGroup(i) Then
            lHeight = grdOutlook.EvaluateTextHeight(i, 14) + grdOutlook.DefaultRowHeight
            grdOutlook.RowHeight(i) = lHeight
         End If
      Next i
   End If
   grdOutlook.Redraw = True
End Sub

Private Sub mnuView_Click(Index As Integer)
   If (Index = 2) Then
      Dim fC As frmOutlookGroup
      Dim j As Long

      Set fC = New frmOutlookGroup
      For j = mnuColumns.LBound To mnuColumns.UBound
         fC.AddField mnuColumns(j).Caption, mnuColumns(j).Tag
      Next j
            
      fC.Show vbModal
      If Not fC.Cancelled Then
         Screen.MousePointer = vbHourglass
         If fC.SelectionCount > 0 Then
            ReDim sThis(0 To fC.SelectionCount - 1) As String
            ReDim eOrder(0 To fC.SelectionCount - 1) As cShellSortOrderCOnstants
            For j = 1 To fC.SelectionCount
               sThis(j - 1) = fC.SelectedKey(j)
               eOrder(j - 1) = fC.SelectedOrder(j)
            Next j
            DoGroup fC.SelectionCount, sThis(), eOrder()
         Else
            DoGroup 0, sThis(), eOrder()
         End If
         Screen.MousePointer = vbDefault
      End If
   End If
End Sub
