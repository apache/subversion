VERSION 1.0 CLASS
BEGIN
  MultiUse = -1  'True
  Persistable = 0  'NotPersistable
  DataBindingBehavior = 0  'vbNone
  DataSourceBehavior  = 0  'vbNone
  MTSTransactionMode  = 0  'NotAnMTSObject
END
Attribute VB_Name = "cHeaderControl"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = False
Attribute VB_Exposed = False
Option Explicit
' ======================================================================================
' Filename: cHeader control
' Author:   Steve McMahon (steve@vbaccelerator.com)
' Date:     02 June 1998
'
' Requires: SSUBTMR.DLL
'
' Description
' An implementation of the Common Control header control.
'
' Changes:
' 19/10/99, SPM
' * Setting the icon in a column with no text didn't work: basically no text
'   didn't work. Fixed by removing the HDF_STRING format if blank text.
'
' 01/01/99, SPM
' * Attempt to set ColumnImage to -1 (no icon) or an index not in the ImageList
'   caused GPF.
' * AddColumn method set image to the first image in the ImageList when no Image
'   specified.
' * ColumnHeader property set to "" caused no change or a corrupt string to
'   appear in the header and ColumnHeader property.
' * RemoveColumn for column other than the last one caused the ColumnTags to be
'   incorrect.
' * Don't raise ColumnEndDrag event when cancel column dragging
' * Added method for getting or setting column order (ColumnIndex).
' * Added method for getting and setting column alignment (ColumnTextAlign).
' * Added method for getting and setting image alignment left/right (ColumnImageOnRight).
' * Previous version re-created the control from scratch when setting styles, this
'   was not necessary. Now just the style is changed for a smoother display.
'
' Issues:
' Full Drag mode does not work - drag-drop not supported.
' No tool-tips.
'
' --------------------------------------------------------------------------------------
' Copyright © 1998-199 Steve McMahon (steve@vbaccelerator.com)
' Visit vbAccelerator - free, advanced VB source code.
'    http://vbaccelerator.com
' ======================================================================================


' ======================================================================================
' API declares:
' ======================================================================================

' Creating new windows:
Private Declare Function CreateWindowEx Lib "user32" Alias "CreateWindowExA" (ByVal dwExStyle As Long, ByVal lpClassName As String, ByVal lpWindowName As String, ByVal dwStyle As Long, ByVal x As Long, ByVal y As Long, ByVal nWidth As Long, ByVal nHeight As Long, ByVal hWndParent As Long, ByVal hMenu As Long, ByVal hInstance As Long, lpParam As Any) As Long
' General window styles:
Private Const WS_BORDER = &H800000
Private Const WS_CAPTION = &HC00000                  '  WS_BORDER Or WS_DLGFRAME
Private Const WS_CHILD = &H40000000
Private Const WS_CHILDWINDOW = (WS_CHILD)
Private Const WS_CLIPCHILDREN = &H2000000
Private Const WS_CLIPSIBLINGS = &H4000000
Private Const WS_DISABLED = &H8000000
Private Const WS_DLGFRAME = &H400000
Private Const WS_EX_ACCEPTFILES = &H10&
Private Const WS_EX_DLGMODALFRAME = &H1&
Private Const WS_EX_NOPARENTNOTIFY = &H4&
Private Const WS_EX_TOPMOST = &H8&
Private Const WS_EX_TRANSPARENT = &H20&
Private Const WS_GROUP = &H20000
Private Const WS_HSCROLL = &H100000
Private Const WS_MAXIMIZE = &H1000000
Private Const WS_MAXIMIZEBOX = &H10000
Private Const WS_MINIMIZE = &H20000000
Private Const WS_ICONIC = WS_MINIMIZE
Private Const WS_MINIMIZEBOX = &H20000
Private Const WS_OVERLAPPED = &H0&
Private Const WS_POPUP = &H80000000
Private Const WS_SYSMENU = &H80000
Private Const WS_TABSTOP = &H10000
Private Const WS_THICKFRAME = &H40000
Private Const WS_SIZEBOX = WS_THICKFRAME
Private Const WS_TILED = WS_OVERLAPPED
Private Const WS_VISIBLE = &H10000000
Private Const WS_VSCROLL = &H200000
Private Const WS_OVERLAPPEDWINDOW = (WS_OVERLAPPED Or WS_CAPTION Or WS_SYSMENU Or WS_THICKFRAME Or WS_MINIMIZEBOX Or WS_MAXIMIZEBOX)
Private Const WS_TILEDWINDOW = WS_OVERLAPPEDWINDOW
Private Const WS_POPUPWINDOW = (WS_POPUP Or WS_BORDER Or WS_SYSMENU)

' Window appearance control:
Private Declare Function ShowWindow Lib "user32" (ByVal hwnd As Long, ByVal nCmdShow As Long) As Long
' Show window styles
Private Const SW_SHOWNORMAL = 1
Private Const SW_ERASE = &H4
Private Const SW_HIDE = 0
Private Const SW_INVALIDATE = &H2
Private Const SW_MAX = 10
Private Const SW_MAXIMIZE = 3
Private Const SW_MINIMIZE = 6
Private Const SW_NORMAL = 1
Private Const SW_OTHERUNZOOM = 4
Private Const SW_OTHERZOOM = 2
Private Const SW_PARENTCLOSING = 1
Private Const SW_RESTORE = 9
Private Const SW_PARENTOPENING = 3
Private Const SW_SHOW = 5
Private Const SW_SCROLLCHILDREN = &H1
Private Const SW_SHOWDEFAULT = 10
Private Const SW_SHOWMAXIMIZED = 3
Private Const SW_SHOWMINIMIZED = 2
Private Const SW_SHOWMINNOACTIVE = 7
Private Const SW_SHOWNA = 8
Private Const SW_SHOWNOACTIVATE = 4
Private Declare Function DestroyWindow Lib "user32" (ByVal hwnd As Long) As Long
Private Declare Function EnableWindow Lib "user32" (ByVal hwnd As Long, ByVal fEnable As Long) As Long
Private Declare Function SetFocus Lib "user32" (ByVal hwnd As Long) As Long
Private Declare Function MoveWindow Lib "user32" (ByVal hwnd As Long, ByVal x As Long, ByVal y As Long, ByVal nWidth As Long, ByVal nHeight As Long, ByVal bRepaint As Long) As Long
Private Declare Function InvalidateRect Lib "user32" (ByVal hwnd As Long, lpRect As RECT, ByVal bErase As Long) As Long

