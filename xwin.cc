 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * X interface v0.0
  * v0.0: 950305: black and white window
  * 
  * (c) 1995 Bernd Schmidt
  */

#include <stdio.h>
#include <X11/Xlib.h>

#include "xwin.h"

Display *display;
int screen;
Window rootwin, mywin;
GC whitegc,blackgc;
XColor black,white;
Colormap cmap;

void InitX()
{
   char *display_name = 0;
   XSetWindowAttributes wattr;
   
   display = XOpenDisplay(display_name);
   if (display == 0)  {
      printf("Can't connect to X server %s\n",XDisplayName(display_name));
      exit(-1);
   }
   screen = XDefaultScreen(display);
   rootwin = XRootWindow(display,screen);
/*   wattr.backing_store = Always;*/
  /* dimensions are not exact */
   mywin = XCreateWindow(display,rootwin,0,0,800,400,0,
			 CopyFromParent,CopyFromParent,CopyFromParent,
			 0,&wattr);
   XMapWindow(display,mywin);
   
   whitegc = XCreateGC(display,mywin,0,0);
   blackgc = XCreateGC(display,mywin,0,0);
   cmap = XDefaultColormap(display,screen);
   
   XParseColor(display,cmap,"#000000",&black);
   if (!XAllocColor(display,cmap,&black)) printf("Fehlschlag\n");
   XParseColor(display,cmap,"#ffffff",&white);
   if (!XAllocColor(display,cmap,&white)) printf("Fehlschlag 2\n");
   XSetForeground(display,blackgc,black.pixel);
   XSetForeground(display,whitegc,white.pixel);
}

void DrawPixel(int x,int y,int col)
{
  if (col){
    XDrawPoint(display,mywin,blackgc,x,y);
  } else {
    XDrawPoint(display,mywin,whitegc,x,y);
  }
}
