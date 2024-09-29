#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include "dat.h"
#include "fns.h"

extern QLock lsync;

int stereo, chan;
int debug, paused, notriob;
int reader = -1;
Channel *pidc;

static Keyboardctl *kc;
static Mousectl *mc;
static int cat;
static Channel *crm114;
static int pids[32];
int nslots = nelem(pids);

void
killreader(void)
{
	if(reader <= 0)
		return;
	postnote(PNGROUP, reader, "kill");
}

static void
killemall(void)
{
	int i, pid;

	for(i=0; i<nelem(pids); i++)
		if((pid = pids[i]) >= 0)
			postnote(PNGROUP, pid, "kill");
}

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
	int n;
	Rune q;
	static char buf[512];

	chartorune(&q, buf);
	if(q != r)
		snprint(buf, sizeof buf, "%C", r);
	lockdisplay(display);
	n = enter("cmd:", buf, sizeof(buf)-UTFmax, mc, kc, nil);
	unlockdisplay(display);
	return n < 0 ? nil : buf;
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
	int i, pid;
	char *p;
	Mouse m, mo;
	Channel *waitc;
	Waitmsg *w;
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
			appendfile(*argv++);
	else
		appendfile(nil);
	initdrw(notriob);
	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");
	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	mo.xy = ZP;
	for(i=0; i<nelem(pids); i++)
		pids[i] = -1;
	if(setpri(13) < 0)
		fprint(2, "setpri: %r\n");
	if((crm114 = chancreate(sizeof(ulong), 2)) == nil
	|| (pidc = chancreate(sizeof(int), 1)) == nil)
		sysfatal("chancreate: %r");
	if(proccreate(aproc, nil, 16*1024) < 0)
		sysfatal("threadcreate: %r");
	toggleplay();
	waitc = threadwaitchan();
	enum{
		Aresize,
		Amouse,
		Akey,
		Apid,
		Await,
	};
	Alt a[] = {
		[Aresize] {mc->resizec, nil, CHANRCV},
		[Amouse] {mc->c, &mc->Mouse, CHANRCV},
		[Akey] {kc->c, &r, CHANRCV},
		[Apid] {pidc, &pid, CHANRCV},
		[Await] {waitc, &w, CHANRCV},
		{nil, nil, CHANEND}
	};
	for(;;){
		switch(alt(a)){
		case Aresize:
			mo = mc->Mouse;
			lockdisplay(display);
			if(getwindow(display, Refnone) < 0)
				sysfatal("resize failed: %r");
			unlockdisplay(display);
			redraw(1);
			break;
		case Amouse:
			m = mc->Mouse;
			if(mo.msec == 0)
				mo = m;
			switch(mc->buttons){
			case 1: setjump(view2ss(m.xy.x - screen->r.min.x)); break;
			case 2: setloop(view2ss(m.xy.x - screen->r.min.x)); break;
			case 4: setpan(mo.xy.x - m.xy.x); break;
			case 5: setzoom(m.xy.y - mo.xy.y, mo.xy.x - screen->r.min.x); m.xy.x = mo.xy.x; break;
			case 8: setzoom(1.0, mo.xy.x - screen->r.min.x); m.xy.x = mo.xy.x; break;
			case 16: setzoom(-1.0, mo.xy.x - screen->r.min.x); m.xy.x = mo.xy.x; break;
			}
			mo = m;
			break;
		case Akey:
			switch(r){
			case ' ': toggleplay(); break;
			case Kesc: setrange(0, dot.totalsz); break;
			case '\n': zoominto(dot.from, dot.to); break;
			case '\t': chan = chan + 1 & 1; break;
			case '-': setzoom(-20.0, mo.xy.x - screen->r.min.x); m.xy.x = mo.xy.x; break;
			case '=': setzoom(20.0, mo.xy.x - screen->r.min.x); m.xy.x = mo.xy.x; break;
			case '_': setzoom(-1.0, mo.xy.x - screen->r.min.x); m.xy.x = mo.xy.x; break;
			case '+': setzoom(1.0, mo.xy.x - screen->r.min.x); m.xy.x = mo.xy.x; break;
			case '1': bound = 0; break;
			case '2': bound = 1; break;
			case 'S': stereo ^= 1; redraw(1); break;
			case 'b': setjump(dot.from); break;
			case 't': samptime ^= 1; break;
			case 'z': zoominto(0, dot.totalsz); break;
			case Kleft: setpage(-1); break;
			case Kright: setpage(1); break;
			case 'D': killreader(); break;
			case Kdel: killemall(); break;
			case 'q': threadexitsall(nil);
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
				default: break;
				}
			}
			break;
		case Apid:
			if(pid < 0){
				fprint(2, "process exited with error\n");
				break;
			}
			for(i=0; i<nelem(pids); i++)
				if(pids[i] < 0){
					pids[i] = pid;
					break;
				}
			assert(i < nelem(pids));
			if(reader == 0)
				reader = pid;
			nslots--;
			break;
		case Await:
			for(i=0; i<nelem(pids); i++)
				if(pids[i] == w->pid){
					pids[i] = -1;
					break;
				}
			if(i == nelem(pids))
				fprint(2, "phase error -- no such pid %d\n", w->pid);
			if(w->pid == reader)
				reader = -1;
			nslots++;
			free(w);
			break;
		}
	}
}
