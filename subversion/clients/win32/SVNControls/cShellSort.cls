VERSION 1.0 CLASS
BEGIN
  MultiUse = -1  'True
END
Attribute VB_Name = "cShellSortTGridCells"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = False
Attribute VB_Exposed = True
Option Explicit

' =================================================================
' Class:    cShellSortTGridCells
' Author:   SPM
' Date:     1 Feb 1997, modified 12/01/99 to support tGridCells
'
' Shell sorts a variant array according to a given
' column, using numeric, string or date type, ascending
' or descending.
'
' 19/10/99
' * Added CCLSortStringNoCase
'
' FREE SOURCE CODE - ENJOY!
' =================================================================
Public Enum cShellSortTypeConstants
   ' Text sorting:
    CCLSortNumeric = 100
    CCLSortString = 102
    CCLSortStringNoCase = 103
    
    ' Date sorting
    CCLSortDate = 200
    
    CCLSortDateYearAccuracy = 250
    CCLSortDateMonthAccuracy = 251
    CCLSortDateDayAccuracy = 252
    CCLSortDateHourAccuracy = 253
    CCLSortDateMinuteAccuracy = 254
    
    ' Icon sorting:
    CCLSortIcon = 300
    CCLSortExtraIcon = 301
    
    ' Colour sorting:
    CCLSortForeColor = 400
    CCLSortBackColor = 401
    ' Font sorting:
    CCLSortFontIndex = 500
    ' Selection sorting
    CCLSortSelected = 600
    ' Indentation sorting
    CCLSortIndentation = 700
End Enum
Public Enum cShellSortOrderCOnstants
   CCLOrderNone = 0
   CCLOrderAscending = 1
   CCLOrderDescending = 2
End Enum
Private m_iSortColumn() As Integer
Private m_eSortOrder() As cShellSortOrderCOnstants
Private m_eSortType() As cShellSortTypeConstants
Private m_iSortIndexCount As Integer
Private m_iLastSortIndex As Integer
Public Sub Clear()
Attribute Clear.VB_Description = "Clears all sort settings."
    m_iSortIndexCount = 0
    Erase m_iSortColumn
    Erase m_eSortOrder
    Erase m_eSortType
End Sub
Property Get LastSortIndex() As Integer
    LastSortIndex = m_iLastSortIndex
End Property
Property Let LastSortIndex( _
        ByVal iLastSortIndex As Integer _
    )
    m_iLastSortIndex = iLastSortIndex
End Property
Property Let SortColumn( _
        ByVal iSortIndex As Integer, _
        ByVal iSortColumn As Integer _
    )
Attribute SortColumn.VB_Description = "Gets/sets the grid column to sort by.  Up to three grid columns can be specified for a sort."
    If (pbValidSortIndex(iSortIndex)) Then
        m_iSortColumn(iSortIndex) = iSortColumn
    End If
End Property
Property Get SortColumn( _
        ByVal iSortIndex As Integer _
    ) As Integer
    SortColumn = m_iSortColumn(iSortIndex)
End Property
Property Let SortOrder( _
        ByVal iSortIndex As Integer, _
        ByVal iSortOrder As cShellSortOrderCOnstants _
    )
Attribute SortOrder.VB_Description = "Gets/sets the order to sort in for a specified sort column.  Up to three columns can be specified for a sort."
    If (pbValidSortIndex(iSortIndex)) Then
        m_eSortOrder(iSortIndex) = iSortOrder
    End If
End Property
Property Get SortOrder( _
        ByVal iSortIndex As Integer _
    ) As cShellSortOrderCOnstants
    SortOrder = m_eSortOrder(iSortIndex)
End Property
Property Get SortType( _
        ByVal iSortIndex As Integer _
    ) As cShellSortTypeConstants
Attribute SortType.VB_Description = "Gets/sets the type of sorting to use for a specified sort column.  Up to three columns can be specified for a sort."
    SortType = m_eSortType(iSortIndex)
End Property
Property Let SortType( _
        ByVal iSortIndex As Integer, _
        ByVal eSortType As cShellSortTypeConstants _
    )
    If (pbValidSortIndex(iSortIndex)) Then
        m_eSortType(iSortIndex) = eSortType
    End If
End Property
Private Function pbValidSortIndex( _
        ByVal iSortIndex As Integer _
    ) As Boolean
    If (iSortIndex > 0) And (iSortIndex <= 8) Then
        If (iSortIndex > m_iSortIndexCount) Then
            m_iSortIndexCount = iSortIndex
            ReDim Preserve m_iSortColumn(1 To m_iSortIndexCount) As Integer
            ReDim Preserve m_eSortOrder(1 To m_iSortIndexCount) As cShellSortOrderCOnstants
            ReDim Preserve m_eSortType(1 To m_iSortIndexCount) As cShellSortTypeConstants
        End If
        pbValidSortIndex = True
    Else
        Err.Raise vbObjectError + 1048 + 1, App.EXEName & ".cShellSort", "Invalid sort array index."
    End If
