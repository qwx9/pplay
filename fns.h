void	dprint(Chunk*, char*, ...);
void	checksz(void);
void	freechain(Chunk*);
int	unpop(char*);
int	popop(char*);
int	cpaste(usize, usize);
void	ccopy(usize, usize);
void	chold(Chunk*);
void	ccut(usize, usize);
void	ccrop(usize, usize);
Chunk*	chunkfile(int);
void	initbuf(Chunk*);
int	cmd(char*);
void	initcmd(void);
void	update(void);
void	setzoom(int, int);
int	zoominto(vlong, vlong);
void	setrange(usize, usize);
void	setloop(vlong);
void	setofs(usize);
void	setpan(int);
void	setpage(int);
void	setjump(usize);
void	redraw(int);
void	initdrw(int);
int	advance(Dot*, usize);
Chunk*	p2c(usize, usize*);
int	setpos(usize);
uchar*	getslice(Dot*, usize, usize*);
vlong	getbuf(Dot, usize, uchar*, usize);
int	loadin(int);
int	χfmt(Fmt*);
int	Δfmt(Fmt*);
int	τfmt(Fmt*);
void*	emalloc(usize);
void*	erealloc(void*, usize, usize);
char*	estrdup(char*);
int	setpri(int);

#pragma	varargck	argpos	dprint	2
#pragma	varargck	type	"χ"	Chunk*
#pragma	varargck	type	"Δ"	Dot*
#pragma	varargck	type	"τ"	usize
