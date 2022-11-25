#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

/* stupidest implementation with the least amount of state to keep track of */

Dot dot;
usize totalsz_, totalsz;
static Chunk norris = {.left = &norris, .right = &norris};
static Chunk *held;
static uchar plentyofroom[Iochunksz];
static int cutheld;
static int epfd[2];

static void
printchunks(Chunk *r)
{
	Chunk *c;

	fprint(2, "chunklist dot %zux %zux %zux: ", 
		dot.from.pos, dot.pos, dot.to.pos);
	c = r;
	do{
		fprint(2, "%#p:%zux:←%#p→%#p - ", c, c->bufsz, c->left, c->right);
		assert(c->right->left == c);
		c = c->right;
	}while(c != r);
	fprint(2, "\n");
}

static Chunk *
newchunk(usize n)
{
	Chunk *c;

	assert((n & 3) == 0);
	c = emalloc(sizeof *c);
	c->bufsz = n;
	c->buf = emalloc(c->bufsz);
	c->left = c;
	c->right = c;
	return c;
}

static Chunk *
clonechunk(void)
{
	Chunk *c;

	assert(held != nil);
	c = newchunk(held->bufsz);
	memcpy(c->buf, held->buf, c->bufsz);
	return c;
}

static void
freechunk(Chunk *c)
{
	if(c == nil)
		return;
	free(c->buf);
	free(c);
}

static void
assertsize(void)
{
	Chunk *c;

	totalsz_ = 0;
	for(c=norris.right; c!=&norris; c=c->right)
		totalsz_ += c->bufsz;
	assert(totalsz_ == totalsz);
	assert(dot.to.pos <= totalsz);
}

static void
linkchunk(Chunk *left, Chunk *c)
{
	c->left->right = left->right;
	left->right->left = c->left;
	c->left = left;
	left->right = c;
	totalsz += c->bufsz;
}

static void
unlinkchunk(Chunk *c)
{
	c->left->right = c->right;
	c->right->left = c->left;
	c->left = c->right = nil;
	totalsz -= c->bufsz;
}

static void
resizechunk(Chunk *c, usize newsz)
{
	vlong Δ;

	Δ = newsz - c->bufsz;
	c->buf = erealloc(c->buf, newsz, c->bufsz);
	c->bufsz = newsz;
	if(c->right == &norris && Δ < 0)
		dot.to.pos += Δ;
	totalsz += Δ;
}

/* stupidest possible approach for now: minimal bookkeeping */
Chunk *
p2c(usize p, usize *off)
{
	Chunk *c;

	c = norris.right;
	while(p > c->bufsz){
		p -= c->bufsz;
		c = c->right;
	}
	if(off != nil)
		*off = p;
	assert(c != &norris);
	return c;
}
static usize
c2p(Chunk *tc)
{
	Chunk *c;
	usize p;

	p = 0;
	c = norris.right;
	while(c != tc){
		p += c->bufsz;
		c = c->right;
	}
	return p;
}

void
setrange(usize from, usize to)
{
	dot.from.pos = from;
	dot.to.pos = to;
	if(dot.pos < from || dot.pos >= to)
		dot.pos = from;
}

int
setpos(usize off)
{
	setrange(0, totalsz);
	assert(off >= dot.from.pos && off < dot.to.pos);
	dot.pos = off;
	return 0;
}

int
jump(usize off)
{
	if(off < dot.from.pos || off > dot.to.pos){
		werrstr("cannot jump outside of loop bounds\n");
		return -1;
	}
	dot.pos = off;
	return 0;
}

static int
holdchunk(Chunk *c, int cut)
{
	if(held != nil){
		if(held == c)
			return 0;
		else if(cutheld)
			freechunk(held);
	}
	held = c;
	cutheld = cut;
	if(cut){
		unlinkchunk(c);
		setpos(dot.from.pos);
	}
	return 0;
}

