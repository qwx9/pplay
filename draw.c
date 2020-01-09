#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum{
	Cbg,
	Csamp,
	Cline,
	Cloop,
	Ncol,
};
static Image *col[Ncol];
static Image *viewbg, *view;
static Rectangle liner;
static Point statp;

static Image *
eallocimage(Rectangle r, int repl, ulong col)
{
	Image *i;

	if((i = allocimage(display, r, screen->chan, repl, col)) == nil)
		sysfatal("allocimage: %r");
	return i;
}

static void
drawstat(void)
{
	char s[64];

	snprint(s, sizeof s, "T %d p %zd", T, bufp-buf);
	string(screen, statp, col[Cline], ZP, font, s);
}

static void
drawsamps(void)
{
	int x, yl, yr, w, scal, lmin, lmax, rmin, rmax;
	short s;
	uchar *p, *e;
	Rectangle l, r;

	w = T * 4;
	p = viewp;
	x = 0;
	yl = viewbg->r.max.y / (stereo ? 4 : 2);
	yr = viewbg->r.max.y - yl;
	scal = 32767 / yl;
	while(p < viewe){
		e = p + w;
		if(e > viewe)
			e = viewe;
		lmin = lmax = 0;
		rmin = rmax = 0;
		while(p < e){
			s = (short)(p[1] << 8 | p[0]);
			if(s < lmin)
				lmin = s;
			else if(s > lmax)
				lmax = s;
			if(stereo){
				s = (short)(p[3] << 8 | p[2]);
				if(s < rmin)
					rmin = s;
				else if(s > rmax)
					rmax = s;
			}
			p += 4;
		}
		l = Rect(x, yl - lmax / scal, x+1, yl - lmin / scal);
		draw(viewbg, l, col[Csamp], nil, ZP);
		if(stereo){
			r = Rect(x, yr - rmax / scal, x+1, yr - rmin / scal);
			draw(viewbg, r, col[Csamp], nil, ZP);
		}
		x++;
	}
}

void
update(void)
{
	int x;

	x = screen->r.min.x + (bufp - viewp) / 4 / T;
	if(liner.min.x == x || bufp < viewp && x > liner.min.x)
		return;
	draw(screen, screen->r, view, nil, ZP);
	liner.min.x = x;
	liner.max.x = x + 1;
	if(bufp >= viewp)
		draw(screen, liner, col[Cline], nil, ZP);
	drawstat();
	flushimage(display, 1);
}

void
drawview(void)
{
	int x;
	Rectangle r;

	draw(view, view->r, viewbg, nil, ZP);
	if(loops != buf && loops >= viewp){
		x = (loops - viewp) / 4 / T;
		r = view->r;
		r.min.x += x;
		r.max.x = r.min.x + 1;
		draw(view, r, col[Cloop], nil, ZP);
	}
	if(loope != bufe && loope >= viewp){
		x = (loope - viewp) / 4 / T;
		r = view->r;
		r.min.x += x;
		r.max.x = r.min.x + 1;
		draw(view, r, col[Cloop], nil, ZP);
	}
	draw(screen, screen->r, view, nil, ZP);
	draw(screen, liner, col[Cline], nil, ZP);
	drawstat();
	flushimage(display, 1);
}

void
redrawbg(void)
{
	int w, x;
	Rectangle viewr, midr;

	T = nbuf / zoom / Dx(screen->r);
	if(T == 0)
		T = 1;
	w = Dx(screen->r) * T * 4;
	viewmax = bufe - w;
	if(viewp < buf)
		viewp = buf;
	else if(viewp > viewmax)
		viewp = viewmax;
	viewe = viewp + w;
	x = screen->r.min.x + (bufp - viewp) / 4 / T;
	liner = screen->r;
	liner.min.x = x;
	liner.max.x = x + 1;
	viewr = rectsubpt(screen->r, screen->r.min);
	freeimage(viewbg);
	freeimage(view);
	viewbg = eallocimage(viewr, 0, DBlack);
	view = eallocimage(viewr, 0, DBlack);
	if(stereo){
		midr = viewr;
		midr.min.y = midr.max.y / 2;
		midr.max.y = midr.min.y + 1;
		draw(viewbg, midr, col[Csamp], nil, ZP);
		statp = Pt(screen->r.min.x,
			screen->r.min.y + (Dy(screen->r) - font->height) / 2 + 1);
	}else
		statp = Pt(screen->r.min.x, screen->r.max.y - font->height);
	drawsamps();
	drawview();
}

void
initview(void)
{
	if(initdraw(nil, nil, "pplay") < 0)
		sysfatal("initdraw: %r");
	col[Cbg] = display->black;
	col[Csamp] = eallocimage(Rect(0,0,1,1), 1, 0x440000FF);
	col[Cline] = eallocimage(Rect(0,0,1,1), 1, 0x884400FF);
	col[Cloop] = eallocimage(Rect(0,0,1,1), 1, 0x777777FF);
	redrawbg();
}
