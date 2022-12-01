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
#include <sys/time.h>

#define BUFFSIZE 1500 //MTU ou 1460 - A modifier lors optimisation
#define SYN "SYN"
#define ACK "ACK"
#define SYN_ACK "SYN-ACK"
#define FIN "FIN"
#define ALPHA 0.4 //- A modifier lors optimisation -- si réseau stable, petite valeur de α = 0.4 sinon grande valeur α = 0.9 par exemple

#define max(x, y) (((x) > (y)) ? (x) : (y))
#define min(x, y) (((x) < (y)) ? (x) : (y))

int check(int exp, const char *msg);
char* Num_Sequence(int num_seq, char* char_num_seq);
int Creation_Socket (int port, struct sockaddr_in server_addr);
void ACK_num_seq(char *str);
double differencetemps (struct timeval t0,struct timeval t1);
double SRTT (double prev_SRTT, double prev_RTT);

// creation d'un dictionnaire -- regarde pour chaque segement à quel temps il a été envoyé afin de calculer le rtt
typedef struct {
    int key_num_seq;
    struct timeval value_temps_envoie; 
} temps_send ;

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        perror("nombre d'argument incorrect. exmeple : ./serveur 3000"); 
        return (-1); 
    }

    int PORT = atoi(argv[1]);

    int socket_desc, Sous_socket, num_client = 1;
    struct sockaddr_in server_addr, client_addr, ss_addr;
    char server_message[BUFFSIZE], client_message[BUFFSIZE];
    int client_struct_length = sizeof(client_addr);
    int cwnd_taille = 10; // A modifier lors optimisation
    
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
            char char_num_seq[6]; 
            char lecture[BUFFSIZE-6];
            
            struct timeval RetransmissionTimeout;
            struct timeval TimerNul;  
            
            char buffer_last_Ack_Recu[6];
            long last_Ack_Recu;

            fd_set rset;
            FD_ZERO(&rset);
            int nready;

            int num_seq = 0;
            int Flight_Size = 0;
            int SlowStartSeuil = 1024; // A modifier lors optimisation 
            double rtt; 
            double srtt; 
            struct timeval rtt_t0;
            struct timeval rtt_t1;
            temps_send tmp_envoie[300]; 
            long ACK_previous;

            int compteur_ACK_DUP = 0;

            while ( 1 ) { 
                while ( (cwnd_taille - Flight_Size ) != 0){
                    if (! feof(fp)) {
                        num_seq += 1;
                        memset(server_message, '\0', BUFFSIZE);
                        memset(lecture, '\0', BUFFSIZE-6);
                        fread(lecture, 1, BUFFSIZE-6, fp);
                        Num_Sequence(num_seq, char_num_seq);
                        fflush(fp);
                        sprintf(server_message, "%s%s", char_num_seq, lecture);

                        sendto(Sous_socket, server_message, strlen(server_message), 0,
                            (struct sockaddr*)&client_addr, client_struct_length) ;
                        gettimeofday(&rtt_t0,0);
                        printf("message envoyé n° %d !\n", num_seq);
                        tmp_envoie[num_seq].key_num_seq = num_seq;
                        tmp_envoie[num_seq].value_temps_envoie = rtt_t0;
                        Flight_Size ++ ; 
                    }

                    FD_SET(Sous_socket, &rset);
                    TimerNul.tv_sec = 0;
                    TimerNul.tv_usec = 0; 
                    nready = select(Sous_socket+1, &rset, NULL, NULL, &TimerNul); // empeche de bloquer au receivefrom
                    if (FD_ISSET(Sous_socket, &rset)) { 
                        // On a un ACK qui est arrivé 
                        memset(client_message, '\0', BUFFSIZE);
                        if (recvfrom(Sous_socket, client_message, BUFFSIZE, 0,
                            (struct sockaddr*)&client_addr, &client_struct_length) < 0){
                            printf("Erreur lors de la reception\n");
                            return -1;
                        }
                        gettimeofday(&rtt_t1,0);
                        printf("%s\n", client_message);
                        ACK_num_seq(client_message);
                        strcpy(buffer_last_Ack_Recu,client_message);
                        ACK_previous = last_Ack_Recu;
                        last_Ack_Recu = strtol(buffer_last_Ack_Recu, NULL, 10 ); //atoi
                        if (last_Ack_Recu == ACK_previous) {
                            compteur_ACK_DUP ++;
                        }
                        if (compteur_ACK_DUP == 4){
                            // FAST RETRANSMIT
                            cwnd_taille = cwnd_taille / 2; 
                        }
                        rtt = differencetemps(tmp_envoie[last_Ack_Recu].value_temps_envoie, rtt_t1);
                        printf("N° seq %ld, RTT : %f \n", last_Ack_Recu, rtt); 
                        if (last_Ack_Recu == 1){
                            srtt = rtt;
                            printf("SRTT : %f \n", srtt);
                        } else {
                            srtt = SRTT(srtt, rtt); 
                        }
                        Flight_Size -- ;
                        if (cwnd_taille < SlowStartSeuil)
                        {
                            //slow start
                            cwnd_taille ++;
                        }  else {
                            //congestion avoidance
                            cwnd_taille = cwnd_taille + 1/cwnd_taille;
                        }

                    }
                    if (num_seq == last_Ack_Recu) {
                        break;
                    }
                }
                if (num_seq == last_Ack_Recu) {
                    break;
                }
                // On a envoyé tous les messages possibles en fonction de la taille de notre fenêtre 
                RetransmissionTimeout.tv_sec = min(5,max(1,(1,3*srtt))); /* checké pour trouver val optimal  */
                RetransmissionTimeout.tv_usec = 0;
                printf("RetransmissionTimeout %ld.%06lds \n", RetransmissionTimeout.tv_sec, RetransmissionTimeout.tv_usec);
                
                FD_SET(Sous_socket, &rset);
                nready = select(Sous_socket+1, &rset, NULL, NULL, &RetransmissionTimeout);
                // On reste bloqué en attendant la fin du timeout afin de voir si le message pourra être ACK
                if (FD_ISSET(Sous_socket, &rset)) { 
                    memset(client_message, '\0', BUFFSIZE);
                    if (recvfrom(Sous_socket, client_message, BUFFSIZE, 0,
                        (struct sockaddr*)&client_addr, &client_struct_length) < 0){
                        printf("Erreur lors de la reception\n");
                        return -1;
                    }
                    gettimeofday(&rtt_t1,0);
                    printf("%s\n", client_message);
                    ACK_num_seq(client_message);
                    strcpy(buffer_last_Ack_Recu,client_message);
                    last_Ack_Recu = strtol(buffer_last_Ack_Recu, NULL, 10 );
                    rtt = differencetemps(tmp_envoie[last_Ack_Recu].value_temps_envoie, rtt_t1);
                    printf("N° seq %ld, RTT : %f \n", last_Ack_Recu, rtt); 
                    if (last_Ack_Recu == 1){
                        srtt = rtt;
                        printf("SRTT : %f \n", srtt);
                    } else {
                        srtt = SRTT(srtt, rtt); 
                    }
                    Flight_Size -- ;
                    if (cwnd_taille < SlowStartSeuil)
                    {
                        //slow start
                        cwnd_taille ++;
                    }  else {
                        //congestion avoidance
                        cwnd_taille = cwnd_taille + 1/cwnd_taille;
                    }
                } else {
                    // Paquets perdus => Retransmission 
                    printf("paquets perdus..\n");
                    fseek(fp, last_Ack_Recu*(BUFFSIZE-6), SEEK_SET); // regarde à partir du début du fichier (à modif par la suite pour plus de perf)
                    fread(lecture, 1, BUFFSIZE-6, fp);
                    fflush(fp);
                    sprintf(server_message, "%s%s", char_num_seq, lecture);

                    sendto(Sous_socket, server_message, BUFFSIZE, 0,
                        (struct sockaddr*)&client_addr, client_struct_length);
                    gettimeofday(&rtt_t0,0);
                    printf("message réenvoyé n° %ld !\n", last_Ack_Recu+1);
                    tmp_envoie[last_Ack_Recu+1].key_num_seq = last_Ack_Recu+1;
                    tmp_envoie[last_Ack_Recu+1].value_temps_envoie = rtt_t0;
                    cwnd_taille = cwnd_taille / 2; //NewReno
                    // A gérer si dès la première perte ? 
                    SlowStartSeuil = Flight_Size / 2;
                    // num_seq += 1; // A modifier avec les ACK cumulatif !
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
    server_addr.sin_addr.s_addr = INADDR_ANY; // inet_addr("192.168.0.18") - 10.43.8.92
    
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

double differencetemps (struct timeval t0,struct timeval t1){
    long seconds = t1.tv_sec - t0.tv_sec;
    long microsec = t1.tv_usec - t0.tv_usec;
    double rtt = seconds + microsec*1e-6;
    return rtt;
}

double SRTT (double prev_SRTT, double prev_RTT)
{
    double srtt = ALPHA*prev_SRTT + (1-ALPHA)*prev_RTT;
    printf("SRTT : %f \n", srtt);
    return srtt;
}