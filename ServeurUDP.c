#include <stdio.h>
#include <stdlib.h>
// entête pour la socket
#include <sys/types.h>  
#include <sys/socket.h>
// manipulation des adresse sockaddr
#include <string.h>
#include <netinet/in.h>
// htons..
#include <arpa/inet.h>
// pour close
#include <unistd.h>
// https://www.educative.io/answers/how-to-implement-udp-sockets-in-c
// pour le wait 
#include<sys/wait.h>

#define BUFFSIZE 500
#define SYN "SYN"
#define ACK "ACK"
#define SYN_ACK "SYN-ACK"
#define FIN "FIN"

int check(int exp, const char *msg);
char* Num_Sequence(int num_seq, char* char_num_seq);
int Creation_Socket (int port, struct sockaddr_in server_addr);
void ACK_num_seq(char *str);

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        perror("nombre d'argument incorrect. exmeple : ./serveur 3000"); 
        return (-1); 
    }

    int PORT = atoi(argv[1]);
    printf("PORT %d \n", PORT);

    int socket_desc, Sous_socket, num_client = 1;
    struct sockaddr_in server_addr, client_addr, ss_addr;
    char server_message[BUFFSIZE], client_message[BUFFSIZE];
    int client_struct_length = sizeof(client_addr);
    int cwnd_taille = 10; 
    
    // Vide les buffers:
    memset(server_message, '\0', BUFFSIZE);
    memset(client_message, '\0', BUFFSIZE);
    
    // Creation socket UDP :
    socket_desc = Creation_Socket (PORT, server_addr);

    // Three-way handshake avec un client:
    int nvx_port = PORT + num_client;

    char buffer_SYN_ACK[13];
    memset(buffer_SYN_ACK, '\0', 13);    

    sprintf(buffer_SYN_ACK, "%s%d", SYN_ACK, nvx_port);
    printf("syn ack message : %s\n", buffer_SYN_ACK);

    // recvfrom est bloquant 
    recvfrom(socket_desc, client_message, BUFFSIZE, 0,
            (struct sockaddr*)&client_addr, &client_struct_length);
    if(strncmp("SYN", client_message, 3) == 0)
    {
        sendto(socket_desc, buffer_SYN_ACK, strlen(buffer_SYN_ACK), 0,
            (struct sockaddr*)&client_addr, client_struct_length);
    
        recvfrom(socket_desc, client_message, BUFFSIZE, 0,
                (struct sockaddr*)&client_addr, &client_struct_length);

        if(strncmp("ACK", client_message, 3) == 0)
        {
            printf("reception du ACK :) /n");

            // Creation socket UDP directe avec le client:
            int Sous_socket = Creation_Socket (nvx_port, ss_addr);
            num_client += 1; 

            // Protocole connecté !
            printf("Bien connecté ! \n");
            printf("Client : @IP: %s et du port: %i\n",
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            
            //Fichier demandé par le client :
            memset(client_message, '\0', BUFFSIZE); 
            recvfrom(Sous_socket, client_message, BUFFSIZE, 0,
                            (struct sockaddr*)&client_addr, &client_struct_length);
            printf("Nom du fichier demandé : %s \n", client_message);

            // Envoie d'un fichier:
            FILE *fp = fopen(client_message,"r");
            if(fp == NULL) {
                perror ("Error in opening file");
                exit(-1);
            }
            memset(server_message, '\0', BUFFSIZE);
            int num_seq = 0;
            char char_num_seq[6]; 
            char lecture[BUFFSIZE-6];
            /* RTT provisoire */
            struct timeval tv;
            struct timeval tv1;
            
            char buffer_last_Ack_Recu[6];
            long last_Ack_Recu;

            fd_set rset;
            FD_ZERO(&rset);
            int nready;

            while ( 1 ) { 
                if (num_seq == last_Ack_Recu) {
                    break;
                }
                while (cwnd_taille != 0){
                    if (! feof(fp)) {
                        num_seq += 1;
                        memset(server_message, '\0', BUFFSIZE);
                        memset(lecture, '\0', BUFFSIZE-6);
                        fread(lecture, 1, BUFFSIZE-6, fp);
                        Num_Sequence(num_seq, char_num_seq);
                        fflush(fp);
                        sprintf(server_message, "%s%s", char_num_seq, lecture);

                        sendto(Sous_socket, server_message, BUFFSIZE, 0,
                            (struct sockaddr*)&client_addr, client_struct_length) ;
                        printf("message envoyé n° %d !\n", num_seq);
                        cwnd_taille -- ; 
                    }

                    FD_SET(Sous_socket, &rset);
                    tv1.tv_sec = 0;
                    tv1.tv_usec = 0; 
                    nready = select(Sous_socket+1, &rset, NULL, NULL, &tv1); // empeche de bloquer au receivefrom
                    if (FD_ISSET(Sous_socket, &rset)) { 
                        // On a un ACK qui est arrivé 
                        memset(client_message, '\0', BUFFSIZE);
                        if (recvfrom(Sous_socket, client_message, BUFFSIZE, 0,
                            (struct sockaddr*)&client_addr, &client_struct_length) < 0){
                            printf("Erreur lors de la reception\n");
                            return -1;
                        }
                        printf("%s\n", client_message);
                        ACK_num_seq(client_message);
                        strcpy(buffer_last_Ack_Recu,client_message);
                        last_Ack_Recu = strtol(buffer_last_Ack_Recu, NULL, 10 ); //atoi
                        cwnd_taille ++ ;
                    }
                    if (num_seq == last_Ack_Recu) {
                        break;
                    }
                }
                // On a envoyé tous les messages possibles en fonction de la taille de notre fenêtre 
                tv.tv_sec = 2;
                tv.tv_usec = 0;
                FD_SET(Sous_socket, &rset);
                nready = select(Sous_socket+1, &rset, NULL, NULL, &tv);
                // On reste bloqué en attendant la fin du timeout afin de voir si le message pourra être ACK
                if (FD_ISSET(Sous_socket, &rset)) { 
                    memset(client_message, '\0', BUFFSIZE);
                    if (recvfrom(Sous_socket, client_message, BUFFSIZE, 0,
                        (struct sockaddr*)&client_addr, &client_struct_length) < 0){
                        printf("Erreur lors de la reception\n");
                        return -1;
                    }
                    printf("%s\n", client_message);
                    ACK_num_seq(client_message);
                    strcpy(buffer_last_Ack_Recu,client_message);
                    last_Ack_Recu = strtol(buffer_last_Ack_Recu, NULL, 10 );
                    cwnd_taille ++ ;
                } else {
                    // Paquets perdus => Retransmission 
                    fseek(fp, last_Ack_Recu*(BUFFSIZE-6), SEEK_SET); // regarde à partir du début du fichier (à modif par la suite pour plus de perf)
                    fread(lecture, 1, BUFFSIZE-6, fp);
                    fflush(fp);
                    sprintf(server_message, "%s%s", char_num_seq, lecture);

                    sendto(Sous_socket, server_message, BUFFSIZE, 0,
                        (struct sockaddr*)&client_addr, client_struct_length) ;
                    // num_seq += 1; // A modifier avec les ACK cumulatif 
                } 
            }
            fclose(fp);
            sendto(Sous_socket, FIN, strlen(FIN), 0,
                    (struct sockaddr*)&client_addr, client_struct_length);
            close(Sous_socket);
        } else {
            printf("erreur Threeway handshake\n");
        }
        close(socket_desc);
        return 0;
    }
}

int check(int exp, const char *msg){
    if (exp == (-1) ) {
        perror(msg);
        exit(1);
    }
    return exp;
}

char* Num_Sequence(int num_seq, char* char_num_seq){
    if(num_seq > 999999){
        return NULL;
    }
    char k[6];
    sprintf(k, "%d",num_seq);
    int len_k = strlen(k);
    char ki [6] ="000000";
    sprintf(ki+6-len_k,"%d",num_seq);
    sprintf(char_num_seq,"%s",ki);
    return char_num_seq;
}

int Creation_Socket (int port, struct sockaddr_in server_addr)
{      
    // Creation socket UDP :
    int descripteur_socket;
    check((descripteur_socket = socket(AF_INET, SOCK_DGRAM, 0)), 
        "Échec de la création du socket");
    printf("Socket créée avec succès !\n");
    
    // Fixe port & IP:
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    // Bind aux port & @IP:
    check(bind(descripteur_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)), 
        "Bind Failed!"); 
    printf("Bind réussie\n");

    return (descripteur_socket);
}

void ACK_num_seq(char *str){
    int x = 0;
    while(str[x] != '\0'){
        str[x] = str[x+3];
        x++;
    }
}
