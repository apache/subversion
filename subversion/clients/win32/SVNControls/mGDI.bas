Attribute VB_Name = "mAPIAndCallbacks"
Option Explicit
' ======================================================================================
' Name:     mGDI
' Author:   Steve McMahon (steve@vbaccelerator.com)
' Date:     22 December 1998
'
' Copyright © 1998-1999 Steve McMahon for vbAccelerator
' --------------------------------------------------------------------------------------
' Visit vbAccelerator - advanced free source code for VB programmers
' http://vbaccelerator.com
' --------------------------------------------------------------------------------------
'
' Various GDI declares and helper functions for the vbAcceleratorGrid
' control.
'
' FREE SOURCE CODE - ENJOY!
' ======================================================================================
#Const DEBUGMODE = 0

' the grid
Public Type tGridCell
   oBackColor As OLE_COLOR
   oForeColor As OLE_COLOR
   iFntIndex As Long
   sText As Variant
   eTextFlags As Long 'ECGTextAlignFlags
   iIconIndex As Long
   bSelected As Boolean
   bDirtyFlag As Boolean
   lIndent As Long
   lExtraIconIndex As Long
   lItemData As Long
   ' 19/10/1999: More options
   bOwnerDraw As Boolean
   lCellBorderStyle As Long
End Type
Public Type tRowPosition
   lHeight As Long
   lStartY As Long
   bVisible As Boolean
   bFixed As Boolean
   sKey As String
   bGroupRow As Boolean
   lGroupStartColIndex As Long
End Type

Public Type RECT
   Left As Long
   Top As Long
   Right As Long
   Bottom As Long
End Type
Public Type LOGBRUSH
   lbStyle As Long
   lbColor As Long
   lbHatch As Long
End Type
Public Type POINTAPI
   x As Long
   y As Long
End Type

Declare Function LockWindowUpdate Lib "user32" (ByVal hwndLock As Long) As Long
Declare Function UpdateWindow Lib "user32" (ByVal hwnd As Long) As Long

Private Declare Function CreateDCAsNull Lib "gdi32" Alias "CreateDCA" ( _
   ByVal lpDriverName As String, lpDeviceName As Any, lpOutput As Any, lpInitData As Any) As Long
Private Declare Function SetROP2 Lib "gdi32" (ByVal hdc As Long, ByVal nDrawMode As Long) As Long
     Private Const R2_BLACK = 1 ' 0
     Private Const R2_COPYPEN = 13 ' P
     Private Const R2_LAST = 16
     Private Const R2_MASKNOTPEN = 3 ' DPna
     Private Const R2_MASKPEN = 9 ' DPa
     Private Const R2_MASKPENNOT = 5 ' PDna
     Private Const R2_MERGENOTPEN = 12    ' DPno
     Private Const R2_MERGEPEN = 15 ' DPo
     Private Const R2_MERGEPENNOT = 14    ' PDno
     Private Const R2_NOP = 11    ' D
     Private Const R2_NOT = 6 ' Dn
     Private Const R2_NOTCOPYPEN = 4 ' PN
     Private Const R2_NOTMASKPEN = 8 ' DPan
     Private Const R2_NOTMERGEPEN = 2 ' DPon
     Private Const R2_NOTXORPEN = 10 ' DPxn
     Private Const R2_WHITE = 16 ' 1
     Private Const R2_XORPEN = 7 ' DPx
