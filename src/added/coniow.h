#ifndef _CONIOW_H_
#define _CONIOW_H_

#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "keys_n_colors.h"
struct text_info {
  unsigned short winleft;
  unsigned short wintop;
  unsigned short winright;
  unsigned short winbottom;
  unsigned short attribute;
  unsigned short normattr;
  unsigned short currmode;
  unsigned short screenheight;
  unsigned short screenwidth;
  short curx;
  short cury;
  char title[256];
};

#define BLINK       128
#define PASS_MAX    8
enum _cursor_types { _NOCURSOR=0,_SOLIDCURSOR=100,_NORMALCURSOR=20};
enum _ext_attrs {
  UPPER_LINE=0x0400,
  LEFT_LINE=0x0800,
  RIGHT_LINE=0x1000,
  REVERSED_COLORS=0x4000,
  UNDER_LINE=0x8000
};

typedef struct char_info {
  unsigned char letter;
  unsigned char attr;
} char_info;
typedef struct wchar_info {
  wchar_t letter;
  unsigned short attr;
} wchar_info;

extern int _directvideo;
extern int _wscroll;

void    clreol( void );
void    clrscr( void );
void    delline( void );
int     _conio_gettext( int left, int top, int right, int bottom,  char_info* destin);
const struct text_info* gettextinfo(struct text_info *r);
void    settextinfo(const struct text_info *r);
void    gotoxy(int x, int y);
void    insline( void );
void    highvideo( void );
void    lowvideo( void );
void    normvideo( void );
int     movetext( int left, int top, int right, int bottom, int destleft, int desttop );
int     puttext( int left, int top,  int right, int bottom, const char_info* source );

int     to_attr(int tcolor, int bgcolor, int other);
void    textattr( int attr );
void    textbackground( int tcolor );
void    textcolor( int bgcolor );
void    textmode( int mode );
void    window( int left, int top, int right, int bottom);
void    drawborder( int left, int top, int right, int bottom, const char *style);
void    clrarea( int left, int top, int right, int bottom);

void          _setcursortype( int cur_t );
char *        cgets( char *str );
int           cprintf( const char *format, ... );
int           cputs( const char *str );
int           cscanf( const char *format, ... );
int           getch(void);
int           getche(void);
char *        getpass( const char *prompt );
int           kbhit( void );
int           putch( int c );
int           ungetch( int ch );
int           wherex( void );
int           wherey( void );

void          _conio_delay (int ms);
int           kbmhit( void );

int  getkbm( void );
extern volatile int _mousex;
extern volatile int _mousey;
extern volatile int _mousebuttons;
extern volatile int _controlkeystate;

wchar_t *cgetws( wchar_t *str );
int      cputws( const wchar_t *str );
wchar_t  getwch(void);
wint_t  putwch(wchar_t c);
wchar_t  ungetwch(wchar_t);
wchar_t  getwche(void);
int cwscanf( const wchar_t *format, ... );
int cwprintf( const wchar_t *format, ... );
int putwtext( int left, int top, int right, int bottom,  const wchar_info *source );
int getwtext( int left, int top, int right, int bottom,  wchar_info *destin );

wchar_t ansi2unicode(char ch);
char unicode2ansi(wchar_t ch);


#ifndef _INC_CONIO
#define _INC_CONIO
#define _getch getch
#define _getwch getwch
#define _getche getche
#define _getwche getwche
#define _kbhit kbhit
#define _putch putch
#define _putwch putwch
#define _ungetch ungetch
#define _ungetwch ungetwch
#define _cscanf cscanf
#define _cwscanf cwscanf
#define _cprintf cprintf
#define _cwprintf cwprintf
#define _cputs cputs
#define _cputws cputws
#define _cgets cgets
#define _cgetws cgetws
#endif

#define delay _conio_delay
#ifndef _CONIO_NO_GETTEXT_
  #define gettext _conio_gettext
#endif

#ifdef __cplusplus
}
#endif

#endif /* _CONIO2_H_ */
