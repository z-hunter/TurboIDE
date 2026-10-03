#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include "coniow.h"

#define MAX_BUFFER 250

static struct text_info ti= {1,1,80,25,7,7,0,25,80,1,1,{0}};

int _directvideo;
int _wscroll=1;
volatile int _mousex;
volatile int _mousey;
volatile int _mousebuttons;
volatile int _controlkeystate;
static int ti_flag=0;
#define KEYBUF_SIZE 100
static int keybuf[KEYBUF_SIZE];
static int nkeybuf=0;
static int keystate=0;
const int UNICODE=0x40000;
#include <string.h>
static BOOL WINAPI HandlerRoutine(DWORD dwCtrlType)
{  if (dwCtrlType==0)
   {
      if (nkeybuf<KEYBUF_SIZE)
         keybuf[nkeybuf++]=3;
      return TRUE;
   }
   else if (dwCtrlType==2)
   {
      if (nkeybuf<KEYBUF_SIZE)
         keybuf[nkeybuf++]=WINDOW_CLOSE;
      return FALSE;
   }
   return FALSE;
}
static HANDLE con_win=INVALID_HANDLE_VALUE;
static HANDLE con_in=INVALID_HANDLE_VALUE;
static HANDLE con_out=INVALID_HANDLE_VALUE;
static void init_ti(void)
{
   if (ti_flag) return;
   ti_flag=1;
   con_win=GetConsoleWindow();
   if (!con_win) {
      AllocConsole();
      con_win=GetConsoleWindow();
   }
   if (!IsWindowVisible(con_win))
   {
      ShowWindow(con_win, SW_SHOW);
      SetActiveWindow(con_win);
   }
//  HMENU hMenu = GetSystemMenu(con_win, FALSE);
//  if (hMenu != NULL) DeleteMenu(hMenu, SC_CLOSE, MF_BYCOMMAND);
   SECURITY_ATTRIBUTES attr= {sizeof(SECURITY_ATTRIBUTES),NULL,1};
   con_out=GetStdHandle(STD_OUTPUT_HANDLE);
   DWORD mode;
   if (!GetConsoleMode(con_out,&mode))
   {
      con_out=CreateFileA("CONOUT$",GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,&attr,OPEN_EXISTING,0,0);
      SetStdHandle(STD_OUTPUT_HANDLE,con_out);
   }
   con_in=GetStdHandle(STD_INPUT_HANDLE);
   if (!GetConsoleMode(con_in,&mode))
   {
      con_in=CreateFileA("CONIN$",GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,&attr,OPEN_EXISTING,0,0);
      SetStdHandle(STD_INPUT_HANDLE,con_in);
   }
   CONSOLE_SCREEN_BUFFER_INFO info;
   GetConsoleScreenBufferInfo(con_out, &info);
   ti.normattr = info.wAttributes;
   ti.winright  = info.dwSize.X;
   ti.winbottom = info.dwSize.Y;
   ti.screenwidth  = info.dwSize.X;
   ti.screenheight = info.dwSize.Y;
   ti.curx = info.dwCursorPosition.X+1;
   ti.cury = info.dwCursorPosition.Y+1;
   ti.attribute = info.wAttributes;
   GetConsoleTitleA(ti.title,sizeof(ti.title));
   UINT acp=GetACP();
   if (GetConsoleOutputCP()!=acp)
   {
      SetConsoleOutputCP(acp);
      SetConsoleCP(acp);
   }
   SetConsoleCtrlHandler(HandlerRoutine,TRUE);
   SetConsoleMode(con_in, ((mode& ~ENABLE_QUICK_EDIT_MODE)|ENABLE_EXTENDED_FLAGS|ENABLE_WINDOW_INPUT|ENABLE_MOUSE_INPUT|ENABLE_ECHO_INPUT));
}
static int in_window(int x, int y)
{  return !(x<ti.winleft || y<ti.wintop || x>ti.winright || y>ti.winbottom);
}
static void putchxyattr(int x, int y, int ch, int attr, int unc)
{
   if (!in_window(x,y)) return;
   CHAR_INFO buffer;
   COORD c= {0,0},s= {1,1};
   SMALL_RECT r={x-1, y-1, x-1, y-1};
   if (unc)
      buffer.Char.UnicodeChar = ch;
   else
      buffer.Char.AsciiChar = ch;
   if(attr&0x300)
   { WORD oldattr=attr;
      DWORD nread;
      COORD ca={x-1, y-1};
      ReadConsoleOutputAttribute(con_out,&oldattr,1,ca,&nread);
      if(attr&0x200) attr=(attr&0xFDF0)|(oldattr&0xF);
      if(attr&0x100) attr=(attr&0xFC0F)|(oldattr&0xF0);
   }
   buffer.Attributes = attr;
   (unc?WriteConsoleOutputW:WriteConsoleOutputA)(con_out, &buffer, s, c, &r);
}
static void putchxyattrwh(int x, int y, int ch, int attr, int w, int h, int unc)
{
   if (x<ti.winleft)
      w-=ti.winleft-x;
   if (y<ti.wintop)
      h-=ti.wintop-y;
   if (x+w-1>ti.winright)
      w=ti.winright-x+1;
   if (y+h-1>ti.winbottom)
      h=ti.winbottom-y+1;
   if (w<=0 || h<=0) return;
   for (int i=0; i<h; ++i)
   {
      DWORD written;
      COORD c={x-1, y-1+i};
      if(attr&0x300) {
         WORD oldattr[w];
         ReadConsoleOutputAttribute(con_out,oldattr,w,c,&written);
         for(int j=0;j<w;++j)
         {
            if((attr&0x300)==0x300) oldattr[j]=(attr&0xFC00)|(oldattr[j]&0xFF);
            else if(attr&0x200) oldattr[j]=(attr&0xFCF0)|(oldattr[j]&0xF);
            else oldattr[j]=(attr&0xFC0F)|(oldattr[j]&0xF0);
         }
         WriteConsoleOutputAttribute(con_out,oldattr,w,c,&written);
      }
      else
         FillConsoleOutputAttribute(con_out,attr,w,c,&written);
      if(unc)
         FillConsoleOutputCharacterW(con_out,ch,w,c,&written);
      else
         FillConsoleOutputCharacterA(con_out,ch,w,c,&written);
   }
}
wchar_t ansi2unicode(char ch)
{
   char ansistr[2];
   wchar_t unicodestr[2];
   ansistr[0]=ch;
   ansistr[1]=0;
   unicodestr[0]=0;
   MultiByteToWideChar(CP_ACP, 0, ansistr, 1, unicodestr, 1);
   return unicodestr[0];
}
char unicode2ansi(wchar_t ch)
{
   char ansistr[2];
   wchar_t unicodestr[2];
   ansistr[0]=(char)ch;
   unicodestr[0]=ch;
   unicodestr[1]=0;
   WideCharToMultiByte(CP_ACP, 0, unicodestr, 1, ansistr, 1, NULL, NULL);
   return ansistr[0];
}
void clreol(void)
{
   init_ti();
   putchxyattrwh(ti.winleft+ti.curx-1,ti.wintop+ti.cury-1,' ',ti.attribute,ti.winright-ti.winleft+2-ti.curx,1,0);
   gotoxy(ti.curx, ti.cury);
}
void clrscr(void)
{
   init_ti();
   putchxyattrwh(ti.winleft,ti.wintop,' ',ti.attribute,ti.winright-ti.winleft+1,ti.winbottom-ti.wintop+1,0);
   gotoxy(1, 1);
}
void clrarea( int left, int top, int right, int bottom) {
   init_ti();
   putchxyattrwh(ti.winleft+left-1,ti.wintop+top-1,' ',ti.attribute,right-left+1,bottom-top+1,0);
}
static void scroll(int dir)
{
   init_ti();
   COORD c={ ti.winleft-1, ti.wintop-1 + ti.cury -dir};
   SMALL_RECT r= { ti.winleft-1,  ti.wintop-1+ ti.cury, ti.winright - 1, ti.winbottom - 2+dir};
   CHAR_INFO fc={{.UnicodeChar = L' '}, ti.attribute&0xFCFF};
   ScrollConsoleScreenBufferW(con_out, &r, NULL, c, &fc);
   gotoxy(ti.curx, ti.cury);
}
void delline(void)
{ scroll(1);
}
void insline(void)
{ scroll(0);
}
static int ugettext(int left, int top, int right, int bottom,  void *destin, int unc)
{
   if (right<left || bottom<top) return 0;
   init_ti();
   if(left<=0 || top<=0 || right>ti.screenwidth || bottom>ti.screenheight) return 0;
   SMALL_RECT r={left-1,top-1,right - 1,bottom - 1};
   COORD  s = { right - left + 1, bottom - top + 1};
   COORD c= {0,0};
   CHAR_INFO buffer [s.X * s.Y];
   if(!(unc?ReadConsoleOutputW:ReadConsoleOutputA)(con_out, buffer, s, c, &r)) return 0;
   for (int i = 0; i < s.X * s.Y; i++)
   {
      if(unc)
      { wchar_info* buf=(wchar_info *)destin;
        buf[i].letter = buffer[i].Char.UnicodeChar;
        buf[i].attr = buffer[i].Attributes;
      }
      else
      { char_info* buf=(char_info *)destin;
        buf[i].letter = buffer[i].Char.AsciiChar;
        buf[i].attr = buffer[i].Attributes;
      }
   }
   return 1;
}
int getwtext(int left, int top,  int right, int bottom,  wchar_info *destin)
{  return ugettext(left,top,right,bottom,destin, 1);
}
int _conio_gettext(int left, int top, int right, int bottom,  char_info *destin)
{  return ugettext(left,top,right,bottom,destin, 0);
}
const struct text_info *gettextinfo(struct text_info *r)
{
   init_ti();
   if (r) *r=ti;
   return &ti;
}
static void printattr(int attr)
{
  if(attr&0x300)
    attr=(attr&0xFC00)|(ti.normattr&0xFF);
  SetConsoleTextAttribute(con_out, attr);
}
void settextinfo(const struct text_info *r)
{
   init_ti();
   if (strcmp(ti.title,r->title)!=0)
      SetConsoleTitleA(r->title);
   if (ti.screenwidth!=r->screenwidth || ti.screenheight!=r->screenheight)
   {  COORD sizes;
      sizes.X=r->screenwidth;
      sizes.Y=r->screenheight;
      SetConsoleScreenBufferSize(con_out,sizes);
   }
   if (ti.winleft+ti.curx!=r->winleft+r->curx ||
         ti.wintop+ti.cury!=r->wintop+r->cury)
   {
      COORD c;
      c.X = r->winleft+r->curx-2;
      c.Y = r->wintop+r->cury-2;
      SetConsoleCursorPosition(con_out, c);
   }
   if (ti.attribute!=r->attribute)
      printattr(r->attribute);
   ti=*r;
}