Private Declare Function GetCursorPos Lib "user32" (lpPoint As POINTAPI) As Long

' CommonControls function
Private Declare Sub InitCommonControls Lib "COMCTL32.DLL" ()

' Header stuff:
Private Const WC_HEADERA = "SysHeader32"
Private Const WC_HEADER = WC_HEADERA

Private Const HDS_HORZ = &H0
Private Const HDS_BUTTONS = &H2
Private Const HDS_HIDDEN = &H8

Private Const HDS_HOTTRACK = &H4 ' v 4.70
Private Const HDS_DRAGDROP = &H40 ' v 4.70
Private Const HDS_FULLDRAG = &H80

Private Const HDI_WIDTH = &H1
Private Const HDI_HEIGHT = HDI_WIDTH
Private Const HDI_TEXT = &H2
Private Const HDI_FORMAT = &H4
Private Const HDI_LPARAM = &H8
Private Const HDI_BITMAP = &H10

'
Private Const HDI_IMAGE = &H20
Private Const HDI_DI_SETITEM = &H40
Private Const HDI_ORDER = &H80

Private Const HDF_LEFT = 0
Private Const HDF_RIGHT = 1
Private Const HDF_CENTER = 2
Private Const HDF_JUSTIFYMASK = &H3
Private Const HDF_RTLREADING = 4
' 4.70+
Private Const HDF_BITMAP_ON_RIGHT = &H1000
Private Const HDF_IMAGE = &H800

Private Const HDF_OWNERDRAW = &H8000
Private Const HDF_STRING = &H4000
Private Const HDF_BITMAP = &H2000

Private Const HDM_FIRST = &H1200                    '// Header messages

Private Const HDM_GETITEMCOUNT = (HDM_FIRST + 0)
' Header_GetItemCount(hwndHD) \
'    (int)SendMessage((hwndHD), HDM_GETITEMCOUNT, 0, 0L)
Private Const HDM_INSERTITEMA = (HDM_FIRST + 1)
Private Const HDM_INSERTITEM = HDM_INSERTITEMA
'Header_InsertItem(hwndHD, i, phdi) \
'    (int)SendMessage((hwndHD), HDM_INSERTITEM, (WPARAM)(int)(i), (LPARAM)(const HD_ITEM FAR*)(phdi))
Private Const HDM_DELETEITEM = (HDM_FIRST + 2)
'Header_DeleteItem(hwndHD, i) \
'    (BOOL)SendMessage((hwndHD), HDM_DELETEITEM, (WPARAM)(int)(i), 0L)
Private Const HDM_GETITEMA = (HDM_FIRST + 3)
Private Const HDM_GETITEM = HDM_GETITEMA
'Header_GetItem(hwndHD, i, phdi) \
'    (BOOL)SendMessage((hwndHD), HDM_GETITEM, (WPARAM)(int)(i), (LPARAM)(HD_ITEM FAR*)(phdi))
Private Const HDM_SETITEMA = (HDM_FIRST + 4)
Private Const HDM_SETITEM = HDM_SETITEMA
' Header_SetItem(hwndHD, i, phdi) \
'    (BOOL)SendMessage((hwndHD), HDM_SETITEM, (WPARAM)(int)(i), (LPARAM)(const HD_ITEM FAR*)(phdi))
Private Const HDM_LAYOUT = (HDM_FIRST + 5)
' Header_Layout(hwndHD, playout) \
'    (BOOL)SendMessage((hwndHD), HDM_LAYOUT, 0, (LPARAM)(HD_LAYOUT FAR*)(playout))
Private Const HDM_ORDERTOINDEX = (HDM_FIRST + 15)
Private Const HDM_SETIMAGELIST = (HDM_FIRST + 8)
'  Header_SetImageList(hwnd, himl) \
'        (HIMAGELIST)SNDMSG((hwnd), HDM_SETIMAGELIST, 0, (LPARAM)himl)
Private Const HDM_GETIMAGELIST = (HDM_FIRST + 9)
' Header_GetImageList(hwnd) \
'        (HIMAGELIST)SNDMSG((hwnd), HDM_GETIMAGELIST, 0, 0)

Private Const HHT_NOWHERE = &H1
Private Const HHT_ONHEADER = &H2
Private Const HHT_ONDIVIDER = &H4
Private Const HHT_ONDIVOPEN = &H8
Private Const HHT_ABOVE = &H100
Private Const HHT_BELOW = &H200
Private Const HHT_TORIGHT = &H400
Private Const HHT_TOLEFT = &H800
Private Const HDM_HITTEST = (HDM_FIRST + 6)

Private Const H_MAX As Long = &HFFFF + 1
Private Const HDN_FIRST = H_MAX - 300&                  '// header
Private Const HDN_LAST = H_MAX - 399&

