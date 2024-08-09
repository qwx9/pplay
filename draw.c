#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

QLock lsync;
int debugdraw = 1;

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
static Rectangle statr;
static usize views, viewe, viewmax, linepos;
static int bgscalyl, bgscalyr;
static double bgscalf;
static Channel *drawc;
static usize T;
static int sampwidth = 1;	/* pixels per sample */
static double zoom = 1.0;
static int stalerender, tworking;
// FIXME
static int nbuf = 1;
static Channel *trkc[1];
static Rectangle tr;

static int working;
static vlong slen;
static s16int *graph[2];

static Image *
eallocimage(Rectangle r, int repl, ulong col)
{
	Image *i;

	if((i = allocimage(display, r, screen->chan, repl, col)) == nil)
		sysfatal("allocimage: %r");
	return i;
}

vlong
ss2view(int x)
{
	return (x - views) / (T * sampwidth);
}

vlong
view2ss(int x)
{
	return views + x * T * sampwidth & ~3;
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
renderpos(usize ss, Image *c, int justdraw)
{
	Rectangle r;

	if(ss <= views || ss >= viewe)
		return 0;
	r = view->r;
	r.min.x = ss2view(ss);
	r.max.x = r.min.x + 1;
	if(justdraw){
		r = rectaddpt(r, screen->r.min);
		draw(screen, r, c, nil, ZP);
	}else
		draw(view, r, c, nil, ZP);
	return 1;
}

static void
renderchunks(void)
{
	usize p, off;
	Chunk *c;

	c = p2c(views, &off, current);
	for(p=views-off; p<viewe; p+=c->len, c=c->right){
		if(p == 0)
			continue;
		renderpos(p, col[Cchunk], 0);
	}
}

static void
rendermarks(void)
{
	if(debugdraw)
		renderchunks();
	renderpos(current->from, col[Cloop], 0);
	renderpos(current->to, col[Cloop], 0);
	if(current->off != current->from)
		renderpos(current->off, col[Cins], 0);
}

static void
rendersamples(void)
{
	s16int *l, *e, *r;
	Rectangle rx, rr;

	if(slen == 0)
		return;
	rr = tr;
	draw(view, rr, col[Cbg], nil, ZP);
	if(Dx(rr) > slen / 2)
		rr.max.x = rr.min.x + slen / 2;
	rx = rr;
	for(l=graph[0]+2*rx.min.x, e=l+2*Dx(rr); l<e; l+=2, rx.min.x++){
		rx.min.y = rr.min.y + bgscalyl - l[1] / bgscalf;
		rx.max.x = rx.min.x + sampwidth;
		rx.max.y = rr.min.y + bgscalyl - l[0] / bgscalf;
		draw(view, rx, col[Csamp], nil, ZP);
	}
	if(!stereo)
		return;

/*
FIXME: wrong midpoint.
rendersamples 0x410450 [0 0] [1365 187] view [0 0] [1365 375]→ [187 0] [1552 187]
*/

	rx = rectaddpt(rr, Pt(Dy(view->r)/2,0));
	//fprint(2, "→ %R\n", rx);
	for(r=graph[1]+2*rx.min.x, e=r+2*Dx(rr); r<e; r+=2, rx.min.x++){
		rx.min.y = rr.min.y + bgscalyr - r[1] / bgscalf;
		rx.max.x = rx.min.x + sampwidth;
		rx.max.y = rr.min.y + bgscalyr - r[0] / bgscalf;
		draw(view, rx, col[Csamp], nil, ZP);
	}
}

static void
render(void)
{
	rendersamples();
	rendermarks();
}

static void
erasemark(usize ss)
{
	Rectangle r;

	ss = ss2view(ss);
	r = view->r;
	r.min.x = ss;
	r.max.x = r.min.x + 1;
	r = rectaddpt(r, screen->r.min);
	draw(screen, r, view, nil, subpt(r.min, screen->r.min));
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
drawproc(void*)
{
	int what;

	threadsetname("drawer");
	for(;;){
		what = Drawrender;
		// FIXME
		if(recv(drawc, &what) < 0){
//		if(nbrecv(drawc, &what) < 0){
			fprint(2, "drawproc: %r\n");
			break;
		}
		lockdisplay(display);
		if(what == Drawrender || stalerender || working){
			if(!working)
				stalerender = 0;
			render();
			draw(screen, rectaddpt(view->r, screen->r.min), view, nil, ZP);
		}else
			erasemark(linepos);
		renderpos(current->cur, col[Cline], 1);
		linepos = current->cur;
		drawstat();
		flushimage(display, 1);
		unlockdisplay(display);
		sleep(100);
	}
}

void
refresh(int what)
{
	nbsend(drawc, &what);
}

static void
sampler(void *cp)
{
	int n, lmin, lmax, rmin, rmax;
	usize k;
	uchar *p, *e;
	s16int s, *l, *r, *le;
	vlong N;
	Dot d;
	Channel *c;

	c = cp;
again:
	if(recv(c, &d) < 0)
		threadexits("recv: %r");
	tworking = ++working;
	stalerender++;
	N = (d.to - d.from) / (T * sampwidth);
	if(slen < 2*N){	/* min, max */
		graph[0] = erealloc(graph[0],
			2*N * sizeof *graph[0],
			slen * sizeof *graph[0]);
		graph[1] = erealloc(graph[1],
			2*N * sizeof *graph[1],
			slen * sizeof *graph[1]);
	}
	slen = 2*N;
	l = graph[0];
	r = graph[1];
	le = l + slen;
	while(l < le){
		if(c->n > 0)
			break;
		n = T * sampwidth;
		lmin = lmax = rmin = rmax = 0;
		while(n > 0){
			if((p = getslice(&d, n, &k)) == nil){
				if(k > 0)
					fprint(2, "getslice: %r\n");
				l = le;
				break;
			}
			d.cur += k;
			for(e=p+k; p<e; p+=Sampsz*sampwidth){
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
			}
			n -= k;
		}
		*l++ = lmin;
		*l++ = lmax;
		if(stereo){
			*r++ = rmin;
			*r++ = rmax;
		}
	}
	working--;
	refresh(Drawrender);
	goto again;
}

void
setcurrent(Point o)
{
	int dy;
	Rectangle r;

	dy = screen->r.max.y / 1;
	r = Rpt(screen->r.min, Pt(screen->r.max.x, dy));
	if(ptinrect(o, r)){
		current = &dot;
		return;
	}
	sysfatal("setcurrent: phase error");
}

static void
resetdraw(void)
{
	Rectangle viewr;

	viewr = rectsubpt(screen->r, screen->r.min);
	statr = screen->r;
	if(stereo){
		statr.min.y += (Dy(screen->r) - font->height) / 2 + 1;
		statr.max.y = statr.min.y + font->height;
	}else
		statr.min.y = screen->r.max.y - font->height;
	freeimage(view);
	view = eallocimage(viewr, 0, DNofill);
	bgscalyl = (viewr.max.y - font->height) / (1 * (stereo ? 4 : 2));
	bgscalyr = viewr.max.y - bgscalyl;
	bgscalf = 32767. / bgscalyl;
	if(trkc[0] == nil){
		if((trkc[0] = chancreate(sizeof(Dot), 2)) == nil)
			sysfatal("chancreate: %r");
		if(proccreate(sampler, trkc[0], mainstacksize) < 0)
			sysfatal("00reate: %r");
	}
	tr = Rpt(view->r.min, Pt(view->r.max.x, Dy(view->r)));
}

void
redraw(int all)
{
	usize span;
	Dot d;

	lockdisplay(display);
	T = (vlong)(current->totalsz / zoom / Dx(screen->r)) & ~3;
	if(T < Sampsz)
		T = Sampsz;
	span = Dx(screen->r) * T;
	viewmax = current->totalsz - span;
	if(views > viewmax)
		views = viewmax;
	viewe = views + span;
	if(all)
		resetdraw();
	unlockdisplay(display);
	if(paused)
		refresh(Drawall);
	d = dot;
	d.from = d.cur = views;
	d.to = viewe;
	nbsend(trkc[0], &d);
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
	current->from = from;
	current->to = to;
	if(current->cur < from || current->cur >= to)
		current->cur = from;
	current->off = -1;
	stalerender = 1;
	refresh(Drawrender);
}

int
setjump(vlong off)
{
	off &= ~3;
	if(off < current->from || off > current->to - Sampsz){
		werrstr("cannot jump outside of loop bounds");
		return -1;
	}
	current->off = current->cur = off;
	stalerender = 1;
	refresh(Drawrender);
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
		col[Cline] = eallocimage(Rect(0,0,1,1), 1, DBlue);
		col[Cins] = eallocimage(Rect(0,0,1,1), 1, DPaleblue);
		col[Cloop] = eallocimage(Rect(0,0,1,1), 1, DBluegreen);
		col[Cchunk] = eallocimage(Rect(0,0,1,1), 1, DRed);
	}else{
		col[Cbg] = display->black;
		col[Csamp] = eallocimage(Rect(0,0,1,1), 1, 0x2A2A2AFF);
		col[Ctext] = eallocimage(Rect(0,0,1,1), 1, 0xBBBBBBFF);
		col[Cline] = eallocimage(Rect(0,0,1,1), 1, 0xEEA000FF);
		col[Cins] = eallocimage(Rect(0,0,1,1), 1, 0x509A9AFF);
		col[Cloop] = eallocimage(Rect(0,0,1,1), 1, 0x8888CCFF);
		col[Cchunk] = eallocimage(Rect(0,0,1,1), 1, 0xEE0000FF);
	}
	if((drawc = chancreate(sizeof(int), 1)) == nil)
		sysfatal("chancreate: %r");
	redraw(1);
	if(proccreate(drawproc, nil, mainstacksize) < 0)
		sysfatal("proccreate: %r");
}
