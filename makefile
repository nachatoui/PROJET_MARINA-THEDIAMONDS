all: ServeurUDP 

ServeurUDP: ServeurUDP.c
	gcc ServeurUDP.c -o ServeurUDP

clean:
	rm -f ServeurUDP