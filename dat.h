typedef struct Chunk Chunk;
typedef struct Dot Dot;
typedef struct Buf Buf;

typedef intptr ssize;

enum{
	Rate = 44100,
	WriteRate = 25,
	WriteDelay = Rate / WriteRate,	/* 1764 default delay */
	Sampsz = 2 * 2,
	Outsz = WriteDelay * Sampsz,
	Chunksz = Sampsz * Rate,
};
#pragma incomplete Buf
struct Chunk{
	Buf *b;
	usize boff;
	usize len;
	Chunk *left;
	Chunk *right;
};
struct Dot{
	ssize from;
	ssize to;
	ssize cur;
	ssize off;
	ssize totalsz;
	Chunk *norris;
};
extern Dot dot;
enum{
	Bstart,
	Bend,
};
extern int bound;

extern QLock lsync;

enum{
	Rcur = 1<<0,
	Rrender = 1<<1,
	Rsamp = 1<<2,
	Rview = 1<<3 | Rsamp,
	Rreset = 1<<4,
	Rredraw = Rrender | Rsamp | Rreset,
	Rall = 0x7fffffff,
};

extern int stereo, chan;
extern int debug, paused;
extern int samptime;
extern int nslots;
extern int reader;

#define MIN(x,y)	((x) < (y) ? (x) : (y))
#define MAX(x,y)	((x) > (y) ? (x) : (y))
