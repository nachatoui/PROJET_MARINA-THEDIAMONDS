all: Serveur1_2 Serveur3

Serveur1_2: Serveur1_2.c
	gcc Serveur1_2.c -o Serveur1_2

Serveur3: Serveur3.c
	gcc Serveur3.c -o Serveur3

clean:
	rm -f Serveur1_2
	rm -f Serveur3