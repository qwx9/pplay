#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include "dat.h"
#include "fns.h"

extern QLock lsync;

int stereo;
int debug, paused = 1;

static Keyboardctl *kc;
static Mousectl *mc;
static int cat;
static int afd = -1;

static void
athread(void *)
{
	int nerr;
	uchar *b, *bp, buf[Outsz];
	usize n, m;

	nerr = 0;
	for(;;){
		if(afd < 0 || nerr > 10)
			return;
		for(bp=buf, m=sizeof buf; bp<buf+sizeof buf; bp+=n, m-=n){
			if((b = getslice(current, m, &n)) == nil || n <= 0){
				fprint(2, "athread: %r\n");
				nerr++;
				goto skip;
			}
			memcpy(bp, b, n);
			advance(current, n);
		}
		if(write(afd, buf, sizeof buf) != sizeof buf){
			fprint(2, "athread write: %r\n");
			threadexits("write");
		}
		nerr = 0;
		update(0, 0);
skip:
		yield();
	}
}

static void
toggleplay(void)
{
	if(paused ^= 1){
		if(!cat)
			close(afd);
		afd = -1;
	}else{
		if((afd = cat ? 1 : open("/dev/audio", OWRITE)) < 0){
			fprint(2, "toggleplay: %r\n");
			paused ^= 1;
			return;
		}
		if(threadcreate(athread, nil, 2*mainstacksize) < 0)
			sysfatal("threadcreate: %r");
	}
}

static char *
prompt(Rune r)
{
	Rune q;
	static char buf[512];

	chartorune(&q, buf);
	if(q != r)
          snprint(buf, sizeof buf, "%C", r);
	if(enter(nil, buf, sizeof(buf)-UTFmax, mc, kc, nil) < 0)
		return nil;
	return buf;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-Dbc] [pcm]\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char **argv)
{
	int fd, notriob;
	char *p;
	Mouse mo;
	Rune r;

	notriob = 0;
	ARGBEGIN{
	case 'D': debug = 1; debugdraw = 1; break;
	case 'b': notriob = 1; break;
	case 'c': cat = 1; break;
	default: usage();
	}ARGEND
	fmtinstall(L'Δ', Δfmt);
	fmtinstall(L'χ', χfmt);
	fmtinstall(L'τ', τfmt);
	if((fd = *argv != nil ? open(*argv, OREAD) : 0) < 0)
		sysfatal("open: %r");
	if(initcmd(fd) < 0)
		sysfatal("init: %r");
	close(fd);
	initdrw(notriob);
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
			case Kdel:
			case 'q': threadexitsall(nil);
			case 'D': debug ^= 1; debugdraw ^= 1; break;
			case 'S': stereo ^= 1; redraw(1); break;
			case ' ': toggleplay(); break;
			case 'b': setjump(current->from); break;
			case Kesc: setrange(0, current->totalsz); update(0, 0); break;
			case '\n': zoominto(current->from, current->to); break;
			case 'z': zoominto(0, current->totalsz); break;
			case '-': setzoom(-1, 0); break;
			case '=': setzoom(1, 0); break;
			case '_': setzoom(-1, 1); break;
			case '+': setzoom(1, 1); break;
			case Kleft: setpage(-1); break;
			case Kright: setpage(1); break;
			default:
				if((p = prompt(r)) == nil || strlen(p) == 0)
					break;
				if(treadsoftly){
					fprint(2, "dropping edit command during ongoing read\n");
					break;
				}
				qlock(&lsync);
				switch(cmd(p)){
				case -1: fprint(2, "cmd \"%s\" failed: %r\n", p); update(0, 0); break;
				case 0: update(0, 0); break;
				case 1: redraw(0); break;
				case 2: redraw(1); break;
				}
				qunlock(&lsync);
			}
		}
	}
}