static Chunk *
merge(Chunk *left, Chunk *right)
{
	usize Δ;

	assert(right != &norris);
	if(left->buf == nil || right->buf == nil){
		werrstr("can\'t merge self into void");
		return nil;
	}
	if(left->buf != right->buf){
		Δ = left->bufsz;
		resizechunk(left, left->bufsz + right->bufsz);
		memmove(left->buf + Δ, right->buf, right->bufsz);
	}else{
		right->buf = nil;
		left->bufsz += right->bufsz;
	}
	unlinkchunk(right);
	freechunk(right);
	return 0;
}

static Chunk *
splitright(Chunk *left, usize off)
{
	usize Δ;
	Chunk *c;

	Δ = left->bufsz - off;
	if(off == 0 || Δ == 0)
		return left;
	c = newchunk(Δ);
	memcpy(c->buf, left->buf+off, Δ);
	resizechunk(left, off);
	linkchunk(left, c);
	return c;
}

static Chunk *
mergedot(usize *off)
{
	usize p;
	Chunk *c;

	c = p2c(dot.from.pos, &p);
	*off = p;
	p = dot.from.pos - p;
	while(p + c->bufsz < dot.to.pos)
		merge(c, c->right);
	return c;
}

/* before one may split oneself, one must first merge oneself */
static Chunk *
splitdot(void)
{
	usize p;
	Chunk *c;

	c = mergedot(&p);
	splitright(c, p + dot.to.pos - dot.from.pos);
	return splitright(c, p);
}

uchar *
getbuf(Dot d, usize n, uchar *scratch, usize *boff)
{
	uchar *bp, *p;
	usize Δbuf, Δloop, m, off, Δ;
	Chunk *c;

	c = p2c(d.pos, &off);
	p = c->buf + off;
	m = n;
	bp = scratch;
	while(m > 0){
		Δloop = d.to.pos - d.pos;
		Δbuf = c->bufsz - off;
		if(m < Δloop && m < Δbuf){
			Δ = m;
			memcpy(bp, p, Δ);
			d.pos += Δ;
		}else if(Δloop <= Δbuf){
			Δ = Δloop;
			memcpy(bp, p, Δ);
			d.pos = d.from.pos;
			c = p2c(d.from.pos, nil);
			off = 0;
			p = c->buf;
		}else{
			if(c == &norris)
				c = c->right;
			Δ = Δbuf;
			memcpy(bp, p, Δ);
			d.pos += Δ;
			c = c->right;
			off = 0; 
			p = c->buf;
		}
		bp += Δ;
		m -= Δ;
	}
	if(boff != nil)
		*boff = n;
	return scratch;
}

void
advance(Dot *d, usize n)
{
	usize Δ, Δbuf, Δloop, m, off;
	Chunk *c;

	c = p2c(d->pos, &off);
	m = n;
	while(m > 0){
		Δloop = d->to.pos - d->pos;
		Δbuf = c->bufsz - off;
		if(m < Δloop && m < Δbuf){
			d->pos += m;
			break;
		}else if(Δloop < Δbuf){
			Δ = Δloop;
			d->pos = d->from.pos;
			c = p2c(d->from.pos, nil);
			off = 0;
		}else{
			Δ = Δbuf;
			d->pos += Δ;
			c = c->right;
			off = 0;
		}
		m -= Δ;
	}
}

static int
insert(char *, Chunk *c)
{
	usize p;
	Chunk *left;

	if(c == nil && (c = clonechunk()) == nil){
		werrstr("insert: no buffer");
		return -1;
	}
	left = p2c(dot.pos, &p);
	splitright(left, p);
	linkchunk(left, c);
	setrange(dot.pos, dot.pos + c->bufsz);
	return 1;
}

static int
copy(char *)
{
	Chunk *c;

	c = splitdot();
	holdchunk(c, 0);
	return 0;
}

static int
cut(char *)
{
	Chunk *c;

	if(dot.from.pos == 0 && dot.to.pos == totalsz){
		werrstr("cut: no range selected");
		return -1;
	}
	c = splitdot();
	holdchunk(c, 1);
	return 1;
}

