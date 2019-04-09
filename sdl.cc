 /* 
  * UAE - The Unusable Amiga Emulator
  * 
  * X interface v0.0
  * v0.0: 950305: black and white window
  * 
  * (c) 1995 Bernd Schmidt
  */

#include <iostream>
using namespace std;
#include <stdio.h>
#include <SDL/sdl.h>


SDL_Surface *screen;
SDL_Event ev;
static int sdlcount;
static int bytes_per_pixel = 8;
void setPixel(int x, int y, int color);

#define NUM_COLORS	256

void InitX()
{
	int i;
	SDL_Color palette[NUM_COLORS];

	/* Set the video mode */
	screen = SDL_SetVideoMode(800, 400, bytes_per_pixel , SDL_SWSURFACE);
	if ( screen == NULL ) {
		fprintf(stderr, "Couldn't set display mode: %s\n",SDL_GetError());
		exit(-1);
	}
	fprintf(stderr, "Screen is in %s mode\n",
		(screen->flags & SDL_FULLSCREEN) ? "fullscreen" : "windowed");

	if (bytes_per_pixel ==8) {
		/* Set a gray colormap, reverse order from white to black */
		for ( i=0; i<NUM_COLORS; ++i ) {
			palette[i].r = (NUM_COLORS-1)-i * (256 / NUM_COLORS);
			palette[i].g = (NUM_COLORS-1)-i * (256 / NUM_COLORS);
			palette[i].b = (NUM_COLORS-1)-i * (256 / NUM_COLORS);
		}
		SDL_SetColors(screen, palette, 0, NUM_COLORS);
	}
	SDL_WM_SetCaption("UAE 0.1 (SDL build)","UAE 0.1");
SDL_FillRect(screen,NULL, 0xffffff); 
		if ( screen->flags & SDL_DOUBLEBUF ) {
			SDL_Flip(screen);
		} else {
			SDL_UpdateRect(screen, 0, 0, 0, 0);
		}
sdlcount=0;
}

void SDLPoll(void)
{
sdlcount++;
if(sdlcount>100) {
SDL_PollEvent(&ev);
#if 0
	switch(ev.type) {
	case SDL_KEYDOWN:
	case SDL_QUIT:
		SDL_Quit();
	break;
	default:
	break;
	}
#endif
if(ev.type == SDL_QUIT){
	SDL_Quit();
	exit(0);
	}

		if ( screen->flags & SDL_DOUBLEBUF ) {
			SDL_Flip(screen);
		} else {
			SDL_UpdateRect(screen, 0, 0, 0, 0);
		}

sdlcount=0;
 }
//cout << "SDLPoll\n";
}	

void DrawPixel(int x,int y,int col)
{
SDL_Rect area;
	area.w = 1;
	area.h = 1;
	area.x = x;
	area.y = y;

	SDL_LockSurface(screen);

  if (col){
    //XDrawPoint(display,mywin,blackgc,x,y);
	SDL_FillRect(screen, &area, 255);
  } else {
    //XDrawPoint(display,mywin,whitegc,x,y);
SDL_FillRect(screen, &area, 0);
  }
  SDL_UnlockSurface(screen);
}