Public Declare Function DeleteDC Lib "gdi32" (ByVal hdc As Long) As Long
Public Declare Function ScrollDC Lib "user32" (ByVal hdc As Long, ByVal dx As Long, ByVal dy As Long, lprcScroll As RECT, lprcClip As RECT, ByVal hrgnUpdate As Long, lprcUpdate As RECT) As Long
Public Declare Function Rectangle Lib "gdi32" (ByVal hdc As Long, ByVal X1 As Long, ByVal Y1 As Long, ByVal X2 As Long, ByVal Y2 As Long) As Long
Public Declare Function FrameRect Lib "user32" (ByVal hdc As Long, lpRect As RECT, ByVal hBrush As Long) As Long
Public Declare Function DrawFocusRect Lib "user32" (ByVal hdc As Long, lpRect As RECT) As Long
Public Declare Function ExtCreatePen Lib "gdi32" (ByVal dwPenStyle As Long, ByVal dwWidth As Long, lplb As LOGBRUSH, ByVal dwStyleCount As Long, lpStyle As Long) As Long
Public Const PS_COSMETIC = &H0
Public Const PS_SOLID = 0
Public Declare Function InflateRect Lib "user32" (lpRect As RECT, ByVal x As Long, ByVal y As Long) As Long
Public Declare Function OffsetRect Lib "user32" (lpRect As RECT, ByVal x As Long, ByVal y As Long) As Long
Public Declare Function GetSystemMetrics Lib "user32" (ByVal nIndex As Long) As Long
Public Const SM_CXVSCROLL = 2
Public Const SM_CYHSCROLL = 3
Public Declare Function GetWindowRect Lib "user32" (ByVal hwnd As Long, lpRect As RECT) As Long
Public Declare Function GetClientRect Lib "user32" (ByVal hwnd As Long, lpRect As RECT) As Long
Public Declare Function DrawText Lib "user32" Alias "DrawTextA" (ByVal hdc As Long, ByVal lpStr As String, ByVal nCount As Long, lpRect As RECT, ByVal wFormat As Long) As Long
'#if(WINVER >= =&H0500)
Public Const DT_NOFULLWIDTHCHARBREAK = &H80000
'#if(_WIN32_WINNT >= =&H0500)
Public Const DT_HIDEPREFIX = &H100000
Public Const DT_PREFIXONLY = &H200000
'#endif /* _WIN32_WINNT >= =&H0500 */
'#endif /* WINVER >= =&H0500 */
Public Declare Function GetSysColorBrush Lib "user32" (ByVal nIndex As Long) As Long
Public Const COLOR_HIGHLIGHT = 13
Public Const COLOR_HIGHLIGHTTEXT = 14
Public Declare Function CreateSolidBrush Lib "gdi32" (ByVal crColor As Long) As Long
Public Declare Function FillRect Lib "user32" (ByVal hdc As Long, lpRect As RECT, ByVal hBrush As Long) As Long
Public Declare Function SetTextColor Lib "gdi32" (ByVal hdc As Long, ByVal crColor As Long) As Long
Public Declare Function SetBkColor Lib "gdi32" (ByVal hdc As Long, ByVal crColor As Long) As Long
Public Declare Function SetBkMode Lib "gdi32" (ByVal hdc As Long, ByVal nBkMode As Long) As Long
Public Const OPAQUE = 2
Public Const TRANSPARENT = 1

Public Declare Function BitBlt Lib "gdi32" (ByVal hDestDC As Long, ByVal x As Long, ByVal y As Long, ByVal nWidth As Long, ByVal nHeight As Long, ByVal hSrcDC As Long, ByVal xSrc As Long, ByVal ySrc As Long, ByVal dwRop As Long) As Long
Private Declare Function GetDeviceCaps Lib "gdi32" (ByVal hdc As Long, ByVal nIndex As Long) As Long
Private Const LOGPIXELSX = 88    '  Logical pixels/inch in X
Private Const LOGPIXELSY = 90    '  Logical pixels/inch in Y
Private Declare Function MulDiv Lib "kernel32" (ByVal nNumber As Long, ByVal nNumerator As Long, ByVal nDenominator As Long) As Long
Private Const LF_FACESIZE = 32
Public Type LOGFONT
   lfHeight As Long ' The font size (see below)
   lfWidth As Long ' Normally you don't set this, just let Windows create the Default
   lfEscapement As Long ' The angle, in 0.1 degrees, of the font
   lfOrientation As Long ' Leave as default
   lfWeight As Long ' Bold, Extra Bold, Normal etc
   lfItalic As Byte ' As it says
   lfUnderline As Byte ' As it says
   lfStrikeOut As Byte ' As it says
   lfCharSet As Byte ' As it says
   lfOutPrecision As Byte ' Leave for default
   lfClipPrecision As Byte ' Leave for default
   lfQuality As Byte ' Leave for default
   lfPitchAndFamily As Byte ' Leave for default
   lfFaceName(LF_FACESIZE) As Byte ' The font name converted to a byte array