void gotoxy(int x, int y)
{
   init_ti();
   if (!in_window(ti.winleft+x-1,ti.wintop+y-1)) return;
   ti.curx=x;
   ti.cury=y;
   COORD c={ti.winleft+ti.curx-2, ti.wintop+ti.cury-2};
   SetConsoleCursorPosition(con_out, c);
}
void highvideo(void)
{  textattr(ti.attribute|0x8);
}
void lowvideo(void)
{  textattr(ti.attribute&~0x8);
}
int movetext(int left, int top, int right, int bottom, int destleft, int desttop)
{
   init_ti();
   COORD s={right - left + 1,bottom - top + 1};
   if (s.X<=0 || s.Y<=0) return 0;
   if(left<=0 || top<=0 || right>ti.screenwidth || bottom>ti.screenheight) return 0;
   if(destleft<=0 || desttop<=0 || destleft+s.X-1>ti.screenwidth || desttop+s.Y-1>ti.screenheight) return 0;
   CHAR_INFO buffer[s.X * s.Y];
   COORD c= {0,0};
   SMALL_RECT r={left-1,top-1,right-1,bottom-1};
   if (ReadConsoleOutputW(con_out,buffer, s, c, &r))
      WriteConsoleOutputW(con_out,buffer, s, c, &r);
   return 1;
}
void normvideo(void)
{
   init_ti();
   textattr(ti.normattr);
}
static int uputtext(int left, int top, int right, int bottom,  const void *source, int unc)
{
   if (right<left || bottom<top) return 0;
   init_ti();
   if(left<=0 || top<=0 || right>ti.screenwidth || bottom>ti.screenheight) return 0;
   SMALL_RECT r={left-1,top-1,right - 1,bottom - 1};
   COORD  s = { right - left + 1, bottom - top + 1};
   COORD c= {0,0};
   CHAR_INFO buffer [s.X * s.Y];
   for (int i = 0; i < s.X * s.Y; i++)
   {
      if(unc)
      { wchar_info* buf=(wchar_info *)source;
        buffer[i].Char.UnicodeChar=buf[i].letter;
        buffer[i].Attributes=buf[i].attr;
      }
      else
      { char_info* buf=(char_info *)source;
        buffer[i].Char.AsciiChar=buf[i].letter;
        buffer[i].Attributes=buf[i].attr;
      }
   }
   if(!(unc?WriteConsoleOutputW:WriteConsoleOutputA)(con_out, buffer, s, c, &r)) return 0;
   return 1;
}
int putwtext(int left, int top, int right, int bottom, const wchar_info *source)
{  return uputtext(left,top,right,bottom,source, 1);
}
int puttext(int left, int top, int right, int bottom, const char_info *source)
{  return uputtext(left,top,right,bottom,source, 0);
}
int to_attr(int tcolor, int bgcolor, int other)
{ int attr=(tcolor&0xF)|((bgcolor&0xF)<<4)|(other&0xFC00);
  if(tcolor==NO_COLOR) attr|=0x200;
  if(bgcolor==NO_COLOR) attr|=0x100;
   return attr;
}
void textattr(int newattr)
{
   init_ti();
   printattr(ti.attribute=newattr);
}
void textbackground(int newcolor)
{  textattr((ti.attribute&0xFE0F)|((newcolor&0x1F)<<4));
}
void textcolor(int newcolor)
{  textattr(newcolor==NO_COLOR?(ti.attribute|0x200):((ti.attribute&0xFDF0)|(newcolor&0xF)));
}
void textmode(int newmode)
{  (void)newmode;
}
void window(int left, int top, int right, int bottom)
{
   init_ti();
   if (left<=0 || top<=0 || right>ti.screenwidth || bottom>ti.screenheight ||
     left>right || top>bottom ) return;
   ti.winleft=left;
   ti.winright=right;
   ti.wintop=top;
   ti.winbottom=bottom;
   gotoxy(1,1);
}
void _setcursortype(int cur_t)
{
   CONSOLE_CURSOR_INFO Info;
   if (cur_t == _NOCURSOR)
      Info.bVisible = FALSE;
   else {
      Info.bVisible = TRUE;
      Info.dwSize = cur_t;
   }
   SetConsoleCursorInfo(con_out, &Info);
}
#define CPRINT(ctype,cprintf,vsnprintf,cputs) int cprintf(const ctype *format, ...)\
{  ctype buffer[MAX_BUFFER];\
   va_list ap;\
   va_start(ap, format);\
   int r=vsnprintf(buffer, MAX_BUFFER, format, ap);\
   va_end(ap);\
   cputs(buffer);\
   return r;\
}
CPRINT(char,cprintf,vsnprintf,cputs)
CPRINT(wchar_t,cwprintf,_vsnwprintf,cputws)