Private Const HDN_ITEMCHANGINGA = (HDN_FIRST - 0)
Private Const HDN_ITEMCHANGINGW = (HDN_FIRST - 20)
Private Const HDN_ITEMCHANGEDA = (HDN_FIRST - 1)
Private Const HDN_ITEMCHANGEDW = (HDN_FIRST - 21)
Private Const HDN_ITEMCLICKA = (HDN_FIRST - 2)
Private Const HDN_ITEMCLICKW = (HDN_FIRST - 22)
Private Const HDN_ITEMDBLCLICKA = (HDN_FIRST - 3)
Private Const HDN_ITEMDBLCLICKW = (HDN_FIRST - 23)
Private Const HDN_DIVIDERDBLCLICKA = (HDN_FIRST - 5)
Private Const HDN_DIVIDERDBLCLICKW = (HDN_FIRST - 25)
Private Const HDN_BEGINTRACKA = (HDN_FIRST - 6)
Private Const HDN_BEGINTRACKW = (HDN_FIRST - 26)
Private Const HDN_ENDTRACKA = (HDN_FIRST - 7)
Private Const HDN_ENDTRACKW = (HDN_FIRST - 27)
Private Const HDN_TRACKA = (HDN_FIRST - 8)
Private Const HDN_TRACKW = (HDN_FIRST - 28)
Private Const HDN_ITEMCHANGING = HDN_ITEMCHANGINGA
Private Const HDN_ITEMCHANGED = HDN_ITEMCHANGEDA
Private Const HDN_ITEMCLICK = HDN_ITEMCLICKA
Private Const HDN_ITEMDBLCLICK = HDN_ITEMDBLCLICKA
Private Const HDN_DIVIDERDBLCLICK = HDN_DIVIDERDBLCLICKA
Private Const HDN_BEGINTRACK = HDN_BEGINTRACKA
Private Const HDN_ENDTRACK = HDN_ENDTRACKA
Private Const HDN_TRACK = HDN_TRACKA

' v 4.70
Private Const HDN_BEGINDRAG = (HDN_FIRST - 10)
Private Const HDN_ENDDRAG = (HDN_FIRST - 11)


Private Const NM_FIRST = H_MAX               '(0U-  0U)       // generic to all controls
Private Const NM_LAST = H_MAX - 99& '               (0U- 99U)

Private Const NM_OUTOFMEMORY = (NM_FIRST - 1)
Private Const NM_CLICK = (NM_FIRST - 2)
Private Const NM_DBLCLK = (NM_FIRST - 3)
Private Const NM_RETURN = (NM_FIRST - 4)
Private Const NM_RCLICK = (NM_FIRST - 5)
Private Const NM_RDBLCLK = (NM_FIRST - 6)
Private Const NM_SETFOCUS = (NM_FIRST - 7)
Private Const NM_KILLFOCUS = (NM_FIRST - 8)


Private Type HD_HITTESTINFO
    pt As POINTAPI
    flags As Long
    iItem As Long
End Type

Private Type HD_ITEM
    mask As Long
    cxy As Long
    pszText As String
    hbm As Long
    cchTextMax As Long
    fmt As Long
    lParam As Long
    ' 4.70:
    iImage As Long
    iOrder As Long
End Type

Private Type NMHDR
    hwndFrom As Long
    idfrom As Long
    code As Long
End Type
Private Type HD_NOITFY
    hdr As NMHDR
    iItem As Long
    iButton As Long
    pitem As HD_ITEM
End Type
' This structure is a *bit* VB unfriendly...
Private Type NMHEADER
   hdr As NMHDR
   iItem As Long
   iButton As Long
   lPtrHDItem As Long '    HDITEM  FAR* pItem
End Type
    
Private Type WINDOWPOS
    hwnd As Long
    hWndInsertAfter As Long
    x As Long
    y As Long
    cx As Long
    cy As Long
    flags As Long
End Type
Private Type HD_LAYOUT
    prc As RECT
    pwpos As WINDOWPOS
End Type
    

' Messages:

' General windows messages:
Private Const WM_COMMAND = &H111
Private Const WM_KEYDOWN = &H100
Private Const WM_KEYUP = &H101
Private Const WM_CHAR = &H102
Private Const WM_SETFOCUS = &H7
Private Const WM_KILLFOCUS = &H8
Private Const WM_SETFONT = &H30
Private Const WM_GETTEXT = &HD
Private Const WM_GETTEXTLENGTH = &HE
Private Const WM_SETTEXT = &HC
Private Const WM_NOTIFY = &H4E&

' ======================================================================================
' Private variables:
' ======================================================================================
' Handle of control window:
Private m_hWnd As Long
' Handle of parent:
Private m_hWndParent As Long
' User mode?
Private m_bUserMode As Boolean
' Position
Private m_tR As RECT
Private m_bInitSize As Boolean
' IsVisible:
Private m_bVisible As Boolean

' Font support:
Private m_tULF As LOGFONT
Private m_hFnt As Long
' Image list:
Private m_hIml As Long
' Tags:
Private m_sTag() As String

' Subclassing support:
Implements ISubclass
Private m_emr As EMsgResponse
Private m_bSubClass As Boolean

' Style setting
Private m_bHotTrack As Boolean
Private m_bDragReorderColumns As Boolean
Private m_bButtons As Boolean
Private m_bFullDrag As Boolean
' Enable:
Private m_bEnabled As Boolean
            
' Column/width whilst column changing:
Private m_lCol As Long
Private m_lCXY As Long
Private m_lColOrder As Long


' Last return code of call:
Private m_lR As Long

' ======================================================================================
' Implementation:
' ======================================================================================
' Item Alignment options:
Public Enum EHdrTextAlign
    HdrTextALignLeft = HDF_LEFT
    HdrTextALignCentre = HDF_CENTER
    HdrTextALignRight = HDF_RIGHT
    HdrTextAlignRTLReading = HDF_RTLREADING
End Enum

' Events
Public Event ColumnWidthChanged(lColumn As Long, ByVal lWidth As Long)
Public Event ColumnWidthChanging(lColumn As Long, ByVal lWidth As Long, bCancel As Boolean)
Public Event StartColumnWidthChange(lColumn As Long, ByVal lWidth As Long, bCancel As Boolean)
Public Event DividerDblClick(lColumn As Long)
Public Event ColumnClick(lColumn As Long)
Public Event ColumnDblClick(lColumn As Long)
Public Event ColumnBeginDrag(lColumn As Long)
Public Event ColumnEndDrag(lColumn As Long, lOrder As Long)
Public Event RightClick(x As Single, y As Single)
Public Event RecreateControl()

Friend Property Get Visible() As Boolean
   Visible = m_bVisible
End Property
Friend Property Let Visible(ByVal bVisible As Boolean)
   m_bVisible = bVisible
   If m_hWnd <> 0 Then
      If (bVisible) Then
         ShowWindow m_hWnd, SW_SHOW
      Else
         ShowWindow m_hWnd, SW_HIDE
      End If
   End If