static int
replace(char *, Chunk *c)
{
	Chunk *left, *right;

	if(c == nil && (c = clonechunk()) == nil){
		werrstr("replace: no buffer");
		return -1;
	}
	right = splitdot();
	left = right->left;
	unlinkchunk(right);
	freechunk(right);
	right = left->right;
	linkchunk(left, c);
	setrange(dot.from.pos, right != &norris ? c2p(right) : totalsz);
	return 1;
}

static int
paste(char *s, Chunk *c)
{
	if(dot.from.pos == 0 && dot.to.pos == totalsz)
		return insert(s, c);
	return replace(s, c);
}

static int
crop(char *)
{
	usize Δ;
	Chunk *c, *d;

	Δ = 0;
	for(c=norris.right; c!=&norris; c=d){
		if(Δ + c->bufsz >= dot.from.pos)
			break;
		d = c->right;
		Δ += c->bufsz;
		unlinkchunk(c);
		freechunk(c);
	}
	dot.from.pos -= Δ;
	dot.to.pos -= Δ;
	if(dot.from.pos > 0){
		Δ = c->bufsz - dot.from.pos;
		memmove(c->buf, c->buf + dot.from.pos, Δ);
		resizechunk(c, Δ);
		dot.to.pos -= dot.from.pos;
		dot.from.pos = 0;
	}
	for(Δ=0; c!=&norris; Δ+=c->bufsz, c=c->right)
		if(Δ + c->bufsz >= dot.to.pos)
			break;
	if(dot.to.pos > 0)
		resizechunk(c, dot.to.pos);
	for(c=c->right; c!=&norris; c=d){
		d = c->right;
		unlinkchunk(c);
		freechunk(c);
	}
	dot.pos = 0;
	dot.to.pos = totalsz;
	return 1;
}

static int
forcemerge(char *)
{
	usize p;

	if(dot.from.pos == 0 && dot.to.pos == totalsz){
		werrstr("merge: won\'t implicitely merge entire buffer\n");
		return -1;
	}
	mergedot(&p);
	return 0;
}

static Chunk *
readintochunks(int fd)
{
	int n;
	usize m;
	Chunk *rc, *c, *nc;

	for(m=0, rc=c=nil;; m+=n){
		nc = newchunk(Iochunksz);
		if(rc == nil)
			rc = nc;
		else
			linkchunk(c, nc);
		c = nc;
		if((n = readn(fd, c->buf, Iochunksz)) < Iochunksz)
			break;
		yield();
	}
	close(fd);
	if(n < 0)
		fprint(2, "readintochunks: %r\n");
	else if(n == 0){
		if(c != rc)
			unlinkchunk(c);
		freechunk(c);
		if(c == rc){
			werrstr("readintochunks: nothing read");
			return nil;
		}
	}else if(n > 0 && n < Iochunksz){
		resizechunk(c, n);
		/* kludge! first chunk still unlinked */
		if(m < Iochunksz)
			totalsz += Iochunksz - c->bufsz;
	}
	return rc;
}

static int
readfrom(char *s)
{
	int fd;
	Chunk *c;

	if((fd = open(s, OREAD)) < 0)
		return -1;
	c = readintochunks(fd);
	close(fd);
	if(c == nil)
		return -1;
	return paste(nil, c);
}

static int
writebuf(int fd)
{
	int nio;
	usize n, m, c, k;
	uchar *p;
	Dot d;

	d.pos = d.from.pos = dot.from.pos;
	d.to.pos = dot.to.pos;
	if((nio = iounit(fd)) == 0)
		nio = 8192;
	nio = MIN(nio, sizeof plentyofroom);
	for(m=d.to.pos-d.from.pos, c=0; m>0;){
		k = nio < m ? nio : m;
		if((p = getbuf(d, k, plentyofroom, &k)) == nil){
			fprint(2, "writebuf: couldn\'t get a buffer: %r\n");
			return -1;
		}
		if((n = write(fd, p, k)) != k){
			fprint(2, "writebuf: short write not %zd: %r\n", k);
			return -1;
		}
		m -= n;
		d.pos += n;
		c += n;
	}
	write(fd, plentyofroom, 0);	/* close pipe */
	return 0;
}