End Function

Friend Sub SortItems( _
        ByRef vItems() As tGridCell, _
        ByRef tRows() As tRowPosition _
    )
Dim iSwapIndex As Long
Dim iIncrement As Long
Dim iMainLoop As Long
Dim iSubLoop As Long
Dim vSortItems() As tGridCell
Dim tSortRow As tRowPosition
Dim iItemCount As Long
Dim iCol As Long
Dim iColumns As Long
           
    iColumns = UBound(vItems, 1)
    iItemCount = UBound(vItems, 2)

    ' Shell sort the list:
    ' ========================================================
    ' Implementation of Shell Sort algorithm using
    ' + 1 * 3 increment.
    ' ========================================================
    ' Prepare swap space storage:
    ReDim vSortItems(1 To iColumns) As tGridCell
    ' Get inital shell sort increment
    If (iItemCount > 2) Then
        iIncrement = piGetSuitableShellSortInitialIncrement(iItemCount)
        Do Until iIncrement < 1
            For iMainLoop = iIncrement + 1 To iItemCount
                ' Store iMainLoop in vSortItems():
                For iCol = 1 To iColumns
                    LSet vSortItems(iCol) = vItems(iCol, iMainLoop)
                Next iCol
                LSet tSortRow = tRows(iMainLoop)
                
                ' Loop form MainLoop-Increment to 0
                For iSubLoop = (iMainLoop - iIncrement) To 1 Step -iIncrement
                    If (pbGreater(vItems(), vSortItems(), iSubLoop)) Then
                        Exit For
                    End If
                    For iCol = 1 To iColumns
                        LSet vItems(iCol, (iSubLoop + iIncrement)) = vItems(iCol, iSubLoop)
                    Next iCol
                    LSet tRows(iSubLoop + iIncrement) = tRows(iSubLoop)
                Next iSubLoop
                For iCol = 1 To iColumns
                    LSet vItems(iCol, (iSubLoop + iIncrement)) = vSortItems(iCol)
                Next iCol
                LSet tRows(iSubLoop + iIncrement) = tSortRow
            Next iMainLoop
            ' Get next shell sort increment value:
            iIncrement = iIncrement - 1
            iIncrement = iIncrement \ 3
        Loop
    Else
        ' For only two items just check whether the second should
        ' be swapped with the first:
        '    Fix - last version caused GPF as it fell off the end
        '    of the array..
        If (iItemCount = 2) Then
            For iCol = 1 To iColumns
               LSet vSortItems(iCol) = vItems(iCol, 1)
            Next iCol
            If pbGreater(vItems(), vSortItems(), 2) Then
               ' swap
               LSet tSortRow = tRows(1)
               LSet tRows(1) = tRows(2)
               LSet tRows(2) = tSortRow
               For iCol = 1 To iColumns
                  LSet vItems(iCol, 1) = vItems(iCol, 2)
                  LSet vItems(iCol, 2) = vSortItems(iCol)
               Next iCol
            End If
         End If
    End If
    
End Sub

Private Function pbGreater( _
        ByRef vItems() As tGridCell, _
        ByRef vSortItems() As tGridCell, _
        ByVal iSubLoop As Long _
    ) As Boolean
Dim iSortIndex As Integer
Dim bIsEqual As Boolean
Dim bR As Boolean

    For iSortIndex = 1 To m_iSortIndexCount
        bR = pbIsGreater(vSortItems(m_iSortColumn(iSortIndex)), vItems(m_iSortColumn(iSortIndex), iSubLoop), iSortIndex, bIsEqual)
        If (iSortIndex < m_iSortIndexCount) And bIsEqual Then
            ' Must go to the next one
        Else
            pbGreater = bR
            Exit For
        End If
    Next iSortIndex

End Function
Private Function pbIsGreater( _
        ByRef vSortItem As tGridCell, _
        ByRef vItem As tGridCell, _
        ByVal iSortIndex As Long, _
        ByRef bIsEqual As Boolean _
    ) As Boolean