End Property
Friend Property Get Left() As Long
   Left = m_tR.Left
End Property
Friend Property Get Top() As Long
   Top = m_tR.Top
End Property
Friend Property Get Width() As Long
   Width = m_tR.Right - m_tR.Left
End Property
Friend Property Get Height() As Long
   Height = m_tR.Bottom - m_tR.Top
End Property

Friend Function HitTest(ByVal x As Single, ByVal y As Single) As Long
    Dim hti As HD_HITTESTINFO
    hti.pt.x = x / Screen.TwipsPerPixelX
    hti.pt.y = y / Screen.TwipsPerPixelY
    HitTest = SendMessage(m_hWnd, HDM_HITTEST, 0, hti)
End Function

Friend Property Let Left(ByVal lLeft As Long)
   m_tR.Right = lLeft + m_tR.Right - m_tR.Left
   m_tR.Left = lLeft
   pResize
End Property
Friend Property Let Top(ByVal lTop As Long)
   m_tR.Bottom = lTop + m_tR.Bottom - m_tR.Top
   m_tR.Top = lTop
   pResize
End Property
Friend Property Let Width(ByVal lWidth As Long)
   m_tR.Right = m_tR.Left + lWidth
   pResize
End Property
Friend Property Let Height(ByVal lHeight As Long)
   m_tR.Bottom = m_tR.Top + lHeight
   pResize
End Property
Friend Sub Move(ByVal lLeft As Long, ByVal lTop As Long, Optional ByVal lWidth As Long = -1, Optional ByVal lHeight As Long = -1)

   m_tR.Right = lLeft + m_tR.Right - m_tR.Left
   m_tR.Left = lLeft
   m_tR.Bottom = lTop + m_tR.Bottom - m_tR.Top
   m_tR.Top = lTop
   If (lWidth > -1) Then
      m_tR.Right = m_tR.Left + lWidth
   End If
   If (lHeight > -1) Then
      m_tR.Bottom = m_tR.Top + lHeight
   End If
   pResize
End Sub
Private Sub pResize()
   If m_hWnd <> 0 Then
      m_bInitSize = True
      MoveWindow m_hWnd, m_tR.Left, m_tR.Top, m_tR.Right - m_tR.Left, m_tR.Bottom - m_tR.Top, 1
      InvalidateRect m_hWnd, m_tR, 1
      UpdateWindow m_hWnd
   End If
End Sub

Friend Sub Init(ByVal hWndParent As Long, ByVal bUserMode As Boolean)
   m_hWndParent = hWndParent
   m_bUserMode = bUserMode
   pCreateHeader
End Sub

Friend Property Get Enabled() As Boolean
   Enabled = m_bEnabled
End Property
Friend Property Let Enabled(ByVal bEnabled As Boolean)
Static bResetOnEnable As Boolean
   If (m_bEnabled <> bEnabled) Then
      m_bEnabled = bEnabled
      If Not (m_bEnabled) Then
         If (m_bButtons) Then
            HasButtons = False
            m_bButtons = True
            bResetOnEnable = True
         End If
      Else
         If (bResetOnEnable) Then
            If (m_bButtons) Then
               m_bButtons = False
               HasButtons = True
            End If
         End If
      End If
      EnableWindow m_hWnd, Abs(m_bEnabled)
   End If
End Property

Friend Property Get ColumnTag(ByVal lColumn As Long) As String
    ColumnTag = m_sTag(lColumn)
End Property
Friend Property Let ColumnTag(ByVal lColumn As Long, ByVal sTag As String)
    If (lColumn < ColumnCount) Then
        m_sTag(lColumn) = sTag
    Else
        Debug.Print "Error setting column tag."
    End If
End Property
Friend Sub SetImageList(ByVal lHDC As Long, ByRef vImageList As Variant)
    If (VarType(vImageList) = vbLong) Then
        m_hIml = vImageList
        pSetImageList
    ElseIf (VarType(vImageList) = vbObject) Then
        On Error Resume Next
        ' Ensure image list is initialised:
        vImageList.ListImages(1).Draw lHDC
        Err.Clear
        m_hIml = vImageList.hImageList
        If (Err.Number <> 0) Then
            m_hIml = 0
            Debug.Print "Error setting image list."
        Else
            If (m_hWnd <> 0) Then
                pSetImageList
            End If
        End If
        On Error GoTo 0
    Else
        Debug.Print "Error setting image list."
    End If
End Sub
Friend Property Get HasButtons() As Boolean
   HasButtons = m_bButtons
End Property
Friend Property Let HasButtons(ByVal bHasButtons As Boolean)
   If (bHasButtons <> m_bButtons) Then
      m_bButtons = bHasButtons
      If (m_hWnd <> 0) Then
         pSetStyle HDS_BUTTONS, bHasButtons
      End If
   End If
End Property
Friend Property Get FullDrag() As Boolean
   FullDrag = m_bFullDrag
End Property
Friend Property Let FullDrag(ByVal bFullDrag As Boolean)
   If (m_bFullDrag <> bFullDrag) Then
      m_bFullDrag = bFullDrag
      If (m_hWnd <> 0) Then
         pRecreateControl
      End If
   End If
End Property

Friend Property Get ColumnIndex(ByVal lColumn As Long) As Long
Dim tHI As HD_ITEM
   If (lColumn <> m_lCol) Then
      tHI.mask = HDI_ORDER
      If (pbGetHeaderItemInfo(lColumn, tHI)) Then
         ColumnIndex = tHI.iOrder
      End If
   Else
      ColumnIndex = m_lColOrder
   End If
End Property
Friend Property Let ColumnIndex(ByVal lColumn As Long, ByVal lOrder As Long)
Dim tHI As HD_ITEM
   If (ColumnIndex(lColumn) <> lOrder) Then
      tHI.mask = HDI_ORDER
      tHI.iOrder = lOrder
      If (pbSetHeaderItemInfo(lColumn, tHI)) Then
         ' ok
      Else
         ' error
         Debug.Print "Set column order error"
      End If
   End If
End Property

