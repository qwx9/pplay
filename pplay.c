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
int debug, paused, notriob;

static Keyboardctl *kc;
static Mousectl *mc;
static int cat;
static Channel *crm114;

static void
aproc(void *)
{
	int afd, nerr;
	uchar *b, *bp, buf[Outsz];
	usize n, m;

	Alt a[] = {
		{crm114, nil, CHANRCV},
		{nil, nil, CHANEND}
	};
	nerr = 0;
	afd = -1;
	paused = 1;
	for(;;){
again:
		if(nerr > 10)
			break;
		switch(alt(a)){
		case 0:
			if(paused ^= 1){
				if(!cat)
					close(afd);
				afd = -1;
			}else if((afd = cat ? 1 : open("/dev/audio", OWRITE)) < 0){
				fprint(2, "aproc open: %r\n");
				paused = 1;
			}
			if(afd < 0){
				a[1].op = CHANEND;
				continue;
			}else
				a[1].op = CHANNOBLK;
			break;
		case -1:
			fprint(2, "alt: %r\n");
			break;
		}
		for(bp=buf, m=sizeof buf; bp<buf+sizeof buf; bp+=n, m-=n){
			if((b = getslice(&dot, m, &n)) == nil || n <= 0){
				fprint(2, "aproc: %r\n");
				nerr++;
				goto again;
			}
			memcpy(bp, b, n);
			advance(n);
			refresh(Drawcur);
		}
		if(write(afd, buf, sizeof buf) != sizeof buf){
			fprint(2, "aproc write: %r\n");
			nerr++;
			sendul(crm114, 1UL);
		}else
			nerr = 0;
	}
	threadexits(nil);
}

static void
toggleplay(void)
{
	sendul(crm114, 1UL);
}

static char *
prompt(Rune r)
{
	Rune q;
	static char buf[512];

	chartorune(&q, buf);
	if(q != r)
          snprint(buf, sizeof buf, "%C", r);
	if(enter("cmd:", buf, sizeof(buf)-UTFmax, mc, kc, _screen) < 0)
		return nil;
	return buf;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-bc] [pcm]\n", argv0);
	threadexits("usage");
}

void
threadmain(int argc, char **argv)
{
	char *p;
	Mouse mo;
	Rune r;

	notriob = 0;
	ARGBEGIN{
	case 'D': debug = 1; break;
	case 'b': notriob = 1; break;
	case 'c': cat = 1; break;
	default: usage();
	}ARGEND
	fmtinstall(L'Δ', Δfmt);
	fmtinstall(L'χ', χfmt);
	fmtinstall(L'τ', τfmt);
	if(*argv != nil)
		while(*argv != nil)
			addtrack(*argv++);
	else
		addtrack(nil);
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
	if((crm114 = chancreate(sizeof(ulong), 2)) == nil)
		sysfatal("chancreate: %r");
	if(proccreate(aproc, nil, 16*1024) < 0)
		sysfatal("threadcreate: %r");
	toggleplay();
	for(;;){
		switch(alt(a)){
		case 0:
			lockdisplay(display);
			if(getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			unlockdisplay(display);
			redraw(1);
			mo = mc->Mouse;
			break;
		case 1:
			if(eqpt(mo.xy, ZP))
				mo = mc->Mouse;
			switch(mc->buttons){
			case 1: setjump(view2ss(mc->xy.x - screen->r.min.x)); break;
			case 2: setloop(view2ss(mc->xy.x - screen->r.min.x)); break;
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
			case 'D': debugdraw ^= 1; refresh(Drawrender); break;
			case 'S': stereo ^= 1; refresh(Drawall); break;
			case ' ': toggleplay(); break;
			case 'b': setjump(dot.from); break;
			case Kesc: setrange(0, dot.totalsz); break;
			case '\n': zoominto(dot.from, dot.to); break;
			case 'z': zoominto(0, dot.totalsz); break;
			case '-': setzoom(-1, 0); break;
			case '=': setzoom(1, 0); break;
			case '_': setzoom(-1, 1); break;
			case '+': setzoom(1, 1); break;
			case Kleft: setpage(-1); break;
			case Kright: setpage(1); break;
			default:
				if((p = prompt(r)) == nil || strlen(p) == 0){
					refresh(Drawrender);
					break;
				}
				switch(cmd(p)){
				case -1: fprint(2, "cmd \"%s\" failed: %r\n", p); break;
				case 0: refresh(Drawall); break;
				case 1: redraw(0); break;
				case 2: redraw(1); break;
				}
			}
		}
	}
}
