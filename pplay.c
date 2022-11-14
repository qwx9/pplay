#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include "dat.h"
#include "fns.h"

int stereo, zoom = 1;

static Keyboardctl *kc;
static Mousectl *mc;
static int cat;
static int afd = -1;
static uchar sbuf[Iochunksz];

static void
athread(void *)
{
	int nerr;
	uchar *p;
	usize n;

	nerr = 0;
	for(;;){
		if(afd < 0 || nerr > 10)
			return;
		if((p = getbuf(dot, Outsz, sbuf, &n)) == nil){
			fprint(2, "athread: %r\n");
			nerr++;
			continue;
		}
		if(write(afd, p, n) != n){
			fprint(2, "athread write: %r (nerr %d)\n", nerr);
			break;
		}
		nerr = 0;
		advance(&dot, n);
		update();
		yield();
	}
}

static void
toggleplay(void)
{
	static int play;

	if(play ^= 1){
		if((afd = cat ? 1 : open("/dev/audio", OWRITE)) < 0){
			fprint(2, "toggleplay: %r\n");
			play = 0;
			return;
		}
		if(threadcreate(athread, nil, mainstacksize) < 0)
			sysfatal("threadcreate: %r");
	}else{
		if(!cat)
			close(afd);
		afd = -1;
	}
}

static char *
prompt(Rune r)
{
	static char buf[512];

	snprint(buf, sizeof buf, "%C", r);
	if(enter(nil, buf, sizeof(buf)-UTFmax, mc, kc, _screen) < 0)
		return nil;
	return buf;
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
	if(loadin(fd) < 0)
		sysfatal("inittrack: %r");
	close(fd);
	initdrw();
	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");
	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	mo.xy = ZP;
	Alt a[] = {
		{mc->resizec, nil, CHANRCV},
		{mc->c, &mc->Mouse, CHANRCV},
		{kc->c, &r, CHANRCV},
		{nil, nil, CHANEND}
	};
	initcmd();
	if(setpri(13) < 0)
		fprint(2, "setpri: %r\n");
	toggleplay();
	for(;;){
		switch(alt(a)){
		case 0:
			if(getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			mo = mc->Mouse;
			redraw(1);
			break;
		case 1:
			if(eqpt(mo.xy, ZP))
				mo = mc->Mouse;
			switch(mc->buttons){
			case 1: setofs(mc->xy.x - screen->r.min.x); break;
			case 2: setloop(mc->xy.x - screen->r.min.x); break;
			case 4: setpan(mo.xy.x - mc->xy.x); break;
			case 8: setzoom(1, 1); break;
			case 16: setzoom(-1, 1); break;
			}
			mo = mc->Mouse;
			break;
		case 2:
			switch(r){
			case ' ': toggleplay(); break;
			case 'b': setjump(dot.from.pos); break;
			case Kesc: setrange(0, totalsz); update(); break;
			case Kdel:
			case 'q': threadexitsall(nil);
			case 'z': setzoom(-zoom + 1, 0); break;
			case '-': setzoom(-1, 0); break;
			case '=': setzoom(1, 0); break;
			case '_': setzoom(-1, 1); break;
			case '+': setzoom(1, 1); break;
			default:
				if((p = prompt(r)) == nil || strlen(p) == 0)
					break;
				if(cmd(p) < 0)
					fprint(2, "cmd \"%s\" failed: %r\n", p);
				break;
			}
			break;
		}
	}
}
