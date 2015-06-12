outdir:=bin/
sourcedir:=src/

all:
	gcc -pthread ${sourcedir}client.c -Wall -o ${outdir}client
	gcc ${sourcedir}server.c -Wall -o ${outdir}server

clean:
	rm -f ${outdir}client ${outdir}server