int cputs(const char *str)
{
   while(*str)
     putch(*(str++));
   return 0;
}
int cputws(const wchar_t *str)
{
   while(*str)
     putwch(*(str++));
   return 0;
}
int uputch(int c, int unc)
{
   init_ti();
   switch (c)
   {
   case '\r':
      gotoxy(1,ti.cury);
      break;
   case '\n':
      if (ti.cury<ti.winbottom-ti.wintop+1)
         gotoxy(ti.curx,ti.cury+1);
      else
      {  int x,y;
         x=ti.curx;
         y=ti.cury;
         gotoxy(1,1);
         delline();
         gotoxy(x,y);
      }
      break;
   case '\b':
      { int x=ti.curx, y=ti.cury;
        if (ti.curx > 1) {x=ti.curx-1; y=ti.cury; }
        else { x=ti.winright-ti.winleft+1;
          if(ti.cury>1 && _wscroll) y=ti.cury-1;
        }
        putchxyattr(ti.winleft+x-1, ti.wintop+y-1,' ',ti.attribute,unc);
        gotoxy(x,y);
      }
      break;
   default:
      putchxyattr(ti.winleft+ti.curx-1, ti.wintop+ti.cury-1,c,ti.attribute,unc);
      if (ti.curx+1>ti.winright-ti.winleft+1)
      {
         if (_wscroll)
         {
            if (ti.cury<ti.winbottom-ti.wintop+1)
               gotoxy(1,ti.cury+1);
            else
            {  int y;
               y=ti.cury;
               gotoxy(1,1);
               delline();
               gotoxy(1,y);
            }
         }
         else
           gotoxy(1,ti.cury);
      }
      else
        gotoxy(ti.curx+1,ti.cury);
   }
   return c;
}
static int ugetch(int unc, int echo)
{  int ch;
   if (keystate!=0)
   {  ch=keystate;
      keystate=0;
      return ch;
   }
   while (1)
   {  ch=getkbm();
      if ((ch&MOUSE_LCLICK)==0)
         break;
   }
   if (ch&KEY_SPECIAL)
   {
      keystate=ch&0xFF;
      return 0;
   }
   ch=(ch&UNICODE)?(unc?ch:unicode2ansi(ch)):(unc?ansi2unicode(ch):ch);
   if(echo) uputch(ch,unc);
   return ch;
}