End Type
Public Declare Function CreateFontIndirect Lib "gdi32" Alias "CreateFontIndirectA" (lpLogFont As LOGFONT) As Long
Public Declare Function SelectObject Lib "gdi32" (ByVal hdc As Long, ByVal hObject As Long) As Long
Public Declare Function DeleteObject Lib "gdi32" (ByVal hObject As Long) As Long
Public Declare Function CreateCompatibleBitmap Lib "gdi32" (ByVal hdc As Long, ByVal nWidth As Long, ByVal nHeight As Long) As Long
Public Declare Function CreateCompatibleDC Lib "gdi32" (ByVal hdc As Long) As Long
Private Const FW_NORMAL = 400
Private Const FW_BOLD = 700
Private Const FF_DONTCARE = 0
Private Const DEFAULT_QUALITY = 0
Private Const DEFAULT_PITCH = 0
Private Const DEFAULT_CHARSET = 1
Private Declare Function OleTranslateColor Lib "OLEPRO32.DLL" (ByVal OLE_COLOR As Long, ByVal HPALETTE As Long, pccolorref As Long) As Long
Private Const CLR_INVALID = -1
' Corrected Draw State function declarations:
Private Declare Function DrawState Lib "user32" Alias "DrawStateA" _
   (ByVal hdc As Long, _
   ByVal hBrush As Long, _
   ByVal lpDrawStateProc As Long, _
   ByVal lParam As Long, _
   ByVal wParam As Long, _
   ByVal x As Long, _
   ByVal y As Long, _
   ByVal cx As Long, _
   ByVal cy As Long, _
   ByVal fuFlags As Long) As Long
Private Declare Function DrawStateString Lib "user32" Alias "DrawStateA" _
   (ByVal hdc As Long, _
   ByVal hBrush As Long, _
   ByVal lpDrawStateProc As Long, _
   ByVal lpString As String, _
   ByVal cbStringLen As Long, _
   ByVal x As Long, _
   ByVal y As Long, _
   ByVal cx As Long, _
   ByVal cy As Long, _
   ByVal fuFlags As Long) As Long

' Missing Draw State constants declarations:
'/* Image type */
Private Const DST_COMPLEX = &H0
Private Const DST_TEXT = &H1
Private Const DST_PREFIXTEXT = &H2
Private Const DST_ICON = &H3
Private Const DST_BITMAP = &H4

' /* State type */
Private Const DSS_NORMAL = &H0
Private Const DSS_UNION = &H10
Private Const DSS_DISABLED = &H20
Private Const DSS_MONO = &H80
Private Const DSS_RIGHT = &H8000

' Create a new icon based on an image list icon:
Private Declare Function ImageList_GetIcon Lib "COMCTL32.DLL" ( _
        ByVal hIml As Long, _
        ByVal i As Long, _
        ByVal diIgnore As Long _
    ) As Long
' Draw an item in an ImageList:
Private Declare Function ImageList_Draw Lib "COMCTL32.DLL" ( _
        ByVal hIml As Long, _
        ByVal i As Long, _
        ByVal hdcDst As Long, _
        ByVal x As Long, _
        ByVal y As Long, _
        ByVal fStyle As Long _
    ) As Long
' Get the icon's Background Color:
Private Declare Function ImageList_GetBkColor Lib "COMCTL32.DLL" ( _
        ByVal hIml As Long _
    ) As Long
        
' Draw an item in an ImageList with more control over positioning
' and colour:
Private Declare Function ImageList_DrawEx Lib "COMCTL32.DLL" ( _
      ByVal hIml As Long, _
      ByVal i As Long, _
      ByVal hdcDst As Long, _
      ByVal x As Long, _
      ByVal y As Long, _
      ByVal dx As Long, _
      ByVal dy As Long, _
      ByVal rgbBk As Long, _
      ByVal rgbFg As Long, _
      ByVal fStyle As Long _
   ) As Long
' Built in ImageList drawing methods:
Private Const ILD_NORMAL = 0
Private Const ILD_TRANSPARENT = 1
Private Const ILD_BLEND25 = 2
Private Const ILD_SELECTED = 4
Private Const ILD_FOCUS = 4
Private Const ILD_OVERLAYMASK = 3840
' Use default rgb colour:
Public Const CLR_NONE = -1
Public Const CLR_DEFAULT As Long = &HFF000000
Private Declare Function DestroyIcon Lib "user32" (ByVal hIcon As Long) As Long
Public Declare Function ImageList_GetIconSize Lib "COMCTL32" (ByVal hImageList As Long, cx As Long, cy As Long) As Long
Public Declare Function ImageList_GetImageCount Lib "COMCTL32" (ByVal hImageList As Long) As Long