Friend Property Get ColumnExtraData(ByVal lColumn As Long) As Long
Dim tHI As HD_ITEM
    tHI.mask = HDI_LPARAM
    If (pbGetHeaderItemInfo(lColumn, tHI)) Then
        ColumnExtraData = tHI.lParam
    Else
        ' Error
        Debug.Print "Get column extra data error"
    End If
End Property
Friend Property Let ColumnExtraData(ByVal lColumn As Long, ByVal lExtraData As Long)
Dim tHI As HD_ITEM
    tHI.mask = HDI_LPARAM
    tHI.lParam = lExtraData
    If (pbSetHeaderItemInfo(lColumn, tHI)) Then
    Else
        ' Error
        Debug.Print "Set column extra data error"
    End If
End Property
Friend Property Get ColumnTextAlign(ByVal lColumn As Long) As EHdrTextAlign
Dim tHI As HD_ITEM
   tHI.mask = HDI_FORMAT
   If (pbGetHeaderItemInfo(lColumn, tHI)) Then
      ColumnTextAlign = tHI.fmt And &H7&
   End If
End Property
Friend Property Let ColumnTextAlign(ByVal lColumn As Long, ByVal eAlign As EHdrTextAlign)
Dim tHI As HD_ITEM
   tHI.mask = HDI_FORMAT
   If (pbGetHeaderItemInfo(lColumn, tHI)) Then
      tHI.fmt = tHI.fmt And Not &H7&
      tHI.fmt = tHI.fmt Or eAlign
      If (pbSetHeaderItemInfo(lColumn, tHI)) Then
      Else
         ' failed.
      End If
   End If
End Property

Friend Property Get ColumnWidth(ByVal lColumn As Long) As Long
Dim tHI As HD_ITEM
   If (lColumn = m_lCol) Then
      ColumnWidth = m_lCXY
   Else
      tHI.mask = HDI_WIDTH
      If (pbGetHeaderItemInfo(lColumn, tHI)) Then
          ColumnWidth = tHI.cxy
      Else
          ' Error
          Debug.Print "Get column width error"
      End If
   End If
End Property

Friend Property Let ColumnWidth(ByVal lColumn As Long, ByVal lWidthPixels As Long)
Dim tHI As HD_ITEM
    If (ColumnWidth(lColumn) <> lWidthPixels) Then
        tHI.mask = HDI_WIDTH
        tHI.cxy = lWidthPixels
        If (pbSetHeaderItemInfo(lColumn, tHI)) Then
            RaiseEvent ColumnWidthChanged(lColumn, lWidthPixels)
        Else
            ' Error
            Debug.Print "Set column width error"
        End If
    End If
End Property
Friend Property Get ColumnImage(ByVal lColumn As Long) As Long
Dim tHI As HD_ITEM
   tHI.mask = HDI_FORMAT
   If (pbGetHeaderItemInfo(lColumn, tHI)) Then
      If (tHI.fmt And HDF_IMAGE) = HDF_IMAGE Then
         tHI.mask = HDI_IMAGE
         If (pbGetHeaderItemInfo(lColumn, tHI)) Then
             ColumnImage = tHI.iImage
         Else
             ' Error
             Debug.Print "Get column image error"
         End If
      Else
         ColumnImage = -1
      End If
   End If
End Property

Friend Property Let ColumnImage(ByVal lColumn As Long, ByVal lImage As Long)
Dim tHI As HD_ITEM
   If (ColumnImage(lColumn) <> lImage) Then
      tHI.mask = HDI_FORMAT
      If pbGetHeaderItemInfo(lColumn, tHI) Then
         If (pbValidImage(lImage) < 0) Then
            tHI.fmt = tHI.fmt Or HDF_IMAGE
            tHI.mask = tHI.mask Or HDI_IMAGE
            tHI.iImage = lImage
         Else
            tHI.fmt = tHI.fmt And Not HDF_IMAGE
         End If
         If (pbSetHeaderItemInfo(lColumn, tHI)) Then
             ' ok
         Else
             ' Error
             Debug.Print "Set column image error"
         End If
      End If
   End If
End Property
Private Function pbValidImage(ByVal lImgIndex As Long) As Boolean
Dim iCount As Long
   If (m_hIml <> 0) Then
      iCount = ImageList_GetImageCount(m_hIml)
      If (lImgIndex > -1) And (lImgIndex < iCount) Then
         pbValidImage = True
      End If
   End If
End Function

Friend Property Get ColumnImageOnRight(ByVal lColumn As Long) As Boolean
Dim tHI As HD_ITEM
   tHI.mask = HDI_FORMAT
   If (pbGetHeaderItemInfo(lColumn, tHI)) Then
      ColumnImageOnRight = ((tHI.fmt And HDF_BITMAP_ON_RIGHT) = HDF_BITMAP_ON_RIGHT)
   End If
End Property
Friend Property Let ColumnImageOnRight(ByVal lColumn As Long, ByVal bState As Boolean)
Dim tHI As HD_ITEM
   tHI.mask = HDI_FORMAT
   If (pbGetHeaderItemInfo(lColumn, tHI)) Then
      If (bState) Then
         tHI.fmt = tHI.fmt Or HDF_BITMAP_ON_RIGHT
      Else
         tHI.fmt = tHI.fmt And Not HDF_BITMAP_ON_RIGHT
      End If
      If (pbSetHeaderItemInfo(lColumn, tHI)) Then
         ' ok
      Else
         Debug.Print "Failed to set image on right property"
      End If
   End If
End Property
Friend Property Get ColumnHeader(ByVal lColumn As Long) As String
Dim tHI As HD_ITEM
Dim sColHeader As String
Dim iPos As Long
   tHI.cchTextMax = 255
   sColHeader = String$(tHI.cchTextMax, Chr$(0))
   tHI.mask = HDI_TEXT Or HDI_FORMAT
   tHI.pszText = sColHeader
   If (pbGetHeaderItemInfo(lColumn, tHI)) Then
      If (tHI.fmt And HDF_STRING) = HDF_STRING Then
         sColHeader = tHI.pszText
         iPos = InStr(sColHeader, Chr$(0))
         If (iPos <> 0) Then
            ColumnHeader = Left$(sColHeader, iPos - 1)
         Else
            ColumnHeader = sColHeader
         End If
      Else
      End If
   Else
      ' error
      Debug.Print "Get column header text error"
   End If
