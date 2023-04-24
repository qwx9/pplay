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

// FIXME: crazy idea, multisnarf with addressable elements; $n registers; fork pplay to display them → ?

typedef struct Op Op;
struct Op{
	Chunk *p1;
	Chunk *p2;
	Chunk *l;
	Chunk *r;
};
static Op *opbuf, *ophead, *opend;
static usize opbufsz;

static struct{
	Chunk *from;
	usize foff;
	Chunk *to;
	usize toff;
} hold;

int
Δfmt(Fmt *fmt)
{
	Dot *d;

	d = va_arg(fmt->args, Dot*);
	if(d == nil)
		return fmtstrcpy(fmt, "[??:??:??:??]");
	return fmtprint(fmt, "[from=%08zux cur=%08zux to=%08zux]",
		d->from, d->pos, d->to);
}

int
χfmt(Fmt *fmt)
{
	Chunk *c;

	c = va_arg(fmt->args, Chunk*);
	if(c == nil)
		return fmtstrcpy(fmt, "[]");
	return fmtprint(fmt, "0x%08p:%08zux::0x%08p:0x%08p", c, c->len, c->left, c->right);
}

static void
printchunks(Chunk *r)
{
	usize len;
	Chunk *c;

	c = r;
	len = 0;
	do{
		fprint(2, "\t%χ\toff=%08zux\n", c, len);
		assert(c->right->left == c);
		len += c->len;
		c = c->right;
	}while(c != r);
	fprint(2, "\n");
}

void
dprint(Chunk *c, char *fmt, ...)
{
	char s[256];
	va_list arg;

	if(!debug)
		return;
	va_start(arg, fmt);
	vseprint(s, s+sizeof s, fmt, arg);
	va_end(arg);
	fprint(2, "%s", s);
	printchunks(c == nil ? norris : c);
}

