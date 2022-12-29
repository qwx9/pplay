#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

/* chunk ≡ chain of chunks */
struct Buf{
	uchar *buf;
	usize bufsz;
	Ref;
};
static Chunk *norris;

static void
printchunks(Chunk *r)
{
	Chunk *c;

	fprint(2, "chunklist dot %zux %zux %zux: ", 
		dot.from.pos, dot.pos, dot.to.pos);
	c = r;
	do{
		fprint(2, "[%#p:%zd:←%#p→%#p] ", c, c->len, c->left, c->right);
		assert(c->right->left == c);
		c = c->right;
	}while(c != r);
	fprint(2, "\n");
}

static Chunk *
newchunk(Buf *b)
{
	Chunk *c;

	c = emalloc(sizeof *c);
	c->left = c;
	c->right = c;
	c->b = b;
	c->off = 0;
	c->len = b->bufsz;
	incref(&b->Ref);
	return c;
}
static Chunk *
newbuf(usize n)
{
	Buf *b;

	assert((n & 3) == 0);
	b = emalloc(sizeof *b);
	b->bufsz = n;
	b->buf = emalloc(b->bufsz);
	return newchunk(b);
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
unlink(Chunk *left, Chunk *right)
{
	left->left->right = right->right;
	right->right->left = left->left;
	left->left = right;
	right->right = left;
}

static Chunk *
clonechunk(Chunk *c)
{
	Chunk *nc;

	assert(c != nil && c->b != nil);
	nc = newchunk(c->b);
	nc->off = c->off;
	nc->len = c->len;
	incref(c->b);
	return nc;
}
Chunk *
replicate(Chunk *left, Chunk *right)
{
	Chunk *cl, *c, *nc;

	cl = clonechunk(left);
	c = cl;
	for(; left!=right;){
		left = left->right;
		nc = clonechunk(left);
		linkchunk(c, nc);
		c = nc;
	}
	return cl;
}

static void
freebuf(Buf *b)
{
	if(b == nil)
		return;
	free(b->buf);
	free(b);
}
static void
freechunk(Chunk *c)
{
	if(c == nil)
		return;
	if(c->b != nil && decref(c->b) == 0)
		freebuf(c->b);
	free(c);
}
void
freechain(Chunk *c)
{
	Chunk *cl, *cp;

	if(c == nil)
		return;
	for(cl=c->right; cl!=c; cl=cp){
		cp = cl->right;
		unlink(cl, cl);
		freechunk(cl);
	}
	freechunk(c);
}

static void
shrinkbuf(Chunk *c, usize newsz)
{
	Buf *b;

	b = c->b;
	assert(b != nil);
	assert(newsz < b->bufsz && newsz > 0);
	if(c->off + c->len > newsz)
		c->len = newsz - c->off;
	b->buf = erealloc(b->buf, newsz, b->bufsz);
}

#ifdef nope
static Chunk *
merge(Chunk *left, Chunk *right)
{
	uchar *u;
	Buf *l, *r;
	Chunk *nc;

	if(left == right)
		return left;
	l = left->b;
	r = right->b;
	assert(l != nil && r != nil);
	nc = newbuf(left->len + right->len);
	u = nc->b->buf;
	memcpy(u, l->buf+left->off, left->len);
	memcpy(u + left->len, r->buf+right->off, right->len);
	linkchunk(left->left, nc);
	unlink(left, right);
	freechain(left);
	return nc;
}

Chunk *
mergedot(usize *off)
{
	Chunk *left, *right;

	left = p2c(dot.from.pos, off);
	right = p2c(dot.to.pos, nil);
	if(left == right)
		return left;
	while(left->right != right)
		left = merge(left, left->right);
	return merge(left, right);
}
#endif

Chunk *
p2c(usize p, usize *off)
{
	int x;
	Chunk *c;

	for(c=norris, x=0; p>=c->len; c=c->right){
		if(c == norris && ++x > 1){
			c = norris->left;
			break;
		}
		p -= c->len;
	}
	if(off != nil)
		*off = p;
	return c;
}
usize
c2p(Chunk *tc)
{
	Chunk *c;
	usize p;

	for(p=0, c=norris; c!=tc; c=c->right)
		p += c->len;
	return p;
}

void
recalcsize(void)
{
	int n;

	n = c2p(norris->left) + norris->left->len;
	if(dot.to.pos == totalsz || dot.to.pos > n)
		dot.to.pos = n;
	if(dot.pos < dot.from.pos || dot.pos > dot.to.pos)
		dot.pos = dot.from.pos;
	totalsz = n;
}

#define ASSERT(x) {if(!(x)) printchunks(norris); assert((x)); }
void
paranoia(int exact)
{
	usize n;
	Chunk *c, *pc;
	Buf *b;

	ASSERT(dot.pos >= dot.from.pos && dot.pos < dot.to.pos);
	for(pc=norris, n=pc->len, c=pc->right; c!=norris; pc=c, c=c->right){
		b = c->b;
		ASSERT(b != nil);
		ASSERT((b->bufsz & 3) == 0 && b->bufsz >= Sampsz);
		ASSERT(c->off < b->bufsz);
		ASSERT(c->len > Sampsz);
		ASSERT(c->off + c->len <= b->bufsz);
		ASSERT(c->left == pc);
		n += c->len;
	}
	if(exact){
		ASSERT(n <= totalsz);
		ASSERT(dot.to.pos <= totalsz);
	}
}
#undef ASSERT

void
setdot(Dot *dot, Chunk *right)
{
	dot->from.pos = 0;
	if(right == nil)
		dot->to.pos = c2p(norris->left) + norris->left->len;
	else
		dot->to.pos = c2p(right);
}

Chunk *
splitchunk(Chunk *c, usize off)
{
	Chunk *nc;

	if(off == 0)
		return c;
	assert(off <= c->len);
	nc = clonechunk(c);
	nc->off = c->off + off;
	nc->len = c->len - off;
	c->len = off;
	linkchunk(c, nc);
	return nc;
}

/* c1 [nc … c2] nc */
int
splitrange(usize from, usize to, Chunk **left, Chunk **right)
{
	usize off;
	Chunk *c;

	c = p2c(from, &off);
	if(off > 0){
		splitchunk(c, off);
		*left = c->right;
	}else
		*left = c;	/* dangerous in combination with *right */
	c = p2c(to, &off);
	if(off < c->len - 1){
		splitchunk(c, off);
		*right = c;
	}else
		*right = c;
	return 0;
}

Chunk *
cutrange(usize from, usize to, Chunk **latch)
{
	Chunk *c, *left, *right;

	if(splitrange(from, to, &left, &right) < 0)
		return nil;
	c = left->left;
	if(left == norris)
		norris = c;
	unlink(left, right);
	if(latch != nil)
		*latch = left;
	return c;
}

Chunk *
croprange(usize from, usize to, Chunk **latch)
{
	Chunk *left, *right;

	if(splitrange(from, to, &left, &right) < 0)
		return nil;
	norris = left;
	*latch = right->right;
	unlink(right->right, left->left);
	return left;
}

// FIXME: generalized insert(from, to), where from and to not necessarily distinct
Chunk *
inserton(usize from, usize to, Chunk *c, Chunk **latch)
{
	Chunk *left;

	left = cutrange(from, to, latch);
	linkchunk(left, c);
	return left;
}

Chunk *
insertat(usize pos, Chunk *c)
{
	usize off;
	Chunk *left;

	if(pos == 0){
		left = norris->left;
		norris = c;
	}else{
		left = p2c(pos, &off);
		splitchunk(left, off);
	}
	linkchunk(left, c);
	return left;
}

uchar *
getslice(Dot *d, usize n, usize *sz)
{
	usize Δbuf, Δloop, off;
	Chunk *c;

	if(d->pos >= totalsz){
		werrstr("out of bounds");
		*sz = 0;
		return nil;
	}
	c = p2c(d->pos, &off);
	Δloop = d->to.pos - d->pos;
	Δbuf = c->len - off;
	if(n < Δloop && n < Δbuf){
		*sz = n;
		d->pos += n;
	}else if(Δloop <= Δbuf){
		*sz = Δloop;
		d->pos = d->from.pos;
	}else{
		*sz = Δbuf;
		d->pos += Δbuf;
	}
	return c->b->buf + c->off + off;
}

Chunk *
readintochunks(int fd)
{
	int n;
	usize m;
	Chunk *rc, *c, *nc;

	for(m=0, rc=c=nil;; m+=n){
		nc = newbuf(Iochunksz);
		if(rc == nil)
			rc = nc;
		else
			linkchunk(c, nc);
		c = nc;
		if((n = readn(fd, c->b->buf, Iochunksz)) < Iochunksz)
			break;
		yield();
	}
	close(fd);
	if(n < 0)
		fprint(2, "readintochunks: %r\n");
	else if(n == 0){
		if(c != rc)
			unlink(c, c);
		freechunk(c);
		if(c == rc){
			werrstr("readintochunks: nothing read");
			return nil;
		}
	}else if(n > 0 && n < Iochunksz)
		shrinkbuf(c, n);
	return rc;
}

void
graphfrom(Chunk *c)
{
	norris = c;
	recalcsize();
	setdot(&dot, nil);
}