End Property
Friend Property Let ColumnHeader(ByVal lColumn As Long, ByVal sText As String)
Dim tHI As HD_ITEM

   If (ColumnHeader(lColumn) <> sText) Then
      tHI.mask = HDI_FORMAT
      If pbGetHeaderItemInfo(lColumn, tHI) Then
         If Len(sText) > 0 Then
            tHI.fmt = ColumnTextAlign(lColumn) Or HDF_STRING
            tHI.mask = tHI.mask Or HDI_TEXT
            tHI.pszText = sText
            tHI.cchTextMax = Len(sText)
         Else
            tHI.fmt = tHI.fmt And Not HDF_STRING
         End If
         If pbSetHeaderItemInfo(lColumn, tHI) Then
            'ok
         Else
            ' error
            Debug.Print "Set column text error"
         End If
      End If
   Else
       ' Error
       Debug.Print "Set column header text error"
   End If
   
End Property
Friend Property Get HotTrack() As Boolean
   HotTrack = m_bHotTrack
End Property
Friend Property Let HotTrack(ByVal bHotTrack As Boolean)
   If (m_bHotTrack <> bHotTrack) Then
      m_bHotTrack = bHotTrack
      If (m_hWnd <> 0) Then
         'pRecreateControl
         pSetStyle HDS_HOTTRACK, bHotTrack
      End If
   End If
End Property
Friend Property Get DragReOrderColumns() As Boolean
   DragReOrderColumns = m_bDragReorderColumns
End Property
Friend Property Let DragReOrderColumns(ByVal bState As Boolean)
   If (m_bDragReorderColumns <> bState) Then
      m_bDragReorderColumns = bState
      If (m_hWnd <> 0) Then
         'pRecreateControl
         pSetStyle HDS_DRAGDROP, bState
      End If
   End If
End Property

Friend Sub RemoveColumn(ByVal lColumn As Long)
Dim lR As Long
Dim iCol As Long

   lR = SendMessageByLong(m_hWnd, HDM_DELETEITEM, lColumn, 0)
   If (lR <> 0) Then
      If (ColumnCount > 0) Then
         For iCol = lColumn To UBound(m_sTag) - 1
            m_sTag(iCol) = m_sTag(iCol + 1)
         Next iCol
         ReDim Preserve m_sTag(0 To ColumnCount - 1) As String
      Else
         Erase m_sTag
      End If
   End If
   
End Sub
Private Sub pSetStyle(ByVal lStyleFlags As Long, ByVal bState As Boolean)
Dim lStyle As Long
   lStyle = GetWindowLong(m_hWnd, GWL_STYLE)
   If (bState) Then
      lStyle = lStyle Or lStyleFlags
   Else
      lStyle = lStyle And Not lStyleFlags
   End If
   SetWindowLong m_hWnd, GWL_STYLE, lStyle
End Sub
Private Function pbGetHeaderItemInfo(ByVal lCol As Long, tHI As HD_ITEM) As Boolean
    If (SendMessage(m_hWnd, HDM_GETITEM, lCol, tHI) <> 0) Then
        pbGetHeaderItemInfo = True
    End If
End Function
Private Function pbSetHeaderItemInfo(ByVal lCol As Long, tHI As HD_ITEM) As Boolean
    If (SendMessage(m_hWnd, HDM_SETITEM, lCol, tHI) <> 0) Then
        pbSetHeaderItemInfo = True
    End If
End Function
Friend Function AddColumn( _
        ByVal sText As String, _
        Optional ByVal lWidth As Long = 64, _
        Optional ByVal eTextAlign As EHdrTextAlign = HdrTextALignLeft, _
        Optional ByVal lExtraData As Long = 0, _
        Optional ByVal lImage As Long = -1, _
        Optional ByVal lInsertAfter As Long = -1 _
    ) As Long
Dim tHI As HD_ITEM
Dim lR As Long
Dim wP As Long
Dim lLen As Long

   tHI.mask = HDI_WIDTH Or HDI_FORMAT Or HDI_LPARAM
   lLen = Len(sText)
   If lLen > 0 Then
      tHI.pszText = sText
      tHI.cchTextMax = lLen
      tHI.mask = tHI.mask Or HDI_TEXT
      tHI.fmt = eTextAlign Or HDF_STRING
   End If
   
   tHI.fmt = eTextAlign Or HDF_STRING
   If (m_hIml <> 0) Then
      If (pbValidImage(lImage)) Then
         tHI.mask = tHI.mask Or HDI_IMAGE
         tHI.fmt = tHI.fmt Or HDF_IMAGE
         tHI.iImage = lImage
      End If
   End If
   tHI.cxy = lWidth
   tHI.lParam = lExtraData
   tHI.pszText = sText
   tHI.cchTextMax = Len(sText)
   
   If (lInsertAfter < 0) Then
      wP = ColumnCount + 1
   Else
      wP = lInsertAfter
   End If
   
   m_lR = SendMessage(m_hWnd, HDM_INSERTITEM, wP, tHI)
   'Debug.Print m_lR
   
   If (ColumnCount > 0) Then
      ReDim Preserve m_sTag(0 To ColumnCount - 1) As String
   End If
      
End Function
Friend Property Get ColumnCount() As Long
    m_lR = SendMessageByLong(m_hWnd, HDM_GETITEMCOUNT, 0, 0)
    ColumnCount = m_lR
End Property
Friend Sub SetFont(ByVal lHDC As Long, sFont As StdFont)
Dim hFnt As Long
   ' Store a log font structure for this font:
   pOLEFontToLogFont sFont, lHDC, m_tULF
   ' Store old font handle:
   hFnt = m_hFnt
   ' Create a new version of the font:
   m_hFnt = CreateFontIndirect(m_tULF)
   ' Ensure the edit portion has the correct font:
   If (m_hWnd <> 0) Then
       SendMessage m_hWnd, WM_SETFONT, m_hFnt, 1
   End If
   ' Delete previous version, if we had one:
   If (hFnt <> 0) Then
       DeleteObject hFnt
   End If