wchar_t getwch(void)
{  return ugetch(1,0);
}
int getch(void)
{  return ugetch(0,0);
}
int wherex(void)
{
   init_ti();
   return ti.curx;
}
int wherey(void)
{
   init_ti();
   return ti.cury;
}
void _conio_delay(int ms)
{
   init_ti();
   Sleep(ms);
}

wint_t  putwch(wchar_t c)
{ return uputch(c&0xFFFF,1);
}
int putch(int c)
{ return uputch(c&0xFF,0);
}
int getche(void)
{ return ugetch(0,1);
}
wchar_t  getwche(void)
{ return ugetch(1,1);
}
int ungetch(int ch)
{
   if (nkeybuf<KEYBUF_SIZE)
   {  keybuf[nkeybuf++]=ch!=0 && (ch&0x30000)==0?ch&0xFF:ch;
      return ch;
   }
   return EOF;
}
wchar_t ungetwch(wchar_t ch)
{
   if (nkeybuf<KEYBUF_SIZE)
   {  keybuf[nkeybuf++]=ch!=0 && (ch&0x30000)==0?((ch&0xFFFF)|UNICODE):ch;
      return ch;
   }
   return WEOF;
}
static int ucgets(void *str, int maxlen, int unc, int pass)
{
   int length = 0;
   int ch = 0;
   init_ti();
   while(ch!='\r')
   {
      ch = ugetch(unc,0);
      if (ch==0)
        ugetch(unc,0);
      else
      {
         switch (ch)
         {
         case '\n':
            ch='\r';
            break;
         case '\r':
            break;
         case '\b':
            if (length > 0) {
              uputch('\b',unc);
              --length;
            }
            break;
         default:
            if (length < maxlen)
            {
               uputch(pass?'*':ch,unc);
               if(unc)
                 ((wchar_t*)str)[length] = ch;
               else
                 ((char*)str)[length] = ch;
            }
         }
      }
   }
   return length;
}
wchar_t *cgetws(wchar_t *str)
{
   if (str==NULL) return NULL;
   int len=ucgets(str+2,(int)(str[0])-1,1,0);
   str[1]=len;
   str[2+len]=0;
   return str+2;
}
char *cgets(char *str)
{
   if (str==NULL) return NULL;
   int len=ucgets(str+2,(int)((unsigned char)str[0])-1,0,0);
   str[1]=len;
   str[2+len]=0;
   return str+2;
}
char *getpass(const char *prompt)
{
   static char str[PASS_MAX+1];
   cputs(prompt);
   int len=ucgets(str,PASS_MAX,0,0);
   str[len]=0;
   return str;
}
#define CSCANF(ctype,cscanf,vsscanf,unc) int cscanf(const ctype *format, ...)\
{  ctype buffer[MAX_BUFFER+1];\
   va_list ap;\
   ucgets(buffer,MAX_BUFFER,unc,0);\
   va_start(ap, format);\
   int r=vsscanf(buffer, format, ap);\
   va_end(ap);\
   return r;\
}
CSCANF(char,cscanf,vsscanf,0)
CSCANF(wchar_t,cwscanf,vswscanf,1)

