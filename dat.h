enum{
	Ndelay = 44100 / 25,
	Nchunk = Ndelay * 4,
	Nreadsz = 4*1024*1024,
};

extern int ifd;
extern uchar *pcmbuf;
extern vlong filesz, nsamp;

extern vlong seekp, T, loops, loope;

extern int file, stereo;
extern int zoom;
