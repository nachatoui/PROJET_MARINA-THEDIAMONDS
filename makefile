all: Serveur1 Serveur2 Serveur3

Serveur1: Serveur1.c
	gcc Serveur1.c -o Serveur1

Serveur2: Serveur2.c
	gcc Serveur2.c -o Serveur2

Serveur3: Serveur3.c
	gcc Serveur3.c -o Serveur3

clean:
	rm -f Serveur1
	rm -f Serveur2
	rm -f Serveur3