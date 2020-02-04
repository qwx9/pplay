#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

void
writepcm(char *path)
{
	int n, fd, sz;
	vlong ofs;
	uchar *p;

	if((fd = create(path, OWRITE, 0664)) < 0){
		fprint(2, "create: %r\n");
		return;
	}
	if((sz = iounit(fd)) == 0)
		sz = 8192;
	if(file)
		seek(ifd, loops, 0);
	p = pcmbuf;
	for(ofs=loops; ofs<loope; ofs+=sz){
		n = loope - ofs < sz ? loope - ofs : sz;
		if(!file)
			p = pcmbuf + ofs;
		else if(read(ifd, p, n) != n)
			fprint(2, "read: %r\n");
		if(write(fd, p, n) != n){
			fprint(2, "write: %r\n");
			break;
		}
	}
	close(fd);
	if(file)
		seek(ifd, seekp, 0);
}
