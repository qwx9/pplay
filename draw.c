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
static Rectangle statr;
static usize views, viewe, viewmax, linepos;
static int bgscalyl, bgscalyr;
static double bgscalf;
static Channel *upyours, *drawc;
static usize T;
static int sampwidth = 1;	/* pixels per sample */
static double zoom = 1.0;
static int stalerender, working;

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
rendersamples(Track *t, Rectangle rr)
{
	s16int *l, *e, *r;
	Rectangle rx;

	draw(view, rr, col[Cbg], nil, ZP);
	if(Dx(rr) > t->len / 2)
		rr.max.x = rr.min.x + t->len / 2;
	rx = rr;
	for(l=t->graph[0]+2*rx.min.x, e=l+2*Dx(rr); l<e; l+=2, rx.min.x++){
		rx.min.y = bgscalyl - l[1] / bgscalf;
		rx.max.x = rx.min.x + sampwidth;
		rx.max.y = bgscalyl - l[0] / bgscalf;
		draw(view, rx, col[Csamp], nil, ZP);
	}
	if(!stereo)
		return;
	rx = rr;
	for(r=t->graph[1]+2*rx.min.x, e=r+2*Dx(rr); r<e; r+=2, rx.min.x++){
		rx.min.y = bgscalyr - r[1] / bgscalf;
		rx.max.x = rx.min.x + sampwidth;
		rx.max.y = bgscalyr - r[0] / bgscalf;
		draw(view, rx, col[Csamp], nil, ZP);
	}
}

static void
render(void)
{
	Track *t;

	for(t=tracks; t<tracks+ntracks; t++)
		rendersamples(t, view->r);
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
update(void)
{
	lockdisplay(display);
	if(stalerender || working){
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
}

static void
drawproc(void*)
{
	threadsetname("drawer");
	for(;;){
		if(recv(drawc, nil) < 0){
			fprint(2, "drawproc: %r\n");
			break;
		}
		update();
	}
}

void
refresh(void)
{
	nbsend(drawc, nil);
}

static void
sample(Dot d)
{
	int n, lmin, lmax, rmin, rmax;
	usize k;
	uchar *p, *e;
	s16int s, *l, *r, *le;
	vlong N;

	N = (d.to - d.from) / (T * sampwidth);
	if(d.t->len < 2*N){	/* min, max */
		d.t->graph[0] = erealloc(d.t->graph[0],
			2*N * sizeof *d.t->graph[0],
			d.t->len * sizeof *d.t->graph[0]);
		d.t->graph[1] = erealloc(d.t->graph[1],
			2*N * sizeof *d.t->graph[1],
			d.t->len * sizeof *d.t->graph[1]);
	}
	d.t->len = 2*N;
	l = d.t->graph[0];
	r = d.t->graph[1];
	le = l + d.t->len;
	while(l < le){
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
		if(upyours->n > 0)
			return;
	}
}

static void
sampleproc(void*)
{
	Dot d;

	threadsetname("sampler");
	for(;;){
		if(recv(upyours, &d) < 0){
			fprint(2, "sampproc: %r\n");
			break;
		}
		working = 1;
		stalerender = 1;
		sample(d);
		refresh();
		working = 0;
	}
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
	bgscalyl = (viewr.max.y - font->height) / (stereo ? 4 : 2);
	bgscalyr = viewr.max.y - bgscalyl;
	bgscalf = 32767. / bgscalyl;
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
		refresh();
	/* FIXME: this overloading is stupid; just fork for each? have multiple
	 * workers to begin with à la page? */
	d = *current;
	d.from = d.cur = views;
	d.to = viewe;
	nbsend(upyours, &d);
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
	if(paused)
		refresh();
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
	if(paused)
		refresh();
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
	if((drawc = chancreate(sizeof(int), 1)) == nil
	// FIXME: fudge until better perceptual fix
	|| (upyours = chancreate(sizeof(Dot), 32)) == nil)
		sysfatal("chancreate: %r");
	redraw(1);
	if(proccreate(drawproc, nil, mainstacksize) < 0
	|| proccreate(sampleproc, nil, mainstacksize) < 0)
		sysfatal("proccreate: %r");
}
