CFLAGS=-Wall -I..
LDFLAGS=-L..
LIBS=-lusb-1.0 -lpthread

panda: panda.o ../sim_frontpanel.o ../sim_sock.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)