' Standard GDI draw icon function:
Private Declare Function DrawIconEx Lib "user32" (ByVal hdc As Long, ByVal xLeft As Long, ByVal yTop As Long, ByVal hIcon As Long, ByVal cxWidth As Long, ByVal cyWidth As Long, ByVal istepIfAniCur As Long, ByVal hbrFlickerFreeDraw As Long, ByVal diFlags As Long) As Long
Private Const DI_MASK = &H1
Private Const DI_IMAGE = &H2
Private Const DI_NORMAL = &H3
Private Const DI_COMPAT = &H4
Private Const DI_DEFAULTSIZE = &H8

Public Declare Function LoadImageByNum Lib "user32" Alias "LoadImageA" (ByVal hInst As Long, ByVal lpsz As Long, ByVal un1 As Long, ByVal n1 As Long, ByVal n2 As Long, ByVal un2 As Long) As Long
    Public Const LR_LOADMAP3DCOLORS = &H1000
    Public Const LR_LOADFROMFILE = &H10
    Public Const LR_LOADTRANSPARENT = &H20
    Public Const IMAGE_BITMAP = 0

Public Declare Sub CopyMemory Lib "kernel32" Alias "RtlMoveMemory" ( _
    lpvDest As Any, lpvSource As Any, ByVal cbCopy As Long)
Public Declare Function ClientToScreen Lib "user32" (ByVal hwnd As Long, lpPoint As POINTAPI) As Long
Public Declare Function ScreenToClient Lib "user32" (ByVal hwnd As Long, lpPoint As POINTAPI) As Long
Public Declare Function GetWindowLong Lib "user32" Alias "GetWindowLongA" (ByVal hwnd As Long, ByVal nIndex As Long) As Long
Public Declare Function SetWindowLong Lib "user32" Alias "SetWindowLongA" (ByVal hwnd As Long, ByVal nIndex As Long, ByVal dwNewLong As Long) As Long
Public Const GWL_EXSTYLE = (-20)
Public Const GWL_STYLE = (-16)
Public Const WS_EX_WINDOWEDGE = &H100
Public Const WS_EX_CLIENTEDGE = &H200
Public Const WS_EX_STATICEDGE = &H20000
Public Const WS_HSCROLL = &H100000
Public Const WS_VSCROLL = &H200000

Public Declare Function SetWindowPos Lib "user32" (ByVal hwnd As Long, ByVal hWndInsertAfter As Long, ByVal x As Long, ByVal y As Long, ByVal cx As Long, ByVal cy As Long, ByVal wFlags As Long) As Long
Public Enum ESetWindowPosStyles
    SWP_SHOWWINDOW = &H40
    SWP_HIDEWINDOW = &H80
    SWP_FRAMECHANGED = &H20 ' The frame changed: send WM_NCCALCSIZE
    SWP_NOACTIVATE = &H10
    SWP_NOCOPYBITS = &H100
    SWP_NOMOVE = &H2
    SWP_NOOWNERZORDER = &H200 ' Don't do owner Z ordering
    SWP_NOREDRAW = &H8
    SWP_NOREPOSITION = SWP_NOOWNERZORDER
    SWP_NOSIZE = &H1
    SWP_NOZORDER = &H4
    SWP_DRAWFRAME = SWP_FRAMECHANGED
    HWND_NOTOPMOST = -2
End Enum
' Window relationship functions:
Public Declare Function SetParent Lib "user32" (ByVal hWndChild As Long, ByVal hWndNewParent As Long) As Long
Public Declare Function GetParent Lib "user32" (ByVal hwnd As Long) As Long
' Message functions:
Public Declare Function SendMessageByString Lib "user32" Alias "SendMessageA" (ByVal hwnd As Long, ByVal wMsg As Long, ByVal wParam As Long, ByVal lParam As String) As Long
Public Declare Function SendMessageByLong Lib "user32" Alias "SendMessageA" (ByVal hwnd As Long, ByVal wMsg As Long, ByVal wParam As Long, ByVal lParam As Long) As Long
Public Declare Function SendMessage Lib "user32" Alias "SendMessageA" (ByVal hwnd As Long, ByVal wMsg As Long, ByVal wParam As Long, lParam As Any) As Long
Public Declare Function PostMessage Lib "user32" Alias "PostMessageA" (ByVal hwnd As Long, ByVal wMsg As Long, ByVal wParam As Long, ByVal lParam As Long) As Long