static int input_process()
{
  INPUT_RECORD buf;
  DWORD nevents=0;
  ReadConsoleInputA(con_in,&buf,1,&nevents);
  if (buf.EventType==KEY_EVENT && buf.Event.KeyEvent.bKeyDown)
  {
     _controlkeystate=buf.Event.KeyEvent.dwControlKeyState;
     if (buf.Event.KeyEvent.uChar.AsciiChar==0)
        return KEY_SPECIAL+buf.Event.KeyEvent.wVirtualScanCode;
     return buf.Event.KeyEvent.uChar.AsciiChar&0xFF;
  }
  else if (buf.EventType==WINDOW_BUFFER_SIZE_EVENT)
  {
     ti.screenwidth=buf.Event.WindowBufferSizeEvent.dwSize.X;
     ti.screenheight=buf.Event.WindowBufferSizeEvent.dwSize.Y;
     if (ti.winright>ti.screenwidth) ti.winright=ti.screenwidth;
     if (ti.winbottom>ti.screenheight) ti.winbottom=ti.screenheight;
     if (ti.winleft>ti.winright) ti.winleft=ti.winright;
     if (ti.wintop>ti.winbottom) ti.wintop=ti.winbottom;
     if (!in_window(ti.winleft+ti.curx-1,ti.wintop+ti.cury-1))
        gotoxy(1,1);
     return WINDOW_RESIZE;
  }
  else if (buf.EventType==MOUSE_EVENT)
  {

     switch (buf.Event.MouseEvent.dwEventFlags)
     {
     case 0:
        if (buf.Event.MouseEvent.dwButtonState != 0)
        {
           _mousebuttons=buf.Event.MouseEvent.dwButtonState;
           _mousex=buf.Event.MouseEvent.dwMousePosition.X+1;
           _mousey=buf.Event.MouseEvent.dwMousePosition.Y+1;
           return _mousebuttons&1?MOUSE_LCLICK:MOUSE_RCLICK;
        }
        break;
     case DOUBLE_CLICK:
        {
           _mousebuttons=buf.Event.MouseEvent.dwButtonState;
           _mousex=buf.Event.MouseEvent.dwMousePosition.X+1;
           _mousey=buf.Event.MouseEvent.dwMousePosition.Y+1;
           return _mousebuttons&1?MOUSE_LDBLCLICK:MOUSE_RDBLCLICK;
        }
     case MOUSE_WHEELED:
        _mousex=buf.Event.MouseEvent.dwMousePosition.X+1;
        _mousey=buf.Event.MouseEvent.dwMousePosition.Y+1;
        if ((int)buf.Event.MouseEvent.dwButtonState>0)
           return MOUSE_WHEELUP;
        return MOUSE_WHEELDOWN;
     default:
        break;
     }
  }
  return 0;
}
static int test_events(int events)
{
   init_ti();
   if (nkeybuf) return 1;
   if (keystate) return 1;
   while (1)
   {
      DWORD nevents=0;
      GetNumberOfConsoleInputEvents(con_in,&nevents);
      if (nevents==0) return 0;
      INPUT_RECORD buf;
      PeekConsoleInput(con_in,&buf,1,&nevents);
      if ((events&1) && buf.EventType==KEY_EVENT && buf.Event.KeyEvent.bKeyDown)
         return 1;
      if ((events&2) && buf.EventType==WINDOW_BUFFER_SIZE_EVENT)
         return 1;
      if ((events&4) && buf.EventType==MOUSE_EVENT)
      {
         switch (buf.Event.MouseEvent.dwEventFlags)
         {
         case 0:
            if (buf.Event.MouseEvent.dwButtonState != 0)
               return 1;
            break;
         case DOUBLE_CLICK:
         case MOUSE_WHEELED:
            return 1;
         default:
            break;
         }
      }
      input_process();
   }
}

