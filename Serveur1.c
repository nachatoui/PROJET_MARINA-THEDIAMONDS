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
#include <time.h>

#define BUFFSIZE 1500 // ou MSS 536 RFC 879
#define SYN "SYN"
#define ACK "ACK"
#define SYN_ACK "SYN-ACK"
#define FIN "FIN"
#define ALPHA 0.4 //  -- si réseau stable, petite valeur de α = 0.4 sinon grande valeur α = 0.9 par exemple

#define max(x, y) (((x) > (y)) ? (x) : (y))
#define min(x, y) (((x) < (y)) ? (x) : (y))

// creation d'un dictionnaire -- Il va stocker le temps d'envoi pour chaque segement afin de calculer le rtt
typedef struct {
    int key_num_seq;
    struct timeval value_temps_envoi; 
} temps_send ;

int check(int exp, const char *msg);
char* Num_Sequence(int num_seq, char* char_num_seq);
int Creation_Socket (int port, struct sockaddr_in server_addr);
void ACK_num_seq(char *str);
double differencetemps (struct timeval t0,struct timeval t1);
double SRTT (double prev_SRTT, double prev_RTT);
void remplissage_server_message (char server_message[], char lecture[], char char_num_seq[], int nread);
void envoie_message(FILE *fp, int num_seq, char server_message[], char lecture[], char char_num_seq[], int Sous_socket, struct sockaddr_in client_addr, struct timeval rtt_t0, temps_send tmp_envoi[]);
long reception_message(char client_message[], int Sous_socket, struct sockaddr_in client_addr, int client_struct_length, struct timeval *rtt_t1, char buffer_last_Ack_Recu[], temps_send tmp_envoi[], long ACK_prededent, int *compteur_ACK_DUP, double *rtt, double *srtt);
long FastRetransmit (FILE *fp, int *compteur_ACK_DUP, int *SlowStartSeuil, int *cwnd_taille, long last_Ack_Recu, char server_message[], char lecture[], char char_num_seq[], int Sous_socket, struct sockaddr_in client_addr, struct timeval rtt_t0, temps_send tmp_envoi[], int Flight_Size);

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        perror("nombre d'argument incorrect. exmeple : ./serveur 3000"); 
        return (-1); 
    }

    int PORT = atoi(argv[1]);

    // Creation socket UDP :
    int socket_desc, Sous_socket;
    struct sockaddr_in server_addr, client_addr, ss_addr;
    int client_struct_length = sizeof(client_addr);
    socket_desc = Creation_Socket (PORT, server_addr);

    char server_message[BUFFSIZE], client_message[BUFFSIZE];
    // Vide les buffers:
    memset(server_message, '\0', BUFFSIZE);
    memset(client_message, '\0', BUFFSIZE);
    
    //monitoring
    double temps_exec;
    struct timeval start, end;
    float debit;

    // Three-way handshake avec un client:
    int nvx_port = PORT + 1;

    char buffer_SYN_ACK[12];
    memset(buffer_SYN_ACK, '\0', 12);     
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
            Sous_socket = Creation_Socket (nvx_port, ss_addr);

            // Protocole connecté !
            printf("Bien connecté ! \n");
            printf("Client : @IP: %s et du port: %i\n",
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            
            //Fichier demandé par le client :
            memset(client_message, '\0', BUFFSIZE); 
            recvfrom(Sous_socket, client_message, BUFFSIZE, 0,
                            (struct sockaddr*)&client_addr, &client_struct_length);
            printf("Nom du fichier demandé : %s \n", client_message);
            FILE *fp = fopen(client_message,"rb");
            if(fp == NULL) {
                perror ("Error in opening file");
                exit(-1);
            }

            // On recupère la taille & le nombre de segment a envoyer:
            fseek (fp, 0, SEEK_END);
            long taille_fichier = ftell(fp);
            fseek (fp, 0, SEEK_SET);    
            int last_num_seq_fichier = 100000; 

            // creation de toutes les variables nécessaires
            int cwnd_taille = 5; 
            char char_num_seq[6];  //stocke le num de sequence sur 6 bits
            char lecture[BUFFSIZE-6]; // stocke la partie du ficher que l'on souhaite envoyer sur 1494 bit
            
            struct timeval RetransmissionTimeout, TimerNul;  
    
            char buffer_last_Ack_Recu[6]; // stocke la valeur du dernier ACK reçu
            long last_Ack_Recu = 0;     // pareil mais en long

            fd_set rset;
            FD_ZERO(&rset);
            int nready;     // si notre select est ready 
            int fin_envoie=0;

            int num_seq = 0;
            int Flight_Size = 0;
            int SlowStartSeuil = 1024; 
            double rtt; 
            double srtt; 
            struct timeval rtt_t0, rtt_t1; // rtt_t0: temps envoie du seg - rtt_t1: temps recpetion du seg 
            temps_send tmp_envoi[300000]; //stocker les temps d'envoie en fonction du n° de segemt
            long ACK_prededent; // stocke la valeur du ACK precedent pour savoir s'il est duplique
            int compteur_ACK_DUP = 0;
            long num_dernier_seg_reenvoye;
            int nread; // cette fois c'est pour recuperer la taille du segment à envoyer (le dernier + petit)

            gettimeofday(&start,0); 
            // On commence l'envoie
            while ( last_Ack_Recu != last_num_seq_fichier ) {                  
                while ( ( (cwnd_taille - Flight_Size) > 0 ) && (fin_envoie != 1)){ 
                    // J'envoie sans attendre de ACK 
                    if (! feof(fp)) {
                        num_seq += 1;
                        envoie_message(fp, num_seq, server_message, lecture, char_num_seq, Sous_socket, client_addr, rtt_t0, tmp_envoi);
                        Flight_Size += 1 ; 
                        printf("message envoyé n° %d !\n", num_seq);
                    } else {
                        last_num_seq_fichier = num_seq;
                        fin_envoie = 1;
                    }
                    FD_SET(Sous_socket, &rset);
                    TimerNul.tv_sec = 0;
                    TimerNul.tv_usec = 0; 
                    // je regarde si un ACK est arrivé 
                    nready = select(Sous_socket+1, &rset, NULL, NULL, &TimerNul); // empeche de bloquer au receivefrom
                    if (FD_ISSET(Sous_socket, &rset)) { 
                        ACK_prededent = last_Ack_Recu;
                        last_Ack_Recu = reception_message(client_message, Sous_socket, client_addr, client_struct_length, &rtt_t1, buffer_last_Ack_Recu, tmp_envoi, ACK_prededent, &compteur_ACK_DUP , &rtt, &srtt);
                        Flight_Size -= 1 ;
                        if (compteur_ACK_DUP == 3) { 
                            if (last_Ack_Recu != (num_dernier_seg_reenvoye-1)) //suppose un même pkt pas perdu 2 fois ? - evite trop de renvoie
                            {
                                num_dernier_seg_reenvoye = FastRetransmit (fp, &compteur_ACK_DUP, &SlowStartSeuil, &cwnd_taille, last_Ack_Recu, server_message, lecture, char_num_seq, Sous_socket, client_addr, rtt_t0, tmp_envoi, Flight_Size);
                                fseek(fp, num_seq*(BUFFSIZE-6), SEEK_SET);
                            } else {
                                compteur_ACK_DUP = 0;
                                if (cwnd_taille < SlowStartSeuil)
                                {
                                    //slow start
                                    cwnd_taille +=1;
                                }  else {
                                    //congestion avoidance
                                    cwnd_taille = cwnd_taille + 1/max(1,cwnd_taille);
                                }
                            }
                        } else {
                            if (cwnd_taille < SlowStartSeuil)
                            {
                                //slow start
                                cwnd_taille += 1;
                            }  else {
                                //congestion avoidance
                                cwnd_taille = cwnd_taille + 1/max(1,cwnd_taille);
                            }
                        }
                    } 
                }
                // On a envoyé tous les messages possibles en fonction de la taille de notre fenêtre => Att un ACK 

                RetransmissionTimeout.tv_sec = min(1,(int)(1.3*srtt)); /* checké pour trouver val optimal  */
                RetransmissionTimeout.tv_usec = (1.3*srtt - (int)(1.3*srtt))*1000000;
                if (srtt == 0){
                    RetransmissionTimeout.tv_sec = 1; /*initialisation  */
                    RetransmissionTimeout.tv_usec = 0;
                }
                printf("rtt = %f, srtt = %f, le RetransmissionTimeout = %ld.%06lds \n", rtt, srtt, RetransmissionTimeout.tv_sec, RetransmissionTimeout.tv_usec);

                FD_SET(Sous_socket, &rset);
                nready = select(Sous_socket+1, &rset, NULL, NULL, &RetransmissionTimeout);
                // On reste bloqué en attendant la fin du timeout afin de voir si le message pourra être ACK
                if (FD_ISSET(Sous_socket, &rset)) { 
                    printf("ACK avant la fin du timeout \n");
                    ACK_prededent = last_Ack_Recu; //On stocke la valeur du dernier Ack reçu
                    last_Ack_Recu = reception_message(client_message, Sous_socket, client_addr, client_struct_length, &rtt_t1, buffer_last_Ack_Recu, tmp_envoi, ACK_prededent, &compteur_ACK_DUP, &rtt, &srtt);
                    Flight_Size -= 1 ;

                    if (compteur_ACK_DUP == 3 ) { 
                        if (last_Ack_Recu != (num_dernier_seg_reenvoye-1)) //suppose un même pkt pas perdu 2 fois ? 
                        {
                            num_dernier_seg_reenvoye = FastRetransmit (fp, &compteur_ACK_DUP, &SlowStartSeuil, &cwnd_taille, last_Ack_Recu, server_message, lecture, char_num_seq, Sous_socket, client_addr, rtt_t0, tmp_envoi, Flight_Size);
                            fseek(fp, num_seq*(BUFFSIZE-6), SEEK_SET);
                        } else {
                            compteur_ACK_DUP = 0;
                            if (cwnd_taille < SlowStartSeuil)
                            {
                                //slow start
                                cwnd_taille +=1;
                               
                            }  else {
                                //congestion avoidance
                                cwnd_taille = cwnd_taille + 1/cwnd_taille;
                            }
                        }
                    } else {
                        if (cwnd_taille < SlowStartSeuil)
                        {
                            //slow start
                            cwnd_taille +=1;
                        }  else {
                            //congestion avoidance
                            cwnd_taille = cwnd_taille + 1/max(1,cwnd_taille);
                        }
                    }
                } else {
                    // Paquets perdus après un timeout => Retransmission 
                    printf("Fin du timeout= %ld.%06lds, paquet perdu \n", RetransmissionTimeout.tv_sec, RetransmissionTimeout.tv_usec);
                    envoie_message(fp, last_Ack_Recu+1, server_message, lecture, char_num_seq, Sous_socket, client_addr, rtt_t0, tmp_envoi);
                    printf("message réenvoyé n° %ld !\n", last_Ack_Recu+1);
                    SlowStartSeuil = Flight_Size / 2;
                    cwnd_taille = 1;
                    fseek(fp, num_seq*(BUFFSIZE-6), SEEK_SET);
                } 
            }
            fclose(fp);
            sendto(Sous_socket, FIN, strlen(FIN), 0,
                    (struct sockaddr*)&client_addr, client_struct_length);
            close(Sous_socket);
            gettimeofday(&end,0); 
            temps_exec = differencetemps(start, end);
            printf("taille_fichier %ld \n", taille_fichier);
            debit = (taille_fichier/temps_exec)*0.000001;
            printf("Realise en %f s, debit %f Mo/s \n", temps_exec, debit);
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

void remplissage_server_message (char server_message[], char lecture[], char char_num_seq[], int nread) {
    for (int i =0; i < (nread + 6); i++)
    {
        if (i<6)
        {
            server_message[i]=char_num_seq[i];
        } else {
            server_message[i]=lecture[i-6];
        }
    }
}

void envoie_message(FILE *fp, int num_seq, char server_message[], char lecture[], char char_num_seq[], int Sous_socket, struct sockaddr_in client_addr, struct timeval rtt_t0, temps_send tmp_envoi[]) {
    memset(server_message, '\0', BUFFSIZE);
    memset(lecture, '\0', BUFFSIZE-6);
    fseek(fp, (num_seq-1)*(BUFFSIZE-6), SEEK_SET);
    int nread = fread(lecture, 1, BUFFSIZE-6, fp);
    printf("%s \n", server_message);
    Num_Sequence(num_seq, char_num_seq);
    fflush(fp);
    remplissage_server_message(server_message, lecture, char_num_seq, nread);
    sendto(Sous_socket, server_message, nread+6, 0,
        (struct sockaddr*)&client_addr, sizeof(client_addr));
    gettimeofday(&rtt_t0,0);
    tmp_envoi[num_seq].key_num_seq = num_seq;
    tmp_envoi[num_seq].value_temps_envoi = rtt_t0;    
}

long reception_message(char client_message[], int Sous_socket, struct sockaddr_in client_addr, int client_struct_length, struct timeval *rtt_t1, char buffer_last_Ack_Recu[], temps_send tmp_envoi[], long ACK_prededent, int *compteur_ACK_DUP, double *rtt, double *srtt) {
    memset(client_message, '\0', BUFFSIZE);
    if (recvfrom(Sous_socket, client_message, BUFFSIZE, 0,
        (struct sockaddr*)&client_addr, &client_struct_length) < 0){
        printf("Erreur lors de la reception\n");
        return -1;
    }
    gettimeofday(rtt_t1,0);
    printf("%s\n", client_message);
    ACK_num_seq(client_message);
    strcpy(buffer_last_Ack_Recu,client_message);
    long last_Ack_Recu = strtol(buffer_last_Ack_Recu, NULL, 10 ); //atoi
    if (last_Ack_Recu == ACK_prededent) {
        // ACK dupliqué
        *compteur_ACK_DUP += 1;
        printf("compteur_ACK_DUP %d \n", *compteur_ACK_DUP);
    } else {
        *rtt = differencetemps(tmp_envoi[last_Ack_Recu].value_temps_envoi, *rtt_t1);
        *srtt = SRTT(*srtt, *rtt); 
    }
    return last_Ack_Recu;
}

long FastRetransmit (FILE *fp, int *compteur_ACK_DUP, int *SlowStartSeuil, int *cwnd_taille, long last_Ack_Recu, char server_message[], char lecture[], char char_num_seq[], int Sous_socket, struct sockaddr_in client_addr, struct timeval rtt_t0, temps_send tmp_envoi[], int Flight_Size) {
    *compteur_ACK_DUP = 0;
    printf("3 ACK DUPLIQUE..\n");
    *SlowStartSeuil = Flight_Size/2;
    // Paquet perdu => Retransmission 
    envoie_message(fp, last_Ack_Recu+1, server_message, lecture, char_num_seq, Sous_socket, client_addr, rtt_t0, tmp_envoi);
    printf("message réenvoyé n° %ld !\n", last_Ack_Recu+1);
    *cwnd_taille = *SlowStartSeuil + 3; // 3 = nombre ACK dupliqué reçu
    long num_dernier_seg_reenvoye = last_Ack_Recu+1;
    return num_dernier_seg_reenvoye;
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
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    
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
    // printf("SRTT : %f \n", srtt);
    return srtt;
}