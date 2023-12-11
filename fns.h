void	dprint(Chunk*, char*, ...);
void	freechain(Chunk*);
int	unpop(char*);
int	popop(char*);
int	cpaste(Dot*);
Chunk*	ccopy(Dot*);
void	chold(Chunk*, Dot*);
void	ccut(Dot*);
void	ccrop(Dot*);
Chunk*	loadfile(int, Dot*);
void	quit(void);
int	cmd(char*);
void	addtrack(char*);
void	resizetracks(void);
void	refresh(int);
void	setzoom(int, int);
int	zoominto(vlong, vlong);
void	setrange(usize, usize);
int	setloop(vlong);
void	setpan(int);
void	setpage(int);
int	setjump(vlong);
vlong	ss2view(int);
vlong	view2ss(int);
void	redraw(int);
void	initdrw(int);
void	advance(usize);
void	setcurrent(Punkt);
Chunk*	p2c(usize, usize*, Dot*);
int	setpos(usize);
uchar*	getslice(Dot*, usize, usize*);
vlong	getbuf(Dot, usize, uchar*, usize);
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
