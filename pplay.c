#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include "dat.h"
#include "fns.h"

enum{
	Ndelay = 44100 / 25,
	Nchunk = Ndelay * 4,
	Nreadsz = 4*1024*1024,
};

ulong nbuf;
uchar *buf, *bufp, *bufe, *viewp, *viewe, *viewmax, *loops, *loope;
int T;
int stereo;
int zoom = 1;

static Keyboardctl *kc;
static Mousectl *mc;
static QLock lck;
static int pause;
static int cat;

static void
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

static char *
prompt(void)
{
	int n;
	static char buf[256];

	memset(buf, 0, sizeof buf);
	if(!pause)
		qlock(&lck);
	n = enter("path:", buf, sizeof(buf)-UTFmax, mc, kc, nil);
	if(!pause)
		qunlock(&lck);
	if(n < 0)
		return nil;
	return buf;
}

static void
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

static void
setpan(int n)
{
	n *= T * 4 * 16;
	if(zoom == 1 || viewp == buf && n < 0 || viewp == viewmax && n > 0)
		return;
	viewp += n;
	redrawbg();
}

static void
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

static void
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

static void
bufrealloc(ulong n)
{
	int off;

	off = bufp - buf;
	if((buf = realloc(buf, n)) == nil)
		sysfatal("realloc: %r");
	bufe = buf + n;
	bufp = buf + off;
}

static void
initbuf(int fd)
{
	int n, sz;

	bufrealloc(nbuf += Nreadsz);
	if((sz = iounit(fd)) == 0)
		sz = 8192;
	while((n = read(fd, bufp, sz)) > 0){
		bufp += n;
		if(bufp + sz >= bufe)
			bufrealloc(nbuf += Nreadsz);
	}
	if(n < 0)
		sysfatal("read: %r");
	bufrealloc(nbuf = bufp - buf);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-cs] [pcm]\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char **argv)
{
	int fd;
	char *p;
	Mouse mo;
	Rune r;

	ARGBEGIN{
	case 'c': cat = 1; break;
	case 's': stereo = 1; break;
	default: usage();
	}ARGEND
	if((fd = *argv != nil ? open(*argv, OREAD) : 0) < 0)
		sysfatal("open: %r");
	initbuf(fd);
	close(fd);
	nbuf /= 4;
	bufp = buf;
	viewp = buf;
	loops = buf;
	loope = bufe;
	initview();
	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");
	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
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
			case 'w':
				if((p = prompt()) != nil)
					writepcm(p);
				break;
			}
			break;
		}
	}
}
