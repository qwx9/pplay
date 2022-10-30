#include <u.h>
#include <libc.h>
#include <thread.h>
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
static vlong views, viewe, viewmax;
static int bgscalyl, bgscalyr, bgscalf;
static Channel *drawc;

static Image *
eallocimage(Rectangle r, int repl, ulong col)
{
	Image *i;

	if((i = allocimage(display, r, screen->chan, repl, col)) == nil)
		sysfatal("allocimage: %r");
	return i;
}

static void
drawsamps(void*)
{
	int x, n, lmin, lmax, rmin, rmax;
	s16int s;
	uchar *p, *e, *et;
	Rectangle l, r;

	for(;;){
		recvul(drawc);
again:
		lockdisplay(display);
		draw(viewbg, viewbg->r, display->black, nil, ZP);
		unlockdisplay(display);
		n = viewe - views;
		/*if(!file)*/
			p = pcmbuf + views;
		/*else{
			seek(ifd, bgofs, 0);
			n = read(ifd, bgbuf, n);
			seek(ifd, seekp, 0);
			p = bgbuf;
		}*/
		e = p + n;
		x = 0;
		while(p < e){
			if(nbrecvul(drawc) == 1)
				goto again;
			n = T;
			if(n > e - p)
				n -= n - (e - p);
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
		lockdisplay(display);
			draw(viewbg, l, col[Csamp], nil, ZP);
		unlockdisplay(display);
			if(stereo)
				draw(viewbg, r, col[Csamp], nil, ZP);
			x++;
		}
	}
}

static void
b2t(uvlong ofs, int *th, int *tm, int *ts, int *tμ)
{
	uvlong nsamp;

	nsamp = ofs / 4;
	*ts = nsamp / 44100;
	*tm = *ts / 60;
	*th = *tm / 60;
	*tμ = 100 * (nsamp - *ts * 44100) / 44100;
	*ts %= 60;
}

static void
drawstat(void)
{
	int th, tm, ts, tμ;
	uvlong ssamp;
	char s[256], *p;

	ssamp = seekp / 4;
	ts = ssamp / 44100;
	tm = ts / 60;
	th = tm / 60;
	tμ = 100 * (ssamp - ts * 44100) / 44100;
	ts %= 60;
	p = seprint(s, s+sizeof s, "T %lld @ %02d:%02d:%02d.%03d (%llud) ⋅ ",
		T/4, th, tm, ts, tμ, ssamp);
	if(loops != 0 && loops >= views){
		b2t(loops, &th, &tm, &ts, &tμ);
		p = seprint(p, s+sizeof s, "%02d:%02d:%02d.%03d (%llud) ↺ ",
			th, tm, ts, tμ, loops);
	}else
		p = seprint(p, s+sizeof s, "0 ↺ ");
	if(loope != filesz){
		b2t(loope, &th, &tm, &ts, &tμ);
		seprint(p, s+sizeof s, "%02d:%02d:%02d.%03d (%llud)",
			th, tm, ts, tμ, loope);
	}else
		seprint(p, s+sizeof s, "∞");
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
	if(loope != filesz ){
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

	lockdisplay(display);
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
	unlockdisplay(display);
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
	redraw(0);
}

void
setpan(int Δx)
{
	Δx *= T;
	if(zoom == 1 || views == 0 && Δx < 0 || views >= viewmax && Δx > 0)
		return;
	views += Δx;
	redraw(0);
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
redraw(int all)
{
	vlong span;

	lockdisplay(display);
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
	if(all)
		resetdraw();
	unlockdisplay(display);
	nbsendul(drawc, 1);
	update();
}

void
initdrw(void)
{
	if(initdraw(nil, nil, "pplay") < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	unlockdisplay(display);
	col[Csamp] = eallocimage(Rect(0,0,1,1), 1, 0x440000FF);
	col[Cline] = eallocimage(Rect(0,0,1,1), 1, 0x884400FF);
	col[Cloop] = eallocimage(Rect(0,0,1,1), 1, 0x777777FF);
	loope = filesz;
	if((drawc = chancreate(sizeof(ulong), 4)) == nil)
		sysfatal("chancreate: %r");
	if(proccreate(drawsamps, nil, mainstacksize) < 0)
		sysfatal("proccreate: %r");
	redraw(1);
}