End Sub

Private Sub pCreateHeader()
Dim rcParent As RECT
Dim hdl As HD_LAYOUT
Dim wP As WINDOWPOS
Dim wStyle As Long
Dim lPtr As Long

   ' create the header control.
   wStyle = WS_CHILD Or WS_VISIBLE Or HDS_HORZ
   If (m_bHotTrack) Then
      wStyle = wStyle Or HDS_HOTTRACK
   End If
   If (m_bDragReorderColumns) Then
      wStyle = wStyle Or HDS_DRAGDROP
   End If
   If (m_bButtons) Then
      wStyle = wStyle Or HDS_BUTTONS
   End If
   If (m_bFullDrag) Then
      wStyle = wStyle Or HDS_FULLDRAG
   End If
    
    If Not m_bInitSize Then
      GetClientRect m_hWndParent, m_tR
      m_tR.Bottom = m_tR.Top + 20
      m_bInitSize = True
   End If
   m_hWnd = CreateWindowEx(0, WC_HEADER, "", _
           wStyle, _
           m_tR.Left, m_tR.Top, m_tR.Right, m_tR.Bottom, _
           m_hWndParent, 0, App.hInstance, 0)
   If (m_hWnd <> 0) Then
       ' Commence subclassing:
       pSubClass
       ' Set the image list
       pSetImageList
       
        If Not (m_bUserMode) Then
           ' why does the text not appear in design time?
           AddColumn "Header Control", 128
        End If
        
        Visible = m_bVisible
       
       '// Retrieve the bounding rectangle of the parent window's
       '// client area, and then request size and position values
       '// from the header control.
       'GetClientRect UserControl.hwnd, rcParent
       'LSet hdl.prc = rcParent
       'CopyMemory ByVal lPtr, hdl, LenB(hdl)
       'If (SendMessage(m_hWnd, HDM_LAYOUT, 0, lPtr) <> 0) Then
       '    CopyMemory hdl, ByVal lPtr, LenB(hdl)
       '    ' // Set the size, position, and visibility of the header control.
       '    LSet wp = hdl.pwpos
       '    wp.flags = wp.flags Or SWP_SHOWWINDOW
       '    'SetWindowPos m_hWnd, wp.hWndInsertAfter, wp.x, wp.y, _
       '    '    wp.cx, wp.cy, wp.flags
       'End If
   End If
    

End Sub
Private Sub pRecreateControl()
Dim tHI() As HD_ITEM
Dim iCols As Long
Dim iCol As Long
Dim sCol() As String
Dim iPos As Long
Dim hFnt As Long

   'iCols = ColumnCount
   'If (iCols > 0) Then
   '   ReDim tHI(0 To iCols - 1) As HD_ITEM
   '   ReDim sCol(0 To iCols - 1) As String
   '   For iCol = 0 To iCols - 1
   '      tHI(iCol).cchTextMax = 255
   '      sCol(iCol) = String$(tHI(iCol).cchTextMax, Chr$(0))
   '      tHI(iCol).mask = HDI_TEXT Or HDI_WIDTH Or HDI_FORMAT Or HDI_LPARAM Or HDI_IMAGE
   '      tHI(iCol).pszText = sCol(iCol)
   '      pbGetHeaderItemInfo iCol, tHI(iCol)
   '      iPos = InStr(tHI(iCol).pszText, Chr$(0))
   '      If (iPos <> 0) Then
   '         tHI(iCol).pszText = left$(tHI(iCol).pszText, iPos)
   '      End If
   '   Next iCol
   'End If
   ClearUp
   pCreateHeader
   
   RaiseEvent RecreateControl
   'If (iCols > 0) Then
   '   For iCol = 0 To iCols - 1
   '      AddColumn tHI(iCol).pszText, tHI(iCol).cxy, tHI(iCol).fmt, tHI(iCol).lParam, tHI(iCol).iImage
   '   Next iCol
   '   ' Store a log font structure for this font:
   '   pOLEFontToLogFont UserControl.Font, UserControl.hDC, m_tULF
   '   ' Store old font handle:
   '   hFnt = m_hFnt
   '   ' Create a new version of the font:
   '   m_hFnt = CreateFontIndirect(m_tULF)
   '   ' Ensure the edit portion has the correct font:
   '   If (m_hWnd <> 0) Then
   '       SendMessage m_hWnd, WM_SETFONT, m_hFnt, 1
   '   End If
   '   ' Delete previous version, if we had one:
   '   If (hFnt <> 0) Then
   '       DeleteObject hFnt
   '   End If
   'End If
End Sub


Private Sub pSubClass()
    If (m_bUserMode) Then
        If (m_hWnd <> 0) Then
            AttachMessage Me, m_hWnd, HDM_LAYOUT
            AttachMessage Me, m_hWndParent, WM_NOTIFY
            m_bSubClass = True
        End If
    End If
End Sub
Private Sub pUnSubClass()
    If (m_hWnd <> 0) Then
        If (m_bSubClass) Then
           m_bSubClass = False
            DetachMessage Me, m_hWnd, HDM_LAYOUT
            DetachMessage Me, m_hWndParent, WM_NOTIFY
        End If
    End If
End Sub
Private Sub pSetImageList()
    If (m_hIml <> 0) Then
        SendMessageByLong m_hWnd, HDM_SETIMAGELIST, 0, m_hIml
        If (m_hIml <> SendMessageByLong(m_hWnd, HDM_GETIMAGELIST, 0, 0)) Then
            Debug.Print "Error getting image list"
        End If
    End If
End Sub
Friend Sub ClearUp()
   If (m_hWnd <> 0) Then
      pUnSubClass
      ShowWindow m_hWnd, SW_HIDE
      SetParent m_hWnd, 0
      DestroyWindow m_hWnd
      m_hWnd = 0
      If (m_hFnt <> 0) Then
         DeleteObject m_hFnt
      End If
   End If
   
End Sub

