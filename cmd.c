#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

/* stupidest implementation with the least amount of state to keep track of */

Dot dot;
usize totalsz;
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
		fprint(2, "%#p:%zux ", c, c->bufsz);
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
recalcsize(void)
{
	Chunk *c;

	totalsz = 0;
	for(c=norris.right; c!=&norris; c=c->right)
		totalsz += c->bufsz;
}

static void
linkchunk(Chunk *left, Chunk *c)
{
	c->left->right = left->right;
	left->right->left = c->left;
	c->left = left;
	left->right = c;
}

static void
unlinkchunk(Chunk *c)
{
	c->left->right = c->right;
	c->right->left = c->left;
	c->left = c->right = nil;
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
	if(off < dot.from.pos || off > dot.to.pos){
		werrstr("cannot jump outside of loop bounds\n");
		return -1;
	}
	setrange(0, totalsz);
	dot.pos = off;
	return 0;
}

void
jump(usize off)
{
	dot.pos = off;
}

static int
holdchunk(Chunk *c, int cut)
{
	if(held != nil){
		if(held == c)
			return 0;
		else if(cutheld){
			unlinkchunk(held);
			freechunk(held);
		}
	}
	held = c;
	cutheld = cut;
	if(cut){
		setpos(dot.from.pos);
		unlinkchunk(held);
	}
	return 0;
}

static Chunk *
merge(Chunk *left, Chunk *right)
{
	if(left->buf == nil || right->buf == nil){
		werrstr("can\'t merge self into void");
		return nil;
	}
	if(left->buf != right->buf){
		left->buf = erealloc(left->buf, left->bufsz + right->bufsz, left->bufsz);
		memmove(left->buf + left->bufsz, right->buf, right->bufsz);
	}else
		right->buf = nil;
	left->bufsz += right->bufsz;
	unlinkchunk(right);
	freechunk(right);
	return 0;
}

static Chunk *
splitright(Chunk *left, usize off)
{
	usize p, Δ;
	Chunk *c;

	Δ = left->bufsz - off;
	c = newchunk(Δ);
	memcpy(c->buf, left->buf+off, Δ);
	linkchunk(left, c);
	left->buf = erealloc(left->buf, off, left->bufsz);
	left->bufsz = off;
	p = c2p(c);
	setrange(p, p+Δ);
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
	recalcsize();
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

	c = splitdot();
	recalcsize();
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
	recalcsize();
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
		erealloc(c->buf, Δ, c->bufsz);
		c->bufsz = Δ;
		dot.to.pos -= dot.from.pos;
		dot.from.pos = 0;
	}
	for(Δ=0; c!=&norris; Δ+=c->bufsz, c=c->right)
		if(Δ + c->bufsz >= dot.to.pos)
			break;
	if(dot.to.pos > 0){
		erealloc(c->buf, dot.to.pos, c->bufsz);
		c->bufsz = dot.to.pos;
	}
	for(c=c->right; c!=&norris; c=d){
		d = c->right;
		unlinkchunk(c);
		freechunk(c);
	}
	recalcsize();
	dot.pos = 0;
	dot.to.pos = totalsz;
	return 1;
}

static int
forcemerge(char *)
{
	usize p;

	mergedot(&p);
	return 0;
}

static Chunk *
readintochunks(int fd)
{
	int n, m;
	usize off;
	Chunk *rc, *c, *nc;

	rc = newchunk(Iochunksz);
	for(off=0, m=0, c=rc;; m+=n, off+=n){
		if(off == Iochunksz){
			nc = newchunk(Iochunksz);
			linkchunk(c, nc);
			c = nc;
			off = 0;
		}
		if((n = read(fd, c->buf+off, Ioreadsz)) <= 0)
			break;
	}
	close(fd);
	if(n < 0){
		fprint(2, "readintochunks: %r\n");
		if(m == 0){
			freechunk(rc);
			return nil;
		}
	}
	c->buf = erealloc(c->buf, off, c->bufsz);
	c->bufsz = off;
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
	usize n, m;
	uchar *p;
	Dot d;

	d.pos = d.from.pos = dot.from.pos;
	d.to.pos = dot.to.pos;
	for(m=d.to.pos-d.from.pos; m>0;){
		n = sizeof plentyofroom < m ? sizeof plentyofroom : m;
		if((p = getbuf(d, n, plentyofroom, &n)) == nil){
			fprint(2, "writebuf: getbuf won't feed\n");
			return -1;
		}
		if((n = write(fd, p, m)) != n){
			fprint(2, "writebuf: short write not %zd\n", n);
			return -1;
		}
		m -= n;
	}
	return 0;
}

static void
rc(void *s)
{
	dup(epfd[0], 0);
	dup(epfd[1], 1);
	close(epfd[0]);
	close(epfd[1]);
	procexecl(nil, "/bin/rc", "rc", "-c", s, nil);
	sysfatal("procexec: %r");
}

static int
pipeline(char *arg, int rr, int wr)
{
	Chunk *c;

	if(pipe(epfd) < 0)
		sysfatal("pipe: %r");
	if(procrfork(rc, arg, mainstacksize, RFFDG|RFNOTEG|RFNAMEG) < 0)
		sysfatal("procrfork: %r");
	if(wr)
		writebuf(epfd[1]);
	close(epfd[1]);
	if(rr){
		if((c = readintochunks(epfd[0])) == nil)
			return -1;
		paste(nil, c);
	}
	close(epfd[0]);
	return 1;
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
	recalcsize();
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
