typedef struct Chunk Chunk;
typedef struct Pos Pos;
typedef struct Dot Dot;
typedef struct Buf Buf;

enum{
	Rate = 44100,
	WriteRate = 25,
	WriteDelay = Rate / WriteRate,	/* 1764 default delay */
	Sampsz = 2 * 2,
	Outsz = WriteDelay * Sampsz,
	Iochunksz = 4*1024*1024,	/* ≈ 24 sec. at 44.1 kHz */
};
#pragma incomplete Buf
struct Chunk{
	Buf *b;
	usize off;
	usize len;
	Chunk *left;
	Chunk *right;
};
struct Pos{
	usize pos;	/* bytes */
};
extern struct Dot{
	Pos;
	Pos from;
	Pos to;
};
extern Dot dot;
extern usize totalsz;
extern int treadsoftly;

extern int stereo;
extern int debug;
extern int debugdraw;

#define MIN(x,y)	((x) < (y) ? (x) : (y))
#define MAX(x,y)	((x) > (y) ? (x) : (y))
