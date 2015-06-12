outdir:=bin/
sourcedir:=src/

all:
	gcc -pthread ${sourcedir}client.c ${sourcedir}sockaddr_cmp.c -Wall -o ${outdir}client
	gcc ${sourcedir}server.c -Wall -o ${outdir}server

clean:
	rm -f ${outdir}client ${outdir}server