Public Const WM_KEYDOWN = &H100
Public Const WM_KEYUP = &H101


Public Sub DrawDragImage( _
      ByRef rcNew As RECT, _
      ByVal bFirst As Boolean, _
      ByVal bLast As Boolean _
   )
Static rcCurrent As RECT
Dim hdc As Long
   
   ' First get the Desktop DC:
   hdc = CreateDCAsNull("DISPLAY", ByVal 0&, ByVal 0&, ByVal 0&)
   ' Set the draw mode to XOR:
   SetROP2 hdc, R2_NOTXORPEN
   
   '// Draw over and erase the old rectangle
   If Not (bFirst) Then
      Rectangle hdc, rcCurrent.Left, rcCurrent.Top, rcCurrent.Right, rcCurrent.Bottom
   End If
   
   If Not (bLast) Then
      '// Draw the new rectangle
      Rectangle hdc, rcNew.Left, rcNew.Top, rcNew.Right, rcNew.Bottom
   End If
   
   ' Store this position so we can erase it next time:
   LSet rcCurrent = rcNew
   
   ' Free the reference to the Desktop DC we got (make sure you do this!)
   DeleteDC hdc
    
End Sub

Public Sub DrawImage( _
      ByVal hIml As Long, _
      ByVal iIndex As Long, _
      ByVal hdc As Long, _
      ByVal xPixels As Integer, _
      ByVal yPixels As Integer, _
      ByVal lIconSizeX As Long, ByVal lIconSizeY As Long, _
      Optional ByVal bSelected = False, _
      Optional ByVal bCut = False, _
      Optional ByVal bDisabled = False, _
      Optional ByVal oCutDitherColour As OLE_COLOR = vbWindowBackground, _
      Optional ByVal hExternalIml As Long = 0, _
      Optional ByVal lBgColor As Long = CLR_NONE _
    )
Dim hIcon As Long
Dim lFlags As Long
Dim lhIml As Long
Dim lColor As Long
Dim iImgIndex As Long

   ' Draw the image at 1 based index or key supplied in vKey.
   ' on the hDC at xPixels,yPixels with the supplied options.
   ' You can even draw an ImageList from another ImageList control
   ' if you supply the handle to hExternalIml with this function.
   
   iImgIndex = iIndex
   If (iImgIndex > -1) Then
      If (hExternalIml <> 0) Then
          lhIml = hExternalIml
      Else
          lhIml = hIml
      End If
      
      lFlags = ILD_TRANSPARENT
      If (bSelected) Or (bCut) Then
          lFlags = lFlags Or ILD_SELECTED
      End If
      
      If (bCut) Then
        ' Draw dithered:
        lColor = TranslateColor(oCutDitherColour)
        If (lColor = -1) Then lColor = TranslateColor(vbWindowBackground)
        ImageList_DrawEx _
              lhIml, _
              iImgIndex, _
              hdc, _
              xPixels, yPixels, 0, 0, _
              lBgColor, lColor, _
              lFlags
      ElseIf (bDisabled) Then
        ' extract a copy of the icon:
        hIcon = ImageList_GetIcon(hIml, iImgIndex, 0)
        ' Draw it disabled at x,y:
        DrawState hdc, 0, 0, hIcon, 0, xPixels, yPixels, lIconSizeX, lIconSizeY, DST_ICON Or DSS_DISABLED
        ' Clear up the icon:
        DestroyIcon hIcon
              
      Else
        ' Standard draw:
        ImageList_DrawEx _
            lhIml, _
            iImgIndex, _
            hdc, _
            xPixels, _
            yPixels, _
            0, 0, _
            lBgColor, _
            CLR_NONE, _
            lFlags
      End If
   End If
End Sub


Public Function TranslateColor(ByVal oClr As OLE_COLOR, _
                        Optional hPal As Long = 0) As Long
    ' Convert Automation color to Windows color
    If OleTranslateColor(oClr, hPal, TranslateColor) Then
        TranslateColor = CLR_INVALID
    End If
