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
	Cins,
	Cloop,
	Cchunk,
	Ctext,
	Ncol,
};
static Image *col[Ncol];
static Image *view;
static Rectangle liner, statr;
static usize views, viewe, viewmax;
static int bgscalyl, bgscalyr;
static double bgscalf;
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
	if(p > current->totalsz)
		return fmtstrcpy(fmt, "-∞");
	b2t(p, &th, &tm, &ts, &tμ);
	return fmtprint(fmt, "%02d:%02d:%02d.%03d (%zd)",
		th, tm, ts, tμ, p / Sampsz);
}

static int
drawpos(usize pos, Image *c)
{
	Rectangle r;

	if(pos <= views || pos >= viewe)
		return 0;
	r = screen->r;
	r.min.x += (pos - views) / T;
	r.max.x = r.min.x + 1;
	draw(screen, r, c, nil, subpt(r.min, screen->r.min));
	return 1;
}

static void
drawchunks(void)
{
	usize p, off;
	Chunk *c;

	c = p2c(views, &off, current);
	for(p=views-off; p<viewe; p+=c->len, c=c->right){
		if(p == 0)
			continue;
		drawpos(p, col[Cchunk]);
	}
}

static void
drawsamps(void*)
{
	int ox, n, lmin, lmax, rmin, rmax;
	usize m, k;
	s16int s;
	double x;
	uchar *p, *e;
	Rectangle l, r;
	Point range;
	Dot d;

	for(;;){
end:
		if(recv(drawc, &range) < 0)
			break;
again:
		r = view->r;
		r.min.x = (range.x - views) / T;
		r.max.x = (range.y - views) / T;
		lockdisplay(display);
		draw(view, r, col[Cbg], nil, ZP);
		unlockdisplay(display);
		d = *current;
		d.from = range.x;
		d.cur = d.from;
		d.to = range.y;
		m = d.to - d.from;
		ox = 0;
		x = 0.0;
		qlock(&lsync);
		while(m > 0){
			if((n = nbrecv(drawc, &range)) < 0)
				return;
			else if(n == 1){
				qunlock(&lsync);
				goto again;
			}
			n = m < T * sampwidth ? m : T * sampwidth;
			lmin = lmax = 0;
			rmin = rmax = 0;
			while(n > 0){
				if((p = getslice(&d, n, &k)) == nil){
					if(k > 0)
						fprint(2, "getslice: %r\n");
					goto end;
				}
				d.cur += k;
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
			lockdisplay(display);
			draw(view, l, col[Csamp], nil, ZP);
			if(stereo){
				r = Rect(x, bgscalyr - rmax / bgscalf,
					x+sampwidth, bgscalyr - rmin / bgscalf);
				draw(view, r, col[Csamp], nil, ZP);
			}
			unlockdisplay(display);
			if(x - ox >= 1600){
				update(ox, x);
				ox = x;
			}
			x += k / T;
		}
		update(ox, Dx(screen->r));
		qunlock(&lsync);
	}
}

static void
drawstat(void)
{
	char s[256];
	Point p;

	draw(screen, statr, col[Cbg], nil, ZP);
	seprint(s, s+sizeof s, "T %zd @ %τ", T / Sampsz, current->cur);
	p = string(screen, statr.min, col[Ctext], ZP, font, s);
	if(current->from > 0 || current->to < current->totalsz){
		seprint(s, s+sizeof s, " ↺ %τ - %τ", current->from, current->to);
		p = string(screen, p, col[Cloop], ZP, font, s);
	}
	if(current->off != current->from){
		seprint(s, s+sizeof s, " ‡ %τ", current->off);
		p = string(screen, p, col[Cins], ZP, font, s);
	}
	statr.max.x = p.x;
}

static void
drawmarks(void)
{
	if(debugdraw)
		drawchunks();
	drawpos(current->from, col[Cloop]);
	drawpos(current->to, col[Cloop]);
	if(current->off != current->from)
		drawpos(current->from, col[Cins]);
}

void
update(int x, int x´)
{
	Rectangle r;

	r = liner;
	lockdisplay(display);
	draw(screen, liner, view, nil, subpt(r.min, screen->r.min));
	if(x < x´){
		r.min.x = screen->r.min.x + x;
		r.max.x = screen->r.min.x + x´;
		draw(screen, r, view, nil, subpt(r.min, screen->r.min));
	}
	liner.min.x = screen->r.min.x + (current->cur - views) / T;
	liner.max.x = liner.min.x + 1;
	drawpos(current->cur, col[Cline]);
	drawmarks();
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
	if(z < 1.0 || z > (current->totalsz / Sampsz) / Dx(screen->r))
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
	if(to >= current->totalsz)
		to = current->totalsz;
	to &= ~3;
	if((to - from) / Sampsz < Dx(screen->r)){
		werrstr("range too small");
		return -1;
	}
	views = from;
	viewe = to;
	zoom = (double)current->totalsz / (to - from);
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
	setpan(d * (Dx(view->r) + 1));
}

void
setrange(usize from, usize to)
{
	from &= ~3;
	to &= ~3;
	if(current->from > views)
		drawpos(current->from, view);
	if(current->to < viewe)
		drawpos(current->to, view);
	current->from = from;
	current->to = to;
	if(current->cur < from || current->cur >= to)
		current->cur = from;
	current->off = -1ULL;
}

static int
setcur(usize off)
{
	off &= ~3;
	if(off < current->from || off > current->to - Outsz){
		werrstr("cannot jump outside of loop bounds");
		return -1;
	}
	current->off = current->cur = off;
	return 0;
}

int
setloop(vlong off)
{
	off &= ~3;
	if(off < 0 || off > current->totalsz){
		werrstr("invalid range");
		return -1;
	}
	if(off < current->cur)
		setrange(off, current->to);
	else
		setrange(current->from, off);
	return 0;
}

int
setjump(vlong off)
{
	return setcur(off) & ~3;
}

vlong
p2off(int x)
{
	return views + x * T & ~3;
}

static void
resetdraw(void)
{
	int x;
	Rectangle viewr;

	x = screen->r.min.x + (current->cur - views) / T;
	viewr = rectsubpt(screen->r, screen->r.min);
	statr = screen->r;
	if(stereo)
		statr.min.y += (Dy(screen->r) - font->height) / 2 + 1;
	else
		statr.min.y = screen->r.max.y - font->height;
	freeimage(view);
	view = eallocimage(viewr, 0, DNofill);
	liner = screen->r;
	liner.min.x = x;
	liner.max.x = x + 1;
	bgscalyl = (viewr.max.y - font->height) / (stereo ? 4 : 2);
	bgscalyr = viewr.max.y - bgscalyl;
	bgscalf = 32767. / bgscalyl;
}

void
redraw(int all)
{
	usize span;
	Point p;

	lockdisplay(display);
	T = (vlong)(current->totalsz / zoom / Dx(screen->r)) & ~3;
	if(T == 0)
		T = 4;
	span = Dx(screen->r) * T;
	viewmax = current->totalsz - span;
	if(views > viewmax)
		views = viewmax;
	viewe = views + span;
	if(all)
		resetdraw();
	unlockdisplay(display);
	p = Pt(views, viewe);
	nbsend(drawc, &p);
}

void
initdrw(int fuckit)
{
	if(initdraw(nil, nil, "pplay") < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	unlockdisplay(display);
	if(!fuckit){
		col[Cbg] = eallocimage(Rect(0,0,1,1), 1, 0xFFFFC0FF);
		col[Csamp] = eallocimage(Rect(0,0,1,1), 1, 0x3F3F20FF);
		col[Ctext] = display->black;
		col[Cline] = eallocimage(Rect(0,0,1,1), 1, 0xFF2222FF);
		col[Cins] = eallocimage(Rect(0,0,1,1), 1, DBlue);
		col[Cloop] = eallocimage(Rect(0,0,1,1), 1, 0xDD00DDFF);
		col[Cchunk] = eallocimage(Rect(0,0,1,1), 1, DPaleyellow);
	}else{
		col[Cbg] = display->black;
		col[Csamp] = eallocimage(Rect(0,0,1,1), 1, 0x2A2A2AFF);
		col[Ctext] = eallocimage(Rect(0,0,1,1), 1, 0xBBBBBBFF);
		col[Cline] = eallocimage(Rect(0,0,1,1), 1, 0xEEA000FF);
		col[Cins] = eallocimage(Rect(0,0,1,1), 1, 0x509A9AFF);
		col[Cloop] = eallocimage(Rect(0,0,1,1), 1, 0x8888CCFF);
		col[Cchunk] = eallocimage(Rect(0,0,1,1), 1, 0xEE0000FF);
	}
	if((drawc = chancreate(sizeof(Point), 4)) == nil)
		sysfatal("chancreate: %r");
	if(proccreate(drawsamps, nil, mainstacksize) < 0)
		sysfatal("proccreate: %r");
	redraw(1);
}