static Chunk *
newchunk(Buf *b)
{
	Chunk *c;

	c = emalloc(sizeof *c);
	c->left = c;
	c->right = c;
	c->b = b;
	c->boff = 0;
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
	nc->boff = c->boff;
	nc->len = c->len;
	incref(c->b);
	return nc;
}
static Chunk *
clone(Chunk *left, Chunk *right)
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
static Chunk *
splitchunk(Chunk *p, usize from, usize to)
{
	Chunk *c;

	assert(from < p->len);
	assert(to > 0 && to - from <= p->len);
	c = clonechunk(p);
	c->boff += from;
	c->len = to - from;
	c->left = c->right = c;
	return c;
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

usize
chainlen(Chunk *c)
{
	usize n;
	Chunk *cp;

	for(cp=c, n=cp->len, cp=cp->right; cp!=c; cp=cp->right)
		n += cp->len;
	return n;
}

Chunk *
p2c(usize p, usize *off)
{
	Chunk *c;

	for(c=norris; p>=c->len; c=c->right){
		if(c == norris->left){
			assert(p == c->len);
			break;
		}
		p -= c->len;
	}
	if(off != nil)
		*off = p;
	return c;
}

static void
forgetop(Op *op)
{
	freechain(op->l);
}

int
unpop(char *)
{
	Op *op;

	if(opend == opbuf || ophead == opend)
		return 0;
	op = ophead++;
	dprint(op->p1, "cmd/unpop dot=%Δ P [%χ][%χ] LR [%χ][%χ]\n",
		&dot, op->p1, op->p2, op->l, op->r);
	totalsz += chainlen(op->l);
	linkchunk(op->p1->left, op->l);
	unlink(op->p1, op->p2);
	totalsz -= chainlen(op->p1);
	if(norris == op->p1)
		norris = op->l;
	dot.from = dot.pos = 0;
	dot.to = totalsz;
	return 1;
}

int
popop(char *)
{
	Op *op;

	if(ophead == opbuf)
		return 0;
	op = --ophead;
	dprint(op->l, "cmd/pop dot=%Δ P [%χ][%χ] LR [%χ][%χ]\n",
		&dot, op->p1, op->p2, op->l, op->r);
	totalsz += chainlen(op->p1);
	linkchunk(op->l->left, op->p1);
	unlink(op->l, op->r);
	totalsz -= chainlen(op->l);
	if(norris == op->l)
		norris = op->p1;
	dot.from = dot.pos = 0;
	dot.to = totalsz;
	return 1;
}

static void
pushop(Chunk *p1, Chunk *p2, Chunk *l, Chunk *r)
{
	Op *op;

	if(ophead == opbuf + opbufsz){
		opbuf = erealloc(opbuf,
			(opbufsz + 1024) * sizeof *opbuf,
			opbufsz * sizeof *opbuf);
		ophead = opbuf + opbufsz;
		opend = ophead;
		opbufsz += 1024;
	}
	if(opend > ophead){
		for(op=ophead; op<opend; op++)
			forgetop(op);
		memset(ophead, 0, (opend - ophead) * sizeof *ophead);
	}
	*ophead++ = (Op){p1, p2, l, r};
	opend = ophead;
}

void
ccrop(usize from, usize to)
{
	usize n, off;
	Chunk *p1, *p2, *l, *r;

	assert(from < to && to <= totalsz);
	p1 = p2c(from, &off);
	l = splitchunk(p1, off, p1->len);
	p2 = p2c(to, &off);
	r = splitchunk(p2, 0, off);
	linkchunk(p1, l);
	linkchunk(p2->left, r);
	unlink(p2, p1);
	n = chainlen(l);
	totalsz = n;
	pushop(p2, p1, r, l);
	norris = l;
	dot.pos -= dot.from;
	dot.from = 0;
	dot.to = n;
}

static int
creplace(usize from, usize to, Chunk *c)
{
	usize n, off;
	Chunk *p1, *p2, *l, *r;

	assert(from > 0 && from < to && to <= totalsz);
	p1 = p2c(from, &off);
	l = splitchunk(p1, 0, off);
	p2 = p2c(to, &off);
	r = splitchunk(p2, off, p2->len);
	linkchunk(c, r);
	linkchunk(l, c);
	n = chainlen(l);
	totalsz += n;
	linkchunk(p1->left, l);
	unlink(p1, p2);
	totalsz -= chainlen(p1);
	pushop(p1, p2, l, r);
	if(p1 == norris)
		norris = l;
	dot.to = dot.from + n;
	dot.pos = from;
	return 0;
}

// FIXME: use a specific Dot (prep for multibuf); generalize
static int
cinsert(usize pos, Chunk *c)
{
	usize n, off;
	Chunk *p, *l, *r;

	assert(pos <= totalsz);
	p = p2c(pos, &off);
	l = splitchunk(p, 0, off);
	r = splitchunk(p, off, p->len);
	linkchunk(c, r);
	linkchunk(l, c);
	n = chainlen(l);
	totalsz += n;
	linkchunk(p->left, l);
	unlink(p, p);
	totalsz -= chainlen(p);
	pushop(p, p, l, r);
	if(p == norris)
		norris = l;
	dot.to = dot.from + n;
	dot.pos = pos;
	return 0;
}

int
cpaste(usize from, usize to)
{
	Chunk *p1, *p2, *l, *r;

	if(hold.from == nil || hold.to == nil){
		werrstr("cpaste: nothing to paste");
		return -1;
	}
	p1 = hold.from;
	p2 = hold.to;
	if(p1 == p2)
		l = splitchunk(p1, hold.foff, hold.toff);
	else{
		l = splitchunk(p1, hold.foff, p1->len);
		r = splitchunk(p2, 0, hold.toff);
		if(p1->right != p2)
			linkchunk(l, clone(p1->right, p2->left));
		linkchunk(l->left, r);
	}
	return from == to ? cinsert(from, l) : creplace(from, to, l);
}

void
ccopy(usize from, usize to)
{
	hold.from = p2c(from, &hold.foff);
	hold.to = p2c(to, &hold.toff);
}

void
chold(Chunk *c)
{
	hold.from = hold.to = c;
	hold.foff = hold.toff = 0;
}

void
ccut(usize from, usize to)
{
	usize n;
	Chunk *p1, *p2, *l, *r;

	assert(from > 0 && from < to && to <= totalsz);
	ccopy(from, to);
	p1 = hold.from;
	r = splitchunk(p1, 0, hold.foff);
	p2 = hold.to;
	l = splitchunk(p2, hold.toff, p2->len);
	linkchunk(l, r);
	n = chainlen(l);
	totalsz += n;
	linkchunk(p1->left, l);
	unlink(p1, p2);
	totalsz -= chainlen(p1);
	pushop(p1, p2, l, r);
	if(p1 == norris)
		norris = l;
	dot.from = 0;
	dot.to = n;
	dot.pos = from;
}

uchar *
getslice(Dot *d, usize want, usize *have)
{
	usize n, off;
	Chunk *c;

	if(d->pos >= totalsz){
		werrstr("out of bounds");
		*have = 0;
		return nil;
	}
	c = p2c(d->pos, &off);
	n = c->len - off;
	*have = want > n ? n : want;
	return c->b->buf + c->boff + off;
}

Chunk *
chunkfile(int fd)
{
	int n;
	Chunk *c;
	Buf *b;

	c = newbuf(Chunksz);
	b = c->b;
	for(;;){
		if(b->bufsz - c->len < Chunksz){
			b->buf = erealloc(c->b->buf, 2 * c->b->bufsz, c->b->bufsz);
			b->bufsz *= 2;
		}
		if((n = readn(fd, b->buf + c->len, Chunksz)) < Chunksz)
			break;
		c->len += n;
		yield();
	}
	if(n < 0){
		fprint(2, "chunkfile: %r\n");
		freechunk(c);
		return nil;
	}else if(c->len == 0){
		fprint(2, "chunkfile: nothing read\n");
		freechunk(c);
		return nil;
	}else if(c->len < b->bufsz){
		b->buf = erealloc(b->buf, c->len, b->bufsz);
		b->bufsz = c->len;
	}
	return c;
}

void
initbuf(Chunk *c)
{
	norris = c;
	totalsz = chainlen(c);
	dot.pos = dot.from = 0;
	dot.to = totalsz;
}