End Function

Public Sub pOLEFontToLogFont(fntThis As StdFont, hdc As Long, tLF As LOGFONT)
Dim sFont As String
Dim iChar As Integer

   ' Convert an OLE StdFont to a LOGFONT structure:
   With tLF
       sFont = fntThis.Name
       ' There is a quicker way involving StrConv and CopyMemory, but
       ' this is simpler!:
       For iChar = 1 To Len(sFont)
           .lfFaceName(iChar - 1) = CByte(Asc(Mid$(sFont, iChar, 1)))
       Next iChar
       ' Based on the Win32SDK documentation:
       .lfHeight = -MulDiv((fntThis.Size), (GetDeviceCaps(hdc, LOGPIXELSY)), 72)
       .lfItalic = fntThis.Italic
       If (fntThis.Bold) Then
           .lfWeight = FW_BOLD
       Else
           .lfWeight = FW_NORMAL
       End If
       .lfUnderline = fntThis.Underline
       .lfStrikeOut = fntThis.Strikethrough
       .lfCharSet = fntThis.Charset
   End With

End Sub
Public Sub TileArea( _
        ByVal hdc As Long, _
        ByVal x As Long, _
        ByVal y As Long, _
        ByVal Width As Long, _
        ByVal Height As Long, _
        ByVal lSrcDC As Long, _
        ByVal lBitmapW As Long, _
        ByVal lBitmapH As Long, _
        ByVal lSrcOffsetX As Long, _
        ByVal lSrcOffsetY As Long _
    )
Dim lSrcX As Long
Dim lSrcY As Long
Dim lSrcStartX As Long
Dim lSrcStartY As Long
Dim lSrcStartWidth As Long
Dim lSrcStartHeight As Long
Dim lDstX As Long
Dim lDstY As Long
Dim lDstWidth As Long
Dim lDstHeight As Long

    lSrcStartX = ((x + lSrcOffsetX) Mod lBitmapW)
    lSrcStartY = ((y + lSrcOffsetY) Mod lBitmapH)
    lSrcStartWidth = (lBitmapW - lSrcStartX)
    lSrcStartHeight = (lBitmapH - lSrcStartY)
    lSrcX = lSrcStartX
    lSrcY = lSrcStartY
    
    lDstY = y
    lDstHeight = lSrcStartHeight
    
    Do While lDstY < (y + Height)
        If (lDstY + lDstHeight) > (y + Height) Then
            lDstHeight = y + Height - lDstY
        End If
        lDstWidth = lSrcStartWidth
        lDstX = x
        lSrcX = lSrcStartX
        Do While lDstX < (x + Width)
            If (lDstX + lDstWidth) > (x + Width) Then
                lDstWidth = x + Width - lDstX
                If (lDstWidth = 0) Then
                    lDstWidth = 4
                End If
            End If
            'If (lDstWidth > Width) Then lDstWidth = Width
            'If (lDstHeight > Height) Then lDstHeight = Height
            BitBlt hdc, lDstX, lDstY, lDstWidth, lDstHeight, lSrcDC, lSrcX, lSrcY, vbSrcCopy
            lDstX = lDstX + lDstWidth
            lSrcX = 0
            lDstWidth = lBitmapW
        Loop
        lDstY = lDstY + lDstHeight
        lSrcY = 0
        lDstHeight = lBitmapH
    Loop
End Sub



Public Property Get ObjectFromPtr(ByVal lPtr As Long) As Object
Dim oTemp As Object
   ' Turn the pointer into an illegal, uncounted interface
   CopyMemory oTemp, lPtr, 4
   ' Do NOT hit the End button here! You will crash!
   ' Assign to legal reference
   Set ObjectFromPtr = oTemp
   ' Still do NOT hit the End button here! You will still crash!
   ' Destroy the illegal reference
   CopyMemory oTemp, 0&, 4
   ' OK, hit the End button if you must--you'll probably still crash,
   ' but it will be because of the subclass, not the uncounted reference
End Property


Public Sub debugmsg(ByVal sMsg As String)
#If DEBUGMODE = 1 Then
   MsgBox sMsg
#Else
   Debug.Print sMsg
#End If
End Sub
