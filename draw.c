#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

QLock lsync;
int samptime;

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
static Channel *drawc, *sampc;
static usize T;
static int sampwidth = 1;	/* pixels per sample */
static double zoom = 1.0;
static int stalerender, tworking;
static int working;
static vlong slen;
static s16int *graph[2];

#define Rrate	(1000.0 / 60.0)

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
	if(p > dot.totalsz)
		return fmtstrcpy(fmt, "-∞");
	b2t(p, &th, &tm, &ts, &tμ);
	if(samptime)
		return fmtprint(fmt, "%zd", p / Sampsz);
	else
		return fmtprint(fmt, "%02d:%02d:%02d.%03d", th, tm, ts, tμ);
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

	c = p2c(views, &off, &dot);
	for(p=views-off; p<viewe; p+=c->len, c=c->right){
		if(p == 0)
			continue;
		renderpos(p, col[Cchunk], 0);
	}
}

static void
rendermarks(void)
{
	renderchunks();
	renderpos(dot.from, col[Cloop], 0);
	renderpos(dot.to, col[Cloop], 0);
	if(dot.off != dot.from)
		renderpos(dot.off, col[Cins], 0);
}

static void
rendersamples(void)
{
	s16int *l, *e, *r;
	Rectangle rx, rr;

	if(slen == 0)
		return;
	rr = view->r;
	draw(view, rr, col[Cbg], nil, ZP);
	if(Dx(rr) > slen / 2)
		rr.max.x = rr.min.x + slen / 2;
	rx = rr;
	for(l=graph[chan]+2*rx.min.x, e=l+2*Dx(rr); l<e; l+=2, rx.min.x++){
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

	rx = rectaddpt(rr, Pt(0, Dy(view->r)/2));
	for(r=graph[chan+1&1]+2*rx.min.x, e=r+2*Dx(rr); r<e; r+=2, rx.min.x++){
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
	seprint(s, s+sizeof s, "T=%zd @ %τ", T / Sampsz, dot.cur);
	p = string(screen, statr.min, col[Ctext], ZP, font, s);
	if(dot.from > 0 || dot.to < dot.totalsz){
		seprint(s, s+sizeof s, " from %τ to %τ", dot.from, dot.to);
		p = string(screen, p, col[Cloop], ZP, font, s);
	}
	if(dot.off != dot.from && dot.off >= 0){
		seprint(s, s+sizeof s, " last %τ", dot.off);
		p = string(screen, p, col[Cins], ZP, font, s);
	}
	statr.max.x = p.x;
}

static void
drawproc(void*)
{
	int what;
	long Δt;
	double t0, t;

	threadsetname("drawer");
	t0 = nsec() / 1000000.0 + Rrate;
	for(;;){
		what = Drawrender;
		if(nbrecv(drawc, &what) < 0){
			fprint(2, "drawproc: %r\n");
			break;
		}
		lockdisplay(display);
		if((what & Drawrender) != 0 || stalerender || working){
			if(!working)
				stalerender = 0;
			render();
			draw(screen, rectaddpt(view->r, screen->r.min), view, nil, ZP);
		}else
			erasemark(linepos);
		renderpos(dot.cur, col[Cline], 1);
		linepos = dot.cur;
		drawstat();
		flushimage(display, 1);
		unlockdisplay(display);
		t = nsec() / 1000000.0;
		Δt = t0 - t;
		if(Δt > 0)
			sleep(Δt);
		else{
			Δt = -Δt;
			t0 += Δt - Δt % (long)Rrate;
		}
		t0 += Rrate;
	}
}
 
/* throttling of draw requests happens here */
void
refresh(int what)
{
	static int f;

	f |= what;
	if(nbsend(drawc, &f) != 0)
		f = 0;
}

static void
sampler(void *)
{
	int n, lmin, lmax, rmin, rmax;
	usize k;
	uchar *p, *e;
	s16int s, *l, *r, *le;
	vlong N;
	Dot d;

again:
	if(recv(sampc, &d) < 0)
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
		if(sampc->n > 0)	/* start over */
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
				s = (s16int)(p[3] << 8 | p[2]);
				if(s < rmin)
					rmin = s;
				else if(s > rmax)
					rmax = s;
			}
			n -= k;
		}
		*l++ = lmin;
		*l++ = lmax;
		*r++ = rmin;
		*r++ = rmax;
	}
	working--;
	refresh(Drawrender);
	goto again;
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
}

void
redraw(int all)
{
	usize span;
	Dot d;

	lockdisplay(display);
	T = (vlong)(dot.totalsz / zoom / Dx(screen->r)) & ~3;
	if(T < Sampsz)
		T = Sampsz;
	span = Dx(screen->r) * T;
	viewmax = dot.totalsz - span;
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
	nbsend(sampc, &d);
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
	if(z < 1.0 || z > (dot.totalsz / Sampsz) / Dx(screen->r))
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
	if(to >= dot.totalsz)
		to = dot.totalsz;
	to &= ~3;
	if((to - from) / Sampsz < Dx(screen->r)){
		werrstr("range too small");
		return -1;
	}
	views = from;
	viewe = to;
	zoom = (double)dot.totalsz / (to - from);
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
	dot.from = from;
	dot.to = to;
	if(dot.cur < from || dot.cur >= to)
		dot.cur = from;
	dot.off = -1;
	stalerender = 1;
	refresh(Drawrender);
}

int
setjump(vlong off)
{
	off &= ~3;
	if(off < dot.from || off > dot.to - Sampsz){
		werrstr("cannot jump outside of loop bounds");
		return -1;
	}
	dot.off = dot.cur = off;
	stalerender = 1;
	refresh(Drawrender);
	return 0;
}

int
setloop(vlong off)
{
	off &= ~3;
	if(off < 0 || off > dot.totalsz){
		werrstr("invalid range");
		return -1;
	}
	if(off < dot.cur)
		setrange(off, dot.to);
	else
		setrange(dot.from, off);
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
	if((drawc = chancreate(sizeof(int), 0)) == nil
	|| (sampc = chancreate(sizeof(Dot), 2)) == nil)
		sysfatal("chancreate: %r");
	redraw(1);
	if(proccreate(sampler, nil, mainstacksize) < 0
	|| proccreate(drawproc, nil, mainstacksize) < 0)
		sysfatal("proccreate: %r");
}
