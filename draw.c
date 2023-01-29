#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

QLock lsync;
int debugdraw;

enum{
	Cbg,
	Csamp,
	Cline,
	Cloop,
	Cchunk,
	Ctext,
	Ncol,
};
static Image *col[Ncol];
static Image *viewbg, *view;
static Rectangle liner;
static Point statp;
static usize views, viewe, viewmax;
static int bgscalyl, bgscalyr, bgscalf;
static Channel *drawc;
static usize T;
static int sampwidth = 1;
static double zoom = 1.0;

static Image *
eallocimage(Rectangle r, int repl, ulong col)
{
	Image *i;

	if((i = allocimage(display, r, screen->chan, repl, col)) == nil)
		sysfatal("allocimage: %r");
	return i;
}

static void
b2t(usize off, int *th, int *tm, int *ts, int *tμ)
{
	usize nsamp;

	nsamp = off / Sampsz;
	*ts = nsamp / Rate;
	*tm = *ts / 60;
	*th = *tm / 60;
	*tμ = 100 * (nsamp - *ts * Rate) / Rate;
	*ts %= 60;
	*tm %= 60;
}

int
τfmt(Fmt *fmt)
{
	int th, tm, ts, tμ;
	usize p;

	p = va_arg(fmt->args, usize);
	if(p > totalsz)
		return fmtstrcpy(fmt, "-∞");
	b2t(p, &th, &tm, &ts, &tμ);
	return fmtprint(fmt, "%02d:%02d:%02d.%03d (%zd)",
		th, tm, ts, tμ, p / Sampsz);
}

static int
drawpos(usize pos, Image *c)
{
	int x;
	Rectangle r;

	x = (pos - views) / T;
	if(x < views || x >= viewe)
		return 0;
	r = view->r;
	r.min.x += x;
	r.max.x = r.min.x + 1;
	draw(view, r, c, nil, ZP);
	return 1;
}

static void
drawchunks(void)
{
	usize p, off;
	Chunk *c;

	c = p2c(views, &off);
	for(p=views-off; p<viewe; p+=c->len, c=c->right){
		if(p == 0)
			continue;
		if(!drawpos(p, col[Cchunk]) || c->len == 0)
			break;
	}
}

static void
drawsamps(void*)
{
	int x, lmin, lmax, rmin, rmax;
	usize n, m, k;
	s16int s;
	uchar *p, *e;
	Rectangle l, r;
	Dot d;

	for(;;){
end:
		recvul(drawc);
again:
		lockdisplay(display);
		draw(viewbg, viewbg->r, col[Cbg], nil, ZP);
		unlockdisplay(display);
		d.from = 0;
		d.pos = views;
		d.to = totalsz;
		m = viewe - views;
		x = 0;
		qlock(&lsync);
		while(m > 0){
			if(nbrecvul(drawc) == 1){
				qunlock(&lsync);
				goto again;
			}
			n = m < T * sampwidth ? m : T * sampwidth;
			lmin = lmax = 0;
			rmin = rmax = 0;
			while(n > 0){
				p = getslice(&d, n, &k);
				if(p == nil){
					if(k > 0)
						fprint(2, "getslice: %r\n");
					goto end;
				}
				e = p + k;
				while(p < e){
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
					p += 4 * sampwidth;
				}
				n -= k;
				m -= k;
			}
			l = Rect(x, bgscalyl - lmax / bgscalf,
				x+sampwidth, bgscalyl - lmin / bgscalf);
			r = Rect(x, bgscalyr - rmax / bgscalf,
				x+sampwidth, bgscalyr - rmin / bgscalf);
			lockdisplay(display);
			draw(viewbg, l, col[Csamp], nil, ZP);
			if(stereo)
				draw(viewbg, r, col[Csamp], nil, ZP);
			unlockdisplay(display);
			x = (d.pos - views) / T;
		}
		qunlock(&lsync);
	}
}

static void
drawstat(void)
{
	char s[256];
	Point p;

	seprint(s, s+sizeof s, "T %zd @ %τ", T / Sampsz, dot.pos);
	p = string(screen, statp, col[Ctext], ZP, font, s);
	if(dot.from > 0 || dot.to < totalsz){
		seprint(s, s+sizeof s, " ↺ %τ - %τ", dot.from, dot.to);
		p = string(screen, p, col[Cloop], ZP, font, s);
	}
}

