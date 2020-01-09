#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>

enum{
	Ndelay = 44100 / 25,
	Nchunk = Ndelay * 4,

	Cbg = 0,
	Csamp,
	Cline,
	Cloop,
	Ncol
};

int cat;
int T, stereo, zoom = 1;
Rectangle liner;
Point statp;
ulong nbuf;
uchar *buf, *bufp, *bufe, *viewp, *viewe, *viewmax, *loops, *loope;
Image *viewbg, *view, *col[Ncol], *disp;
Keyboardctl *kc;
Mousectl *mc;
QLock lck;

Image *
eallocimage(Rectangle r, int repl, ulong col)
{
	Image *i;

	if((i = allocimage(display, r, screen->chan, repl, col)) == nil)
		sysfatal("allocimage: %r");
	return i;
}

void
drawstat(void)
{
	char s[64];

	snprint(s, sizeof s, "T %d p %zd", T, bufp-buf);
	string(screen, statp, col[Cline], ZP, font, s);
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
athread(void *)
{
	int n, fd;

	if((fd = cat ? 1 : open("/dev/audio", OWRITE)) < 0)
		sysfatal("open: %r");
	for(;;){
		qlock(&lck);
		n = bufp + Nchunk >= loope ? loope - bufp : Nchunk;
		if(write(fd, bufp, n) != n)
			break;
		bufp += n;
		if(bufp >= loope)
			bufp = loops;
		update();
		qunlock(&lck);
		yield();
	}
	close(fd);
}

void
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
writepcm(int pause)
{
	int fd, n, sz;
	char path[256];
	uchar *p;

	memset(path, 0, sizeof path);
	if(!pause)
		qlock(&lck);
	n = enter("path:", path, sizeof(path)-UTFmax, mc, kc, nil);
	if(!pause)
		qunlock(&lck);
	if(n < 0)
		return;
	if((fd = create(path, OWRITE, 0664)) < 0){
		fprint(2, "create: %r\n");
		return;
	}
	if((sz = iounit(fd)) == 0)
		sz = 8192;
	for(p=loops; p<loope; p+=sz){
		n = loope - p < sz ? loope - p : sz;
		if(write(fd, p, n) != n){
			fprint(2, "write: %r\n");
			goto end;
		}
	}
end:
	close(fd);
}

void
setzoom(int n)
{
	int m;

	m = zoom + n;
	if(m < 1 || m > nbuf / Dx(screen->r))
		return;
	zoom = m;
	if(nbuf / zoom / Dx(screen->r) != T)
		redrawbg();
}

void
setpan(int n)
{
	n *= T * 4 * 16;
	if(zoom == 1 || viewp == buf && n < 0 || viewp == viewmax && n > 0)
		return;
	viewp += n;
	redrawbg();
}

void
setloop(void)
{
	int n;
	uchar *p;

	n = (mc->xy.x - screen->r.min.x) * T * 4;
	p = viewp + n;
	if(p < buf || p > bufe)
		return;
	if(p < bufp)
		loops = p;
	else
		loope = p;
	drawview();
}

void
setpos(void)
{
	int n;
	uchar *p;

	n = (mc->xy.x - screen->r.min.x) * T * 4;
	p = viewp + n;
	if(p < loops || p > loope - Nchunk)
		return;
	bufp = p;
	update();
}

void
bufrealloc(ulong n)
{
	int off;

	off = bufp - buf;
	if((buf = realloc(buf, n)) == nil)
		sysfatal("realloc: %r");
	bufe = buf + n;
	bufp = buf + off;
}

void
usage(void)
{
	fprint(2, "usage: %s [-cs] [pcm]\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char **argv)
{
	int n, sz, fd, pause;
	Mouse mo;
	Rune r;

	ARGBEGIN{
	case 'c': cat = 1; break;
	case 's': stereo = 1; break;
	default: usage();
	}ARGEND
	if((fd = *argv != nil ? open(*argv, OREAD) : 0) < 0)
		sysfatal("open: %r");
	if(sz = iounit(fd), sz == 0)
		sz = 8192;
	bufrealloc(nbuf += 4*1024*1024);
	while((n = read(fd, bufp, sz)) > 0){
		bufp += n;
		if(bufp + sz >= bufe)
			bufrealloc(nbuf += 4*1024*1024);
	}
	if(n < 0)
		sysfatal("read: %r");
	close(fd);
	bufrealloc(nbuf = bufp - buf);
	nbuf /= 4;
	bufp = buf;
	if(initdraw(nil, nil, "pplay") < 0)
		sysfatal("initdraw: %r");
	if(kc = initkeyboard(nil), kc == nil)
		sysfatal("initkeyboard: %r");
	if(mc = initmouse(nil, screen), mc == nil)
		sysfatal("initmouse: %r");
	col[Cbg] = display->black;
	col[Csamp] = eallocimage(Rect(0,0,1,1), 1, 0x440000FF);
	col[Cline] = eallocimage(Rect(0,0,1,1), 1, 0x884400FF);
	col[Cloop] = eallocimage(Rect(0,0,1,1), 1, 0x777777FF);
	viewp = buf;
	loops = buf;
	loope = bufe;
	redrawbg();
	pause = 0;
	Alt a[] = {
		{mc->resizec, nil, CHANRCV},
		{mc->c, &mc->Mouse, CHANRCV},
		{kc->c, &r, CHANRCV},
		{nil, nil, CHANEND}
	};
	if(threadcreate(athread, nil, mainstacksize) < 0)
		sysfatal("threadcreate: %r");
	for(;;){
		switch(alt(a)){
		case 0:
			if(getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			redrawbg();
			mo = mc->Mouse;
			break;
		case 1:
			switch(mc->buttons){
			case 1: setpos(); break;
			case 2: setloop(); break;
			case 4: setpan(mo.xy.x - mc->xy.x); break;
			case 8: setzoom(1); break;
			case 16: setzoom(-1); break;
			}
			mo = mc->Mouse;
			break;
		case 2:
			switch(r){
			case ' ':
				if(pause ^= 1)
					qlock(&lck);
				else
					qunlock(&lck);
				break;
			case 'b': bufp = loops; update(); break;
			case 'r': loops = buf; loope = bufe; drawview(); break;
			case Kdel:
			case 'q': threadexitsall(nil);
			case 'z': if(zoom == 1) break; zoom = 1; redrawbg(); break;
			case '-': setzoom(-1); break;
			case '=':
			case '+': setzoom(1); break;
			case 'w': writepcm(pause); break;
			}
			break;
		}
	}
}
