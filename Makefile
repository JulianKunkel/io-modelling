all: io-model.c mem-eater.c
	mpicc io-model.c  -std=gnu99 -O3 -g3  -lrt -o io-model -DVERSION="\"$(shell git log -1 --format=%h)\""

clean:
	rm io-model