Private Sub Class_Initialize()
   debugmsg "cHeaderControl:Initialize"

   '// Ensure that the common control DLL is loaded,
   InitCommonControls
   m_bEnabled = True
   
   HotTrack = True
   DragReOrderColumns = True
   HasButtons = True
   FullDrag = False
    
End Sub

Private Sub Class_Terminate()
   ClearUp
   debugmsg "cHeaderControl:Terminate"
End Sub

Private Property Let ISubClass_MsgResponse(ByVal RHS As SSubTimer6.EMsgResponse)
    m_emr = RHS
End Property

Private Property Get ISubClass_MsgResponse() As SSubTimer6.EMsgResponse
    ISubClass_MsgResponse = m_emr
End Property

Private Function ISubClass_WindowProc(ByVal hwnd As Long, ByVal iMsg As Long, ByVal wParam As Long, ByVal lParam As Long) As Long
Dim tHDL As HD_LAYOUT
Dim tNMH As NMHDR
Dim tHDN As NMHEADER
Dim lHDI() As Long
Dim bCancel As Boolean
Dim sCol As String
Dim tR As RECT

    Select Case iMsg
    Case HDM_LAYOUT
        ' Got HDM_LAYOUT
        'Debug.Print "HDM_LAYOUT"
        CopyMemory tHDL, ByVal lParam, LenB(tHDL)
        ' Set the window position:
        With tHDL
            GetWindowRect m_hWndParent, tR
            With .pwpos
                .x = tHDL.prc.Left
                .y = tHDL.prc.Top
                .cx = tR.Right - tR.Left
                .cy = tR.Bottom - tR.Top
            End With
            .prc.Left = .pwpos.x
            .prc.Top = .pwpos.y
            .prc.Bottom = .pwpos.cx
            .prc.Right = .pwpos.cy
        End With
        CopyMemory ByVal lParam, tHDL, LenB(tHDL)
        ISubClass_WindowProc = 1
    Case WM_NOTIFY
        CopyMemory tNMH, ByVal lParam, LenB(tNMH)
        Select Case tNMH.code
        Case HDN_BEGINTRACK
            'Debug.Print "Started tracking"
            CopyMemory tHDN, ByVal lParam, Len(tHDN)
            ' Get HD_ITEM from tHDN.lPtrHDItem.  Don't use a HD_ITEM
            ' structure - you will crash...
            ' Here we only need up to the second long (HD_ITEM.cxy)
            ReDim lHDI(0 To 1) As Long
            CopyMemory lHDI(0), ByVal tHDN.lPtrHDItem, 8
            RaiseEvent StartColumnWidthChange(tHDN.iItem, lHDI(1), bCancel)
            If (bCancel) Then
                ISubClass_WindowProc = 1
            End If
        Case HDN_TRACK
            'Debug.Print "Tracking"
            CopyMemory tHDN, ByVal lParam, LenB(tHDN)
            ' Get HD_ITEM from tHDN.lPtrHDItem.  Don't use a HD_ITEM
            ' structure - you will crash...
            ' Here we only need up to the second long (HD_ITEM.cxy)
            ReDim lHDI(0 To 1) As Long
            CopyMemory lHDI(0), ByVal tHDN.lPtrHDItem, 8
            RaiseEvent ColumnWidthChanging(tHDN.iItem, lHDI(1), bCancel)
            If (bCancel) Then
                ISubClass_WindowProc = 1
            End If
        Case HDN_ENDTRACK
            ISubClass_WindowProc = CallOldWindowProc(hwnd, iMsg, wParam, lParam)
            CopyMemory tHDN, ByVal lParam, LenB(tHDN)
            ' Get HD_ITEM from tHDN.lPtrHDItem.  Don't use a HD_ITEM
            ' structure - you will crash...
            ' Here we only need up to the second long (HD_ITEM.cxy)
            ReDim lHDI(0 To 1) As Long
            CopyMemory lHDI(0), ByVal tHDN.lPtrHDItem, 8
            m_lCol = tHDN.iItem
            m_lCXY = lHDI(1)
            RaiseEvent ColumnWidthChanged(tHDN.iItem, lHDI(1))
            m_lCol = -1
        Case HDN_DIVIDERDBLCLICK
            CopyMemory tHDN, ByVal lParam, LenB(tHDN)
            RaiseEvent DividerDblClick(tHDN.iItem)
        Case HDN_ITEMCLICK
            CopyMemory tHDN, ByVal lParam, LenB(tHDN)
            RaiseEvent ColumnClick(tHDN.iItem)
        Case HDN_ITEMDBLCLICK
            CopyMemory tHDN, ByVal lParam, LenB(tHDN)
            RaiseEvent ColumnDblClick(tHDN.iItem)
        Case HDN_BEGINDRAG
            CopyMemory tHDN, ByVal lParam, LenB(tHDN)
            RaiseEvent ColumnBeginDrag(tHDN.iItem)
        Case HDN_ENDDRAG
            ISubClass_WindowProc = CallOldWindowProc(hwnd, iMsg, wParam, lParam)
            CopyMemory tHDN, ByVal lParam, LenB(tHDN)
            ' Get HD_ITEM from tHDN.lPtrHDItem.  Don't use a HD_ITEM
            ' structure - you will crash...
            ' Here we only need up to the eighth long (HD_ITEM.iOrder)
            ReDim lHDI(0 To 8) As Long
            CopyMemory lHDI(0), ByVal tHDN.lPtrHDItem, 36
            ' Check for cancel:
            If (lHDI(8) > -1) Then
               m_lCol = tHDN.iItem
               m_lColOrder = lHDI(8)
               RaiseEvent ColumnEndDrag(tHDN.iItem, lHDI(8))
            End If
            m_lCol = -1
        Case NM_RCLICK
            ' Right click in control
            Dim tp As POINTAPI, sx As Single, sy As Single
            GetCursorPos tp
            ScreenToClient m_hWnd, tp
            sx = tp.x * Screen.TwipsPerPixelX
            sy = tp.y * Screen.TwipsPerPixelY
            RaiseEvent RightClick(sx, sy)
        Case Else
            'Debug.Print tNMH.code
        End Select
    End Select
End Function

