CC = gcc
DA_OBJ = master.o patient.o command.o fifo.o msg.o tools.o vector.o list.o tree.o \
         hashtable.o
WS_OBJ = whoserver.o command.o tools.o vector.o msg.o cirq_buffer.o
WC_OBJ = whoclient.o tools.o vector.o msg.o 
CFLAGS = -g -Wall

all: master whoServer whoClient

master: $(DA_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

master.o: master.c
	$(CC) $(CFLAGS) -o $@ -c $<

command.o: command.c command.h
	$(CC) $(CFLAGS) -o $@ -c $<

patient.o: patient.c patient.h
	$(CC) $(CFLAGS) -o $@ -c $<

fifo.o: fifo.c fifo.h
	$(CC) $(CFLAGS) -o $@ -c $<

tools.o: tools.c tools.h
	$(CC) $(CFLAGS) -o $@ -c $<

vector.o: vector.c vector.h
	$(CC) $(CFLAGS) -o $@ -c $<

list.o: list.c list.h
	$(CC) $(CFLAGS) -o $@ -c $<

tree.o: tree.c tree.h
	$(CC) $(CFLAGS) -o $@ -c $<

hashtable.o: hashtable.c hashtable.h
	$(CC) $(CFLAGS) -o $@ -c $<


whoServer: $(WS_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

whoserver.o: whoserver.c
	$(CC) $(CFLAGS) -o $@ -c $<


whoClient: $(WC_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

whoclient.o: whoclient.c
	$(CC) $(CFLAGS) -o $@ -c $<

cirq_buffer.o: cirq_buffer.c cirq_buffer.h
	$(CC) $(CFLAGS) -o $@ -c $<

msg.o: msg.c msg.h
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	rm -rf master whoServer whoClient $(DA_OBJ) $(WS_OBJ) $(WC_OBJ)