int kbhit(void)
{
   return test_events(1);
}
int kbmhit(void)
{
   return test_events(7);
}
int getkbm(void)
{
   init_ti();
   if (nkeybuf)
      return keybuf[--nkeybuf];
   if (keystate) keystate=0;
   while (1)
   {
      DWORD nevents=0;
      while(nevents==0)
      {
         GetNumberOfConsoleInputEvents(con_in,&nevents);
         if (nevents==0) {
            if (nkeybuf)
               return keybuf[--nkeybuf];
            Sleep(20);
         }
      }
      int k=input_process();
      if(k) return k;
   }
}
static int line_mode(char x)
{
   static char line_modes[]=" -=#";
   for(int i=0;i<4;++i)
     if(x==line_modes[i]) return i;
   return 1;
}
static void drawchar(int x, int y, int vm, int hm, int angle[])
{
   if(vm==0 && hm==0) return;
   if((vm<2 && hm==0) || (hm>2 && vm>2)) return;
   int ch;
   if(vm==0 && hm>=3) ch=' ';
   else if(vm>=1 && vm<=2 && hm>=1 && hm<=2) ch=angle[vm*2+hm-3];
   else ch=angle[4];
   putchxyattr(x, y, ch, ti.attribute, 1);
}
static int char_mode(int c, int d)
{
  static struct char_modes {int c, vm, hm; } chm[]={
     {0x2502,1,0}, {0x2551,2,0},{0x2588,3,3},
     {0x2500,0,1}, {0x2550,0,2},{0x2584,0,3},{0x2580,0,4},
  };
  for(size_t i=0;i<sizeof(chm)/sizeof(chm[0]);++i)
    if(chm[i].c==c)
      return d?chm[i].hm:chm[i].vm;
  return 0;
}
static void drawline(int x, int y, int dx, int dy, int n, int ch, int repl)
{
   static int lh[5]={0x251c,0x255e,0x255F,0x2560,0x2588};
   static int rh[5]={0x2524,0x2561,0x2562,0x2563,0x2588};
   static int uv[5]={0x252c,0x2564,0x2565,0x2566,0x2584};
   static int dv[5]={0x2534,0x2567,0x2568,0x2569,0x2580};
   static int hv[5]={0x253c,0x256a,0x256b,0x256c,0x2588};
   if(ch==' ') return;
   int cm=char_mode(ch,dx);
   x+=ti.winleft-1;
   y+=ti.wintop-1;
   for(int i=0;i<n;++i)
   {
     if(repl)
       putchxyattr(x, y, ch, ti.attribute, 1);
     else
     {
       SMALL_RECT r={x-1,y-1,x-1,y-1};
       COORD  s = {1, 1};
       COORD c= {0,0};
       CHAR_INFO buffer;
       ReadConsoleOutputW(con_out, &buffer, s, c, &r);
       int am=char_mode(buffer.Char.UnicodeChar&0xFFFF,dy);
       if(am>=3)
         ;
       else if(am==0)
         putchxyattr(x, y, ch, ti.attribute, 1);
       else
         drawchar(x,y,dx?am:cm,dx?cm:am,(i==0)?(dx?lh:uv):((i==n-1)?(dx?rh:dv):hv));
     }
     x+=dx;
     y+=dy;
   }
}
void drawborder( int left, int top, int right, int bottom, const char *style)
{
   static int line_vert[4]={' ',0x2502,0x2551,0x2588};
   static int line_horz[6]={' ',0x2500,0x2550,0x2588,0x2584,0x2580};
   static int ul[5]={0x250C,0x2552, 0x2553,0x2554, 0x2584};
   static int dl[5]={0x2514,0x2558, 0x2559,0x255A, 0x2580};
   static int ur[5]={0x2510,0x2555, 0x2556,0x2557, 0x2584};
   static int dr[5]={0x2518,0x255B, 0x255C,0x255D, 0x2580};

   if(left>right || top>bottom) return;
   if(left<=0 || top<=0 || right>ti.winright-ti.winleft+1 || bottom>ti.winbottom-ti.wintop) return;
   if(left==right && top==bottom) return;
   int x=ti.curx;
   int y=ti.cury;
   if(left==right)
      drawline(left,top,0,1,bottom-top+1, line_vert[style?line_mode(style[0]):1],0);
   else if(top==bottom)
      drawline(left,top,1,0,right-left+1, line_horz[style?line_mode(style[0]):1],0);
   else
   { int lm=1,um=1,rm=1,dm=1;
      if(style)
      for(int i=0;i<4 && style[i];++i)
      { int m=line_mode(style[i]);
         if(i==0)
           lm=um=rm=dm=m;
         else if(i==1)
           um=dm=m;
         else if(i==2)
            rm=m;
         else
            dm=m;
      }
      drawline(left,top,0,1,bottom-top+1, line_vert[lm],1);
      drawline(right,top,0,1,bottom-top+1, line_vert[rm],1);
      drawline(left,top,1,0,right-left+1, line_horz[um==3?4:um],1);
      drawline(left,bottom,1,0,right-left+1, line_horz[dm==3?5:dm],1);

      drawchar(left+ti.winleft-1, top+ti.wintop-1, lm,um,ul);
      drawchar(left+ti.winleft-1, bottom+ti.wintop-1, lm,dm,dl);
      drawchar(right+ti.winleft-1, top+ti.wintop-1, rm,um,ur);
      drawchar(right+ti.winleft-1, bottom+ti.wintop-1, rm,dm,dr);
   }
   gotoxy(x,y);
}