Dim vR As Variant
Dim lR As Long
Dim sSortItemText As String, sItemText As String
Dim vSortDate As Date, vDate As Date
Dim bSortDate As Boolean, bDate As Boolean
Dim lDiff As Long

    Select Case m_eSortType(iSortIndex)
    Case CCLSortSelected
        lR = Abs(vSortItem.bSelected) - Abs(vItem.bSelected)
        bIsEqual = (lR = 0)
        If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
            pbIsGreater = (lR >= 0)
        Else
            pbIsGreater = (lR <= 0)
        End If
    
    Case CCLSortFontIndex
        lR = vSortItem.iFntIndex - vItem.iFntIndex
        bIsEqual = (lR = 0)
        If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
            pbIsGreater = (lR >= 0)
        Else
            pbIsGreater = (lR <= 0)
        End If
    
    Case CCLSortIndentation
        lR = vSortItem.lIndent - vItem.lIndent
        bIsEqual = (lR = 0)
        If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
            pbIsGreater = (lR >= 0)
        Else
            pbIsGreater = (lR <= 0)
        End If
    
    Case CCLSortIcon
        lR = vSortItem.iIconIndex - vItem.iIconIndex
        bIsEqual = (lR = 0)
        If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
            pbIsGreater = (lR >= 0)
        Else
            pbIsGreater = (lR <= 0)
        End If
    
    Case CCLSortExtraIcon
        lR = vSortItem.lExtraIconIndex - vItem.lExtraIconIndex
        bIsEqual = (lR = 0)
        If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
            pbIsGreater = (lR >= 0)
        Else
            pbIsGreater = (lR <= 0)
        End If
    
    Case CCLSortForeColor
        lR = vSortItem.oForeColor - vItem.oForeColor
        bIsEqual = (vR = 0)
        If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
            pbIsGreater = (lR >= 0)
        Else
            pbIsGreater = (lR <= 0)
        End If
    
    Case CCLSortBackColor
        lR = vSortItem.oBackColor - vItem.oBackColor
        bIsEqual = (lR = 0)
        If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
            pbIsGreater = (lR >= 0)
        Else
            pbIsGreater = (lR <= 0)
        End If
    
    Case CCLSortNumeric
        vR = Val(vSortItem.sText - vItem.sText)
        bIsEqual = (vR = 0)
        If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
            pbIsGreater = (vR >= 0)
        Else
            pbIsGreater = (vR <= 0)
        End If
        
    Case CCLSortString
      If Not (IsMissing(vSortItem.sText)) Then
         sSortItemText = vSortItem.sText
      End If
      If Not (IsMissing(vItem.sText)) Then
         sItemText = vItem.sText
      End If
      lR = StrComp(sSortItemText, sItemText)
      bIsEqual = (lR = 0)
      If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
          pbIsGreater = (lR > -1)
      Else
          pbIsGreater = (lR < 1)
      End If
      
    Case CCLSortStringNoCase
      If Not (IsMissing(vSortItem.sText)) Then
         sSortItemText = vSortItem.sText
      End If
      If Not (IsMissing(vItem.sText)) Then
         sItemText = vItem.sText
      End If
      lR = StrComp(sSortItemText, sItemText, vbTextCompare)
      bIsEqual = (lR = 0)
      If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
          pbIsGreater = (lR > -1)
      Else
          pbIsGreater = (lR < 1)
      End If
    
    Case CCLSortDate
         If Not (IsMissing(vSortItem.sText)) Then
            sSortItemText = vSortItem.sText
         End If
         If Not (IsMissing(vItem.sText)) Then
            sItemText = vItem.sText
         End If
        bIsEqual = (vSortItem.sText = vItem.sText)
        If Not (bIsEqual) Then
            If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
                pbIsGreater = (vSortItem.sText >= vItem.sText)
            Else
                pbIsGreater = (vItem.sText >= vSortItem.sText)
            End If
        Else
            pbIsGreater = True
        End If
        
    Case CCLSortDateYearAccuracy
      If IsDate(vSortItem.sText) Then
         vSortDate = DateSerial(Year(vSortItem.sText), 1, 1)
         bSortDate = True
      End If
      If IsDate(vItem.sText) Then
         vDate = DateSerial(Year(vItem.sText), 1, 1)
         bDate = True
      End If
      If (bSortDate) And (bDate) Then
         bIsEqual = (vDate = vSortDate)
         If Not (bIsEqual) Then
            If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
                pbIsGreater = (vSortItem.sText >= vItem.sText)
            Else
                pbIsGreater = (vItem.sText >= vSortItem.sText)
            End If
         End If
      ElseIf bSortDate Then
         pbIsGreater = (m_eSortOrder(iSortIndex) = CCLOrderAscending)
      ElseIf bDate Then
         pbIsGreater = (m_eSortOrder(iSortIndex) = CCLOrderDescending)
      Else
         bIsEqual = True
      End If
        
    Case CCLSortDateMonthAccuracy
      If IsDate(vSortItem.sText) Then
         vSortDate = DateSerial(Year(vSortItem.sText), Month(vSortItem.sText), 1)
         bSortDate = True
      End If
      If IsDate(vItem.sText) Then
         vDate = DateSerial(Year(vItem.sText), Month(vItem.sText), 1)
         bDate = True
      End If
      If (bSortDate) And (bDate) Then
         bIsEqual = (vDate = vSortDate)
         If Not (bIsEqual) Then
            If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
                pbIsGreater = (vSortItem.sText >= vItem.sText)
            Else
                pbIsGreater = (vItem.sText >= vSortItem.sText)
            End If
         End If
      ElseIf bSortDate Then
         pbIsGreater = (m_eSortOrder(iSortIndex) = CCLOrderAscending)
      ElseIf bDate Then
         pbIsGreater = (m_eSortOrder(iSortIndex) = CCLOrderDescending)
      Else
         bIsEqual = True
      End If
        
    Case CCLSortDateDayAccuracy
      If IsDate(vSortItem.sText) Then
         vSortDate = DateSerial(Year(vSortItem.sText), Month(vSortItem.sText), Day(vSortItem.sText))
         bSortDate = True
      End If
      If IsDate(vItem.sText) Then
         vDate = DateSerial(Year(vItem.sText), Month(vItem.sText), Day(vItem.sText))
         bDate = True
      End If
      If (bSortDate) And (bDate) Then
         bIsEqual = (vDate = vSortDate)
         If Not (bIsEqual) Then
            If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
                pbIsGreater = (vSortItem.sText >= vItem.sText)
            Else
                pbIsGreater = (vItem.sText >= vSortItem.sText)
            End If
         End If
      ElseIf bSortDate Then
         pbIsGreater = (m_eSortOrder(iSortIndex) = CCLOrderAscending)
      ElseIf bDate Then
         pbIsGreater = (m_eSortOrder(iSortIndex) = CCLOrderDescending)
      Else
         bIsEqual = True
      End If
    
    Case CCLSortDateHourAccuracy
      If IsDate(vSortItem.sText) Then
         vSortDate = DateSerial(Year(vSortItem.sText), Month(vSortItem.sText), Day(vSortItem.sText)) + TimeSerial(Hour(vSortItem.sText), 0, 0)
         bSortDate = True
      End If
      If IsDate(vItem.sText) Then
         vDate = DateSerial(Year(vItem.sText), Month(vItem.sText), Day(vItem.sText)) + TimeSerial(Hour(vItem.sText), 0, 0)
         bDate = True
      End If
      If (bSortDate) And (bDate) Then
         bIsEqual = (vDate = vSortDate)
         If Not (bIsEqual) Then
            If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
                pbIsGreater = (vSortItem.sText >= vItem.sText)
            Else
                pbIsGreater = (vItem.sText >= vSortItem.sText)
            End If
         End If
      ElseIf bSortDate Then
         pbIsGreater = (m_eSortOrder(iSortIndex) = CCLOrderAscending)
      ElseIf bDate Then
         pbIsGreater = (m_eSortOrder(iSortIndex) = CCLOrderDescending)
      Else
         bIsEqual = True
      End If
    
    Case CCLSortDateMinuteAccuracy
      If IsDate(vSortItem.sText) Then
         vSortDate = DateSerial(Year(vSortItem.sText), Month(vSortItem.sText), Day(vSortItem.sText)) + TimeSerial(Hour(vSortItem.sText), Minute(vSortItem.sText), 0)
         bSortDate = True
      End If
      If IsDate(vItem.sText) Then
         vDate = DateSerial(Year(vItem.sText), Month(vItem.sText), Day(vItem.sText)) + TimeSerial(Hour(vItem.sText), Minute(vItem.sText), 0)
         bDate = True
      End If
      If (bSortDate) And (bDate) Then
         bIsEqual = (vDate = vSortDate)
         If Not (bIsEqual) Then
            If (m_eSortOrder(iSortIndex) = CCLOrderAscending) Then
                pbIsGreater = (vSortItem.sText >= vItem.sText)
            Else
                pbIsGreater = (vItem.sText >= vSortItem.sText)
            End If
         End If
      ElseIf bSortDate Then
         pbIsGreater = (m_eSortOrder(iSortIndex) = CCLOrderAscending)
      ElseIf bDate Then
         pbIsGreater = (m_eSortOrder(iSortIndex) = CCLOrderDescending)
      Else
         bIsEqual = True
      End If
       
    End Select
    
End Function
Private Function piGetSuitableShellSortInitialIncrement( _
        iSortSize As Long _
    ) As Long
' ==============================================================
' Part of the implementation of Shell Sort algorithm using
' + 1 * 3 increment strategy.  This function returns the
' largest increment based on +1*3 which is less than the
' sort size.
' ==============================================================
Dim iRet As Long
Dim iLastRet As Long
    iLastRet = 1
    iRet = 1
    Do While iRet < iSortSize
        iLastRet = iRet
        iRet = iRet * 3 + 1
    Loop
    piGetSuitableShellSortInitialIncrement = iLastRet
End Function

Private Sub Class_Initialize()
   debugmsg "cShellSortTGridCells:Initialize"
End Sub

Private Sub Class_Terminate()
   debugmsg "cShellSortTGridCells:Terminate"
End Sub