static void
drawview(void)
{
	draw(view, view->r, viewbg, nil, ZP);
	if(debugdraw)
		drawchunks();
	drawpos(dot.from, col[Cloop]);
	drawpos(dot.to, col[Cloop]);
}

void
update(void)
{
	int x;
	usize p;

	p = dot.pos;
	lockdisplay(display);
	drawview();
	x = screen->r.min.x + (p - views) / T;
	draw(screen, screen->r, view, nil, ZP);
	liner.min.x = x;
	liner.max.x = x + 1;
	if(p >= views)
		draw(screen, liner, col[Cline], nil, ZP);
	drawstat();
	flushimage(display, 1);
	unlockdisplay(display);
}

void
setzoom(int Δz, int mul)
{
	double z;

	if(!mul)
		z = zoom + Δz;
	else if(Δz < 0)
		z = zoom / pow(2, -Δz);
	else
		z = zoom * pow(2, Δz);
	if(z < 1.0 || z > (totalsz / Sampsz) / Dx(screen->r))
		return;
	zoom = z;
	redraw(0);
}

int
zoominto(vlong from, vlong to)
{
	if(from < 0)
		from = 0;
	from &= ~3;
	if(to >= totalsz)
		to = totalsz;
	to &= ~3;
	if((to - from) / Sampsz < Dx(screen->r)){
		werrstr("range too small");
		return -1;
	}
	views = from;
	viewe = to;
	zoom = (double)totalsz / (to - from);
	redraw(0);
	return 0;
}

void
setpan(int Δx)
{
	usize new;

	Δx *= T;
	if(zoom == 1)
		return;
	if(Δx < 0 && -Δx > views)
		new = 0;
	else if(views + Δx >= viewmax)
		new = viewmax;
	else
		new = views + Δx;
	if(new == views)
		return;
	views = new;
	redraw(0);
}

void
setpage(int d)
{
	setpan(d * (vlong)T * Dx(screen->r) / Dx(view->r));
}

void
setloop(vlong off)
{
	off *= T;
	off += views;
	if(off < 0 || off > totalsz)
		return;
	if(off < dot.pos)
		setrange(off, dot.to);
	else
		setrange(dot.from, off);
	update();
}

static void
setcur(usize off)
{
	if(off < dot.from || off > dot.to - Outsz)
		return;
	jump(off);
	update();
}

void
setjump(usize off)
{
	setcur(off);
}

void
setofs(usize ofs)
{
	setcur(views + ofs * T);
}

static void
resetdraw(void)
{
	int x;
	Rectangle viewr, midr;

	x = screen->r.min.x + (dot.pos - views) / T;
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
	usize span;

	lockdisplay(display);
	T = (vlong)(totalsz / zoom / Dx(screen->r)) & ~3;
	if(T == 0)
		T = 4;
	span = Dx(screen->r) * T;
	viewmax = totalsz - span;
	if(views > viewmax)
		views = viewmax;
	viewe = views + span;
	if(all)
		resetdraw();
	unlockdisplay(display);
	nbsendul(drawc, 1);
	update();
}

void
initdrw(int fuckit)
{
	if(initdraw(nil, nil, "pplay") < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	unlockdisplay(display);
	col[Cbg] = fuckit ? display->white : display->black;
	col[Csamp] = eallocimage(Rect(0,0,1,1), 1, fuckit ? 0x555555FF : 0x2A2A2AFF);
	col[Ctext] = eallocimage(Rect(0,0,1,1), 1, fuckit ? DBlack : 0xBBBBBBFF);
	col[Cline] = eallocimage(Rect(0,0,1,1), 1, fuckit ? DPaleyellow: 0xEEA000FF);
	col[Cloop] = eallocimage(Rect(0,0,1,1), 1, fuckit ? DPurpleblue: 0x8888CCFF);
	col[Cchunk] = eallocimage(Rect(0,0,1,1), 1, 0xEE0000FF);
	if((drawc = chancreate(sizeof(ulong), 4)) == nil)
		sysfatal("chancreate: %r");
	if(proccreate(drawsamps, nil, mainstacksize) < 0)
		sysfatal("proccreate: %r");
	redraw(1);
}
