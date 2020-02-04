#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum{
	Csamp,
	Cline,
	Cloop,
	Ncol,
};
static Image *col[Ncol];
static Image *viewbg, *view;
static Rectangle liner;
static Point statp;
static vlong views, viewe, viewmax, bgofs;
static int bgscalyl, bgscalyr, bgscalf;
static uchar bgbuf[Nchunk * 200];

static void (*drawbg)(void);

static Image *
eallocimage(Rectangle r, int repl, ulong col)
{
	Image *i;

	if((i = allocimage(display, r, screen->chan, repl, col)) == nil)
		sysfatal("allocimage: %r");
	return i;
}

static void
drawsamps(void)
{
	int x, n, lmin, lmax, rmin, rmax;
	s16int s;
	uchar *p, *e, *et;
	Rectangle l, r;

	if(bgofs >= viewe)
		return;
	if(!file){
		p = pcmbuf + bgofs;
		n = viewe - bgofs < sizeof bgbuf ? viewe - bgofs : sizeof bgbuf;
	}else{
		seek(ifd, bgofs, 0);
		n = read(ifd, bgbuf, sizeof bgbuf);
		seek(ifd, seekp, 0);
		p = bgbuf;
	}
	e = p + n;
	x = (bgofs - views) / T;
	while(p < e){
		n = filesz - bgofs < T ? filesz - bgofs : T;
		if(n > e - p)
			n -= n - (e - p);
		bgofs += n;
		et = p + n;
		lmin = lmax = 0;
		rmin = rmax = 0;
		while(p < et){
			s = (s16int)(p[1] << 8 | p[0]);
			if(s < lmin)
				lmin = s;
			else if(s > lmax)
				lmax = s;
			if(stereo){
				s = (s16int)(p[3] << 8 | p[2]);
				if(s < rmin)
					rmin = s;
				else if(s > rmax)
					rmax = s;
			}
			p += 4;
		}
		l = Rect(x, bgscalyl - lmax / bgscalf,
			x+1, bgscalyl - lmin / bgscalf);
		r = Rect(x, bgscalyr - rmax / bgscalf,
			x+1, bgscalyr - rmin / bgscalf);
		draw(viewbg, l, col[Csamp], nil, ZP);
		if(stereo)
			draw(viewbg, r, col[Csamp], nil, ZP);
		x++;
	}
}

static void
drawstat(void)
{
	char s[64];

	snprint(s, sizeof s, "T %lld p %lld", T/4, seekp);
	string(screen, statp, col[Cline], ZP, font, s);
}

static void
drawview(void)
{
	int x;
	Rectangle r;

	draw(view, view->r, viewbg, nil, ZP);
	if(loops != 0 && loops >= views){
		x = (loops - views) / T;
		r = view->r;
		r.min.x += x;
		r.max.x = r.min.x + 1;
		draw(view, r, col[Cloop], nil, ZP);
	}
	if(loope != filesz && loope >= views){
		x = (loope - views) / T;
		r = view->r;
		r.min.x += x;
		r.max.x = r.min.x + 1;
		draw(view, r, col[Cloop], nil, ZP);
	}
}

void
update(void)
{
	int x;

	drawbg();
	drawview();
	x = screen->r.min.x + (seekp - views) / T;
	//if(liner.min.x == x || seekp < views && x > liner.min.x)
	//	return;
	draw(screen, screen->r, view, nil, ZP);
	liner.min.x = x;
	liner.max.x = x + 1;
	if(seekp >= views)
		draw(screen, liner, col[Cline], nil, ZP);
	drawstat();
	flushimage(display, 1);
}

void
setzoom(int Δz, int pow)
{
	int z;

	if(!pow)
		z = zoom + Δz;
	else if(Δz < 0)
		z = zoom >> -Δz;
	else
		z = zoom << Δz;
	if(z < 1 || z > nsamp / Dx(screen->r))
		return;
	zoom = z;
	redraw();
}

void
setpan(int Δx)
{
	Δx *= T;
	if(zoom == 1 || views == 0 && Δx < 0 || views >= viewmax && Δx > 0)
		return;
	views += Δx;
	redraw();
}

void
setloop(vlong ofs)
{
	ofs *= T;
	ofs += views;
	if(ofs < 0 || ofs > filesz)
		return;
	if(ofs < seekp)
		loops = ofs;
	else
		loope = ofs;
	update();
}

void
setpos(vlong ofs)
{
	if(ofs < loops || ofs > loope - Nchunk)
		return;
	seekp = ofs;
	if(file)
		seek(ifd, ofs, 0);
	update();
}

void
setofs(vlong ofs)
{
	setpos(views + ofs * T);
}

static void
resetdraw(void)
{
	int x;
	Rectangle viewr, midr;

	x = screen->r.min.x + (seekp - views) / T;
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
	liner = screen->r;
	liner.min.x = x;
	liner.max.x = x + 1;
	bgscalyl = viewbg->r.max.y / (stereo ? 4 : 2);
	bgscalyr = viewbg->r.max.y - bgscalyl;
	bgscalf = 32767 / bgscalyl;
}

void
redraw(void)
{
	vlong span;

	T = filesz / zoom / Dx(screen->r) & ~3;
	if(T == 0)
		T = 4;
	span = Dx(screen->r) * T;
	viewmax = filesz - span;
	if(views < 0)
		views = 0;
	else if(views > viewmax)
		views = viewmax;
	viewe = views + span;
	bgofs = views;
	resetdraw();
	update();
}

void
initdrw(void)
{
	if(initdraw(nil, nil, "pplay") < 0)
		sysfatal("initdraw: %r");
	col[Csamp] = eallocimage(Rect(0,0,1,1), 1, 0x440000FF);
	col[Cline] = eallocimage(Rect(0,0,1,1), 1, 0x884400FF);
	col[Cloop] = eallocimage(Rect(0,0,1,1), 1, 0x777777FF);
	drawbg = drawsamps;
	loope = filesz;
	redraw();
}