static void
rc(void *s)
{
	close(epfd[1]);
	dup(epfd[0], 0);
	dup(epfd[0], 1);
	close(epfd[0]);
	procexecl(nil, "/bin/rc", "rc", "-c", s, nil);
	sysfatal("procexec: %r");
}

static void
wproc(void *efd)
{
	int fd;

	fd = (intptr)efd;
	writebuf(fd);
	close(fd);
	threadexits(nil);
}
static void
rthread(void *efd)
{
	int fd;
	Chunk *c;

	fd = (intptr)efd;
	if((c = readintochunks(fd)) == nil)
		threadexits("readintochunks: %r");
	close(fd);
	paste(nil, c);
	assertsize();
	redraw(0);
	threadexits(nil);
}

static int
pipeline(char *arg, int rr, int wr)
{
	assertsize();
	if(pipe(epfd) < 0)
		sysfatal("pipe: %r");
	if(procrfork(rc, arg, mainstacksize, RFFDG|RFNOTEG|RFNAMEG) < 0)
		sysfatal("procrfork: %r");
	close(epfd[0]);
	if(wr && procrfork(wproc, (int*)dup(epfd[1], -1), mainstacksize, RFFDG) < 0){
		fprint(2, "threadcreate: %r\n");
		return -1;
	}
	if(rr && threadcreate(rthread, (int*)dup(epfd[1], -1), mainstacksize) < 0){
		fprint(2, "threadcreate: %r\n");
		return -1;
	}
	close(epfd[1]);
	return 0;
}

static int
pipeto(char *arg)
{
	return pipeline(arg, 0, 1);
}

static int
pipefrom(char *arg)
{
	return pipeline(arg, 1, 0);
}

static int
pipethrough(char *arg)
{
	return pipeline(arg, 1, 1);
}

/* the entire string is treated as the filename, ie.
 * spaces and any other weird characters will be part
 * of it */
static int
writeto(char *arg)
{
	int r, fd;

	if(dot.to.pos - dot.from.pos == 0){
		werrstr("writeto: dot isn't a range");
		return -1;
	}
	if((fd = create(arg, OWRITE, 0664)) < 0){
		werrstr("writeto: %r");
		return -1;
	}
	r = writebuf(fd);
	close(fd);
	return r;
}

int
cmd(char *s)
{
	int n;
	Rune r, r´;

	/* FIXME: avoid potential conflicts with keys in main() */
	assert(s != nil);
	s += chartorune(&r, s);
	for(;;){
		n = chartorune(&r´, s);
		if(r´ == Runeerror){
			werrstr("malformed input");
			return -1;
		}
		if(r´ == 0 || r´ != ' ' && r´ != '\t')
			break;
		s += n;
	}
	assertsize();
	switch(r){
	case '<': return pipefrom(s);
	case '^': return pipethrough(s);
	case '|': return pipeto(s);
	case 'c': return copy(s);
	case 'd': return cut(s);
	case 'm': return forcemerge(s);
	case 'p': return paste(s, nil);
	case 'r': return readfrom(s);
	case 'w': return writeto(s);
	case 'x': return crop(s);
	default: werrstr("unknown command %C", r); break;
	}
	return -1;
}

int
loadin(int fd)
{
	Chunk *c;

	if((c = readintochunks(fd)) == nil)
		sysfatal("loadin: %r");
	linkchunk(&norris, c);
	setrange(0, totalsz);
	return 0;
}

static void
catch(void *, char *msg)
{
	if(strstr(msg, "closed pipe"))
		noted(NCONT);
	noted(NDFLT);
}

void
initcmd(void)
{
	notify(catch);
}
