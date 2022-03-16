#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include "communication.h"

// valeur d'activité
int active = 1;
// valeur gestion threads
pthread_t * tempThread;
int nbThread = 0;
// mutex contrôle accés variable activité
pthread_mutex_t mutexActive = PTHREAD_MUTEX_INITIALIZER;
// mutex contrôle accés variable Thread
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
// mutex contrôle accés clavier
pthread_mutex_t mutexKeyboard = PTHREAD_MUTEX_INITIALIZER;

////////////////////////////////////////////////////////////////////////////////////////
///                             GESTION PROGRAMME                                    ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction à utliser pour stopper le programme
 *
 * @param value entier
 * @return le programme est en cours d'arrêt
 */
void stopProgramme(int value) {
    printf("\nChat quitté - Appuyer sur entré pour terminer\n");
    pthread_mutex_lock (&mutexActive);
    active = 0;
    pthread_mutex_unlock (&mutexActive);
}

/**
 * @brief fonction qui vérifie le nombre de paramètres
 *
 * @param nbParamters entier qui représente le nombre de paramètres renseigné au lancement du programme
 * @return renvoie une erreur si paramètre(s) pas renseigné
 */
void checkParameters(int nbParamters) {
    if (nbParamters!=3){
        perror("Commande client : ./client <IP_Serveur> <Port>");
        exit(1);
    }
}

/**
 * @brief fonction qui affiche le manuel client
 *
 * @return aucun
 */
void printMan(){
    char buffer[500];
    int nb;
    FILE * fp = fopen("manuel.txt", "r");
    if (fp == NULL) {
       puts("[-]Error in reading file.");
    } else{
        while(!feof(fp)){
            fgets( buffer, sizeof(buffer), fp );
            puts(buffer);
            buffer[0]='\0';
        }
        fclose(fp);
    }
    puts("Vous pouvez écrire des messages");
}

////////////////////////////////////////////////////////////////////////////////////////
///                             GESTION DES THREADS                                  ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui ajoute le thread courant à la liste des threads à fermer
 *
 * @return renvoie une erreur si paramètre(s) pas renseigné
 */
void addTempThread(){
    pthread_mutex_lock (&mutex);
    nbThread+=1;
    tempThread = realloc(tempThread,(nbThread+1)*sizeof(pthread_t));
    tempThread[nbThread] = pthread_self();
    pthread_mutex_unlock (&mutex);
}

/**
 * @brief Fonction qui vérifie si il y a un thread à fermer. Doit être utilisé comme un thread
 *
 * @return renvoie une erreur si paramètre(s) pas renseigné
 */
static void * checkThread(void * p_data) {
    // boucle continue
    while(active){
        pthread_mutex_lock (&mutex);
        if(nbThread>1){
           while(nbThread>1){
               pthread_join(tempThread[nbThread-1], NULL);
               nbThread=-1;
           }
        }
        pthread_mutex_unlock (&mutex);
    }
    pthread_exit(0);
}

////////////////////////////////////////////////////////////////////////////////////////
///                             GESTION DES SOCKETS                                  ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction de création de la socket et de connexion au seveur
 *
 * @param adresse pointeur vers une chaine de caractère qui représente le serveur
 * @param port entier qui représente le port
 * @return entier qui représente la socket
 */
int createSocket(char *adresse, int port) {
    struct sockaddr_in aS;
    socklen_t lgA;
    int dS = socket(PF_INET, SOCK_STREAM, 0);
    if (dS == -1) {
        perror("Erreur sur la création de socket");
        exit(1);
    }
    aS.sin_family = AF_INET;
    inet_pton(AF_INET,adresse,&(aS.sin_addr));
    aS.sin_port = htons(port);
    lgA = sizeof(struct sockaddr_in);
    if (connect(dS, (struct sockaddr *) &aS, lgA) == -1) {
        perror("Erreur sur connection");
        exit(1);
    }
    return dS;
}

/**
 * @brief fonction qui ferme une socket
 *
 * @param dS entier qui représente la socket
 * @return aucun
 */
void shutdownClient(int dS) {
    if (shutdown(dS, 2) == -1) {
        printf("\nLa connection est fermée\n");
    }
}

////////////////////////////////////////////////////////////////////////////////////////
///                         GESTION DES FICHIERS LOCAUX                              ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui récupère la liste des fichiers
 *
 * @return pointeur struct FileAvailable avec la liste des fichiers
 */
struct FileAvailable * getListOfFiles(){
    FILE *fp = malloc(sizeof(FILE));
    char path[50];
    path[0]='\0';
    char ** fichiersDispo= (char**)malloc(sizeof(char*));
    fp = popen("ls | xargs stat --printf \"%A %n \n\" | egrep \"^-r\" | cut -d \" \" -f2","r");
    if (fp == NULL) {
        puts("Failed to run command\n" );
    }
    int nbLignes = 0;
    while (fgets(path, 50, fp) != NULL) {
        fichiersDispo = (char**)realloc(fichiersDispo, (nbLignes+1)*sizeof(char*));
        fichiersDispo[nbLignes]= (char*)malloc((strlen(path))*sizeof(char)+1);
        strcpy(fichiersDispo[nbLignes],path);
        fichiersDispo[nbLignes][strlen(fichiersDispo[nbLignes])-1]='\0';
        nbLignes+=1;
    }
    path[0]='\0';
    pclose(fp);
    struct FileAvailable * files = malloc(sizeof(struct FileAvailable));
    files->length=nbLignes;
    files->files=fichiersDispo;
    return files;
}

/**
 * @brief fonction qui récupère la taille d'un fichier
 *
 * @param nameFile chaine de caractère qui représente un nom de fichier
 * @return taille en chaine de caractère
 */
char * getSizeOfFile(char * nameFile){
    FILE *fp;
    char size[1035];
    char command[strlen(nameFile)+strlen("ls -l ")+strlen(" | cut -d \" \" -f5 ")];
    strcpy(command,"ls -l ");
    strcat(command,nameFile);
    strcat(command," | cut -d \" \" -f5 ");
    fp = popen(command,"r");
    if (fp == NULL) {
        puts("Failed to run command\n" );
    }
    char * result = malloc(2);
    if (fgets(size, sizeof(size), fp) != NULL) {
        size[strlen(size)-1]='\0';
        result=size;
    }
    pclose(fp);
    return result;
}

////////////////////////////////////////////////////////////////////////////////////////
///                   GESTION DES ENVOIS DE FICHIERS AU SERVEUR                      ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui affiche la liste des fichier disponnibles en local
 *
 * @param files pointeur vers une struct FileAvailable
 * @return aucun
 */
void printListOfFilesAvailablesLocal(struct FileAvailable * files){
    puts("\nListe des fichiers disponnibles :\n");
    int i;
    for (i= 0; i < files->length; ++i){
        // construit la ligne
        char line[strlen(files->files[i])+4*sizeof(char)];
        char id[10];
        sprintf(id,"%d",i+1);
        strcpy(line,id);
        strcat(line, " - ");
        strcat(line, files->files[i]);
        // affiche la ligne
        puts(line);
    }
    puts("\nEntrez le numéro du fichier à envoyer - 0 pour quitter :\n");
}

/**
 * @brief fonction qui envoie une demande pour upload un fichier
 *
 * @param files pointeur vers une struct FileAvailable
 * @param idFile entier qui représente l'identifiant du fichier à envoyer
 * @param dS entier qui représente la socket
 * @param nickname pointeur vers une chaine de caractère qui représente le pseudo de l'utilisateur
 * @return aucun
 */
void sendRequestUploadFile(struct FileAvailable * files, int idFile, int dS, char * nickname){
    // récupération de la taille du fichier
    char * size = getSizeOfFile(files->files[idFile-1]);
    // initialisation de la variable de communication
    struct Communication * com = malloc(sizeof(struct Communication));
    com->flag = 3;
    strcpy(com->nicknamePrivate,size);
    strcpy(com->msg,files->files[idFile-1]);
    strcpy(com->nickname,nickname);
    // envoi de la communication
    if (send(dS, com, sizeof(struct Communication), 0) == -1) {
        perror("Error sur envoi");
        pthread_mutex_lock (&mutexActive);
        active=0;
        pthread_mutex_unlock (&mutexActive);
    }
}

/**
 * @brief fonction qui envoi le fichier au serveur. Doit être utilisé avec pthread_create()
 *
 * @param p_data pointeur vers une struct DataUpload
 * @return aucun
 */
static void * sendFile(void * p_data){
    struct DataUpload * dataUpload =(struct DataUpload *)p_data;
    // création de la socket pour upload
    int dSUpload = createSocket(dataUpload->adresse, (dataUpload->port)+1);
    // initialisation variable authentification
    struct ConnexionSecure * authSecure = malloc(sizeof(struct ConnexionSecure));
    strcpy(authSecure->key ,"348;AryW+e6");
    // initialisation variable communication fichier
    struct CommunicationFile * communication = malloc(sizeof(struct CommunicationFile));
    strcpy(communication->size,getSizeOfFile(dataUpload->content));
    strcpy(communication->content,dataUpload->content);
    // si le fichier à une taille de moins de 10 Mo
    if (atoi(communication->size)<10000000 ){
        // send authentification
        if (send(dSUpload, authSecure, sizeof(struct ConnexionSecure), 0) == -1) {
            puts("[-]Error envoie authentification");
        }
        // send caractéristiques fichier
        if (send(dSUpload, communication, sizeof(struct CommunicationFile), 0) == -1) {
            puts("[-]Error envoie données fichier ");
        }
        // variables caractéristiques fichier
        int endSize = atoi(getSizeOfFile(dataUpload->content));
        int currentSize = 0;
        int sizeBuffer = 1024;
        char buffer[1024];
        // ouverture du fichier à envoyer
        FILE * fp = fopen(dataUpload->content, "r");
        if (fp == NULL) {
           puts("[-]Error in reading file.");
        } else{
            puts("Envoi en cours");
            int nb = fread(buffer,1,sizeof(buffer),fp);
            while(!feof(fp)){
                //strcpy(authCom->content,buffer);
                if (send(dSUpload, buffer, nb, 0) == -1) {
                    puts("[-]Error in sending file.");
                }
                bzero(buffer,sizeof(buffer));
                nb = fread(buffer,1,sizeof(buffer),fp);
            }
            if (send(dSUpload, buffer, nb, 0) == -1) {
                puts("[-]Error in sending file.");
            }
            // fermeture du fichier
            fclose(fp);
            puts("Envoie terminé.");
        }
    } else {
        puts("La taille du fichier est trop grande - Retour message");
    }
    free(communication);
    shutdownClient(dSUpload);
    addTempThread();
    pthread_exit(0);
}

/**
 * @brief fonction qui gère l'envoi de fichier au serveur. Doit être utilisé avec pthread_create()
 *
 * @param dS entier qui représente la socket
 * @param adresse pointeur vers une chaine de caractère qui représente l'adresse du serveur avec lequel l'utilisateur communique
 * @param port entier qui représente le port classique de communication
 * @param nickname pointeur vers une chaine de caractère qui représente le pseudo de l'utilisateur
 * @return aucun
 */
void sendFileToServer(int dS, char * adresse, int port, char * nickname){
    // récupération et affichage de la liste des fichiers
    struct FileAvailable * files =getListOfFiles();
    printListOfFilesAvailablesLocal(files);
    // récupération de l'identifiant du fichier choisi
    char number[10];
    fgets (number, sizeof number, stdin);
    int idFile = atoi(number);
    // si l'identifiant est correct
    if(idFile>0&&idFile<=files->length){
        if(strlen(files->files[idFile-1])<30){
            // send demande upload
            sendRequestUploadFile(files,idFile,dS,nickname);
            // initialisation variable avec les données pour upload
            struct DataUpload * dataUploadSend = malloc(sizeof(struct DataUpload));
            dataUploadSend->port=port;
            strcpy(dataUploadSend->adresse,adresse);
            strcpy(dataUploadSend->content,files->files[idFile-1]);
            // création du thread qui envoie le fichier
            pthread_t thread;
            if ( pthread_create( &thread,NULL, sendFile, (void *)dataUploadSend) ) {
                printf("Impossible de créer le thread\n" );
            }
        } else {
            free(files);
            puts("Annulation - le nom du fichier est > à 30 caractères\n");
        }
    } else if(idFile==0) {
        free(files);
        puts("Annulation - Aucun fichier envoyé\n");
    } else {
        free(files);
        puts("Le fichier n'existe pas\n");
    }
    puts("Retour aux messages");
}

////////////////////////////////////////////////////////////////////////////////////////
///                 GESTION DES RECEPTIONS DE FICHIERS DU SERVEUR                    ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui envoie la demande de pouvoir télécharger un fichier
 *
 * @param communication pointeur vers un struct Communication qui représente la communication classique avec le serveur
 * @param dS entier qui représente la socket
 * @return aucun
 */
void sendRequestDownloadFile(struct Communication * communication, int dS){
    communication->flag = 4;
    strcpy(communication->msg,"Demande download fichier");
    if (send(dS, communication, sizeof(struct Communication), 0) == -1) {
        puts("Error sur envoi\n");
        pthread_mutex_lock (&mutexActive);
        active=0;
        pthread_mutex_unlock (&mutexActive);
    }
}

/**
 * @brief fonction qui affiche la liste des fichiers disponnibles sur le serveur
 *
 * @param communicationFile pointeur vers un struct CommunicationFile qui représente la communication fichier avec le serveur
 * @param dSDownload entier qui représente la socket
 * @return aucun
 */
void printListOfFilesAvailablesOnTheServer(struct CommunicationFile * communicationFile, int dSDownload){
    puts("Liste des fichiers disponibles sur le serveur :");
    int id = 1;
    do {
        if (recv(dSDownload, communicationFile, sizeof(struct CommunicationFile), 0) == -1) {
            puts("[-]Error in sending file.");
        }
        if(strlen(communicationFile->content)>0){
            char idStr [5];
            sprintf(idStr,"%d",id);
            char line[strlen(idStr)+3+strlen(communicationFile->content)];
            id+=1;
            strcpy(line,idStr);
            strcat(line," - ");
            strcat(line,communicationFile->content);
            puts(line);
        }

    } while (strlen(communicationFile->content)>0);
    if(id==1){
        puts("Aucun fichier disponnibles");
    }
}

/**
 * @brief fonction qui gère le téléchargement d'un fichier du serveur. Doit être utilisé avec pthread_create()
 *
 * @param p_data pointeur vers un entier qui représente la socket
 * @return aucun
 */
static void * recvFile(void * p_data){
    struct CommunicationFile * authCom = malloc(sizeof(struct CommunicationFile));
    int dSDownload = *((int *) p_data);
    // réception des caractéristiques du fichier
    if (recv(dSDownload, authCom, sizeof(struct CommunicationFile), 0) == -1) {
        puts("[-]Error pour la reception des caractéristiques du fichier");
    } else {
        if(strcmp(authCom->size,"Erreur")==0){
            puts("Erreur saisie\n");
        } else {
            // initialisation des variables représentant les caractéristiques du fichier
            char nameFile[strlen(authCom->content)] ;
            char size[strlen(authCom->size)];
            strcpy(nameFile,authCom->content);
            strcpy(size,authCom->size);
            int endSize = atoi(size);
            int currentSize = 0;
            int sizeBuffer = 1024;
            char buffer[sizeBuffer];
            // ouverture en mode écriture du fichier que l'on reçoit
            FILE * fp = fopen(nameFile, "w+");
            if (fp == NULL) {
               puts("[-]Error in reading file.");
            }
            // réception des données
            puts("\nRéception en cours ...\n");
            int val = 0;
            // si on verifie pas currentSize on peut faire croire qu'on va envoyer 1ko sauf qu'on peut ajouter 4To de données entre temps et saturé l'espace du serveur
            while(currentSize<endSize-1024){
                val = recv(dSDownload, buffer , 1024, 0);
                fwrite( buffer, 1, val, fp );
                currentSize+=strlen(buffer);
            }
            val = recv(dSDownload, buffer , 1024, 0);
            fwrite( buffer, 1, val, fp );
            // fermeture du fichier
            fclose(fp);
            puts("Réception terminé !\n");
        }
    }
    shutdownClient(dSDownload);
    free(authCom);
    addTempThread();
    pthread_exit(0);
}

/**
 * @brief fonction qui gère le choix et le téléchargement d'un fichier du serveur
 *
 * @param communication pointeur vers un struct CommunicationChannel qui représente la communication channel avec le serveur
 * @param dS entier qui représente la socket
 * @param adresse pointeur vers une chaine de caractère qui représente l'adresse du serveur avec lequel l'utilisateur communique
 * @param port entier qui représente le port classique de communication
 * @return aucun
 */
void recvFileFromServer(struct Communication * communication, int dS,char * adresse, int port){
    // envoie de la demmande de recevoir un fichier
    sendRequestDownloadFile(communication,dS);
    // création d'une socket sur un nouveau port spécial
    int dSDownload = createSocket(adresse, port+2);
    // authentification sécurisé sur le nouveau port
    struct ConnexionSecure * auth = malloc(sizeof(struct ConnexionSecure));
    strcpy(auth->key ,"348;AryW+e6");
    if (send(dSDownload, auth, sizeof(struct ConnexionSecure), 0) == -1) {
        puts("[-]Error in sending file.");
    }
    // initialisation de la variable spéciale pour communiquer à propos des channels
    struct CommunicationFile * communicationFile = malloc(sizeof(struct CommunicationFile));
    // réception de la liste des fichiers disponnibles
    printListOfFilesAvailablesOnTheServer(communicationFile,dSDownload);
    // recupération du fichier choisi par l'utilisateur
    char msg[10];
    puts("Entrez le numéro du fichier que vous souhaitez télécharger  - 0 pour quitter:");
    fgets (msg, sizeof msg, stdin);
    communicationFile->content[0]='\0';
    strcpy(communicationFile->content,msg);
    if (send(dSDownload, communicationFile, sizeof(struct CommunicationFile), 0) == -1) {
        puts("[-]Error envoie identifiant fichier souhaité.");
    }
    // si l'utilisateur à tappé un entier
    if(atoi(msg)!=0){
        pthread_t threadDownload;
        int *arg = malloc(sizeof(int));
        *arg = dSDownload;
        // on crée le thread pour recevoir un fichier
        if ( pthread_create( &threadDownload,NULL,recvFile , arg) ) {
            printf("Impossible de créer le thread\n" );
        }
        puts("Retour message");
    } else {
        puts("Erreur saisie / Demmande de fin - Retour message\n");
        shutdownClient(dSDownload);
    }
    free(communicationFile);
}

////////////////////////////////////////////////////////////////////////////////////////
///                              GESTION DES CHANNELS                                ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui prépare la communication à envoyer pour mettre à jour les channels
 *
 * @param dSChannel entier qui représente la socket
 * @return entier qui représente le nombre de channel courrant
 */
int printListOfChannelsAvailables(int dSChannel){
    int nbCurrentChannels = 0;
    char channel[250];
    channel[0]='\0';
    puts("Liste des channels disponibles : ");
    while (active){
        channel[0]='\0';
        if(recv(dSChannel, channel, sizeof(channel), 0) <= 0){
            pthread_mutex_lock (&mutexActive);
            active=0;
            pthread_mutex_unlock (&mutexActive);
            break;
        }
        if(strcmp(channel,"FIN")!=0){
            puts(channel);
            nbCurrentChannels+=1;
        } else {
            break;
        }
    }
    puts("Entrez la commande désirée : ");
    puts("/join <id of the server>");
    puts("/create <name> <description> <users max>");
    puts("/update <id of the server> <name> <description> <users max>");
    puts("/delete <id of the server>");
    puts("/quit (Pour quitter la gestion ds channels)");
    return nbCurrentChannels;
}

/**
 * @brief fonction qui prépare la communication à envoyer joindre un channel
 *
 * @param communication pointeur vers un struct CommunicationChannel qui représente la communication channel avec le serveur
 * @param nbCurrentChannels entier qui représente le nombre de channels courrant
 * @return aucun
 */
void prepareJoinChannel(struct CommunicationChannel * communication, int nbCurrentChannels){
    puts("<> Demande de rejoindre un channel");
    char * tmpMessagePart2 = strtok ( NULL, " " ) ;
    if (tmpMessagePart2==NULL){
        puts("L'identifiant n'est pas renseigné");
    } else if(strlen(tmpMessagePart2)>0&&atoi(tmpMessagePart2)!=0){
        if(atoi(tmpMessagePart2)<=nbCurrentChannels){
            puts("Join channel");
            communication->idChannel=atoi(tmpMessagePart2);
            communication->flag=4;
        } else {
            puts("Identifiant incorrecte");
        }
    } else {
        puts("L'identifiant du channel est invalide");
    }
}

/**
 * @brief fonction qui prépare la communication à envoyer pour créer un channel
 *
 * @param communication pointeur vers un struct CommunicationChannel qui représente la communication channel avec le serveur
 * @return aucun
 */
void prepareCreateChannel(struct CommunicationChannel * communication){
    puts("<> Demande de création de channel");

    char * tmpMessagePart2 = strtok ( NULL, " " ) ;
    if (tmpMessagePart2==NULL){
        puts("Le nom du channel n'est pas renseigné");
    } else if(strlen(tmpMessagePart2)>0){
        strcpy(communication->name,tmpMessagePart2);
        char * tmpMessagePart3 = strtok ( NULL, " " ) ;
        if (tmpMessagePart3==NULL){
            puts("La description n'est pas renseigné");
        } else if(strlen(tmpMessagePart3)>0){
            strcpy(communication->description,tmpMessagePart3);
            char * tmpMessagePart4 = strtok ( NULL, " " ) ;
            if (tmpMessagePart4==NULL){
                puts("Le nombre de client n'est pas renseigné");
            } else if(strlen(tmpMessagePart4)>0&&(atoi(tmpMessagePart4)!=0)){
                communication->nbClientMax=atoi(tmpMessagePart4);
                puts("Création du channel");
                communication->flag=1;
            } else {
                puts("Le nombre de clients est invalide");
            }
        } else {
            puts("La description est invalide");
        }
    } else {
        puts("Le nom du channel est invalide");
    }
}

/**
 * @brief fonction qui prépare la communication à envoyer pour mettre à jour les channels
 *
 * @param communication pointeur vers un struct CommunicationChannel qui représente la communication channel avec le serveur
 * @param nbCurrentChannels entier qui représente le nombre de channels courrant
 * @return aucun
 */
void prepareUpdateChannel(struct CommunicationChannel * communication, int nbCurrentChannels){
    puts("<> Demande de modification de channel");
    char * tmpMessagePart2 = strtok ( NULL, " " ) ;
    if (tmpMessagePart2==NULL){
        puts("L'identifiant n'est pas renseigné");
    } else if(strlen(tmpMessagePart2)>0&&atoi(tmpMessagePart2)!=0){
        if(atoi(tmpMessagePart2)<=nbCurrentChannels&&atoi(tmpMessagePart2)>0){
            communication->idChannel=atoi(tmpMessagePart2);
            char * tmpMessagePart3 = strtok ( NULL, " " ) ;
            if (tmpMessagePart3==NULL){
                puts("Le nom du channel n'est pas renseigné");
            } else if(strlen(tmpMessagePart3)>0){
                strcpy(communication->name,tmpMessagePart3);
                char * tmpMessagePart4 = strtok ( NULL, " " ) ;
                if (tmpMessagePart4==NULL){
                    puts("La description n'est pas renseigné");
                } else if(strlen(tmpMessagePart4)>0){
                    strcpy(communication->description,tmpMessagePart4);
                    char * tmpMessagePart5 = strtok ( NULL, " " ) ;
                    if (tmpMessagePart5==NULL){
                        puts("Le nombre de client n'est pas renseigné");
                    } else if(strlen(tmpMessagePart5)>0&&(atoi(tmpMessagePart5)!=0)){
                        communication->nbClientMax=atoi(tmpMessagePart5);
                        puts("Modification du channel");
                        communication->flag=3;
                    } else {
                        puts("Le nombre de clients est invalide");
                    }
                } else {
                    puts("La description est invalide");
                }
            } else {
                puts("Le nom du channel est invalide");
            }
        } else {
            puts("Identifiant incorrecte");
        }
    } else {
        puts("Le nom du channel est invalide");
    }
}

/**
 * @brief fonction qui prépare la communication à envoyer pour supprimer les channels
 *
 * @param communication pointeur vers un struct CommunicationChannel qui représente la communication channel avec le serveur
 * @param nbCurrentChannels entier qui représente le nombre de channels courrant
 * @return aucun
 */
void prepareDeleteChannel(struct CommunicationChannel * communication, int nbCurrentChannels){
    puts("<> Demande de supression de channel");
    char * tmpMessagePart2 = strtok ( NULL, " " ) ;
    if (tmpMessagePart2==NULL){
        puts("L'identifiant n'est pas renseigné");
    } else if(strlen(tmpMessagePart2)>0&&atoi(tmpMessagePart2)!=0){
        if(atoi(tmpMessagePart2)<=nbCurrentChannels){
            puts("Suppression de channel");
            communication->idChannel=atoi(tmpMessagePart2);
            communication->flag=2;
        } else {
            puts("Identifiant incorrecte");
        }
    } else {
        puts("Le nom du channel est invalide");
    }
}

/**
 * @brief fonction qui permet de gérer et envoyer les demandes de channels
 *
 * @param communication pointeur vers un struct Communication qui représente la communication classique avec le serveur
 * @param dS entier qui représente le déscripteur de socket pour les communications classique
 * @return aucun
 */
void  manageChannels( struct Communication * com, int dSChannel) {
    char command[250];
    while(active&&strcmp(command,"/quit")!=0){
        // affichage des channels disponnibles
        int nbCurrentChannels = printListOfChannelsAvailables(dSChannel);
        // input : récuperation commandes clients
        fgets(command, sizeof(command), stdin);
        // initialisation de la communication spéciale Channel
        struct CommunicationChannel * communication = malloc(sizeof(struct CommunicationChannel));
        if(strlen(command)>1){
            command[strlen(command)-1]='\0';
            // récupération du type de commande
            char * tmpMessage = malloc(sizeof(command)) ;
            strcpy(tmpMessage,command);
            char *commandType = strtok ( tmpMessage, " " );
            communication->flag=6;
            // si c'est une demmande de joindre un channel
            if (strcmp(commandType, "/join")==0) {
                prepareJoinChannel(communication, nbCurrentChannels);
            // si c'est une demmande de créer un channel
            } else if (strcmp(commandType, "/create")==0) {
                prepareCreateChannel(communication);
            // si c'est une demmande de mettre à jour un channel
            } else if (strcmp(commandType, "/update")==0) {
                prepareUpdateChannel(communication, nbCurrentChannels);
            // si c'est une demmande de supprimer un channel
            } else if (strcmp(commandType, "/delete")==0) {
                prepareDeleteChannel(communication, nbCurrentChannels);
            // si c'est une demmande de quitter la gestion des channels
            } else if (strcmp(commandType, "/quit")==0) {
                puts("<> Vous quitez la gestion de channels");
                communication->flag=0;
            } else {
                puts("<> Commande incomprise");
                communication->flag=6;
            }
        }else {
            communication->flag=6;
        }
        // send : commande vers serveur
        if (send(dSChannel, communication, sizeof(struct CommunicationChannel), 0) == -1) {
            puts("[-]Error in sending file.");
        }
        // recv : résultat commande
        if(communication->flag!=0){
            if(recv(dSChannel, communication, sizeof(struct CommunicationChannel), 0) == -1) {
                perror("Error sur envoi");
            }
            switch(communication->flag)
            {
                case 5: // si la requête est validé
                    puts("<> Validé :");
                    break;
                case 6 : // si la requête est refusé
                    puts("<> Erreur :");
                    break;
            }
            // information serveur
            puts(communication->description);
        }
        free(communication);

    }
    command[0]='\0';
    // on libère le clavier
    pthread_mutex_unlock (&mutexKeyboard);
    puts("<> Vous pouvez recommencer à envoyer des messages");
}

////////////////////////////////////////////////////////////////////////////////////////
///                          GESTION DES ENTREES CLAVIER                             ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui récupère de manière sécurisé ce qui est tappé au clavier par l'utilisateur
 *
 * @param msg pointeur vers une chaine de caractères où sera stocké ce qui est tappé par l'utilisateur
 * @return aucun
 */
void getInputKeyboard(char * msg){
    pthread_mutex_lock (&mutexKeyboard);
    fgets (msg, 50, stdin);
    pthread_mutex_unlock (&mutexKeyboard);
}

/**
 * @brief fonction qui prépare la communication à envoyer en demandant de pouvoir gérer les channels
 *
 * @param communication pointeur vers un struct Communication qui représente la communication classique avec le serveur
 * @return aucun
 */
void prepareRequestManageChannel(struct Communication * communication){
    communication->flag = 5;
    pthread_mutex_lock (&mutexKeyboard);
    strcpy(communication->msg,"....");
}

/**
 * @brief fonction qui prépare la communication à envoyer en y intégrant le message privé de l'utilisateur  et le pseudo du destinataire
 *
 * @param communication pointeur vers un struct Communication qui représente la communication classique avec le serveur
 * @param msg pointeur vers une chaine de caractère qui représente le message de l'utilisateur
 * @param msgAfter pointeur vers une chaine de caractère qui représente le message de l'utilisateur qui suit
 * @param nicknamePrivate pointeur vers une chaine de caractère qui représente le pseudo du destinataire
 * @return aucun
 */
void preparePrivateMessage(struct Communication * communication, char * msg, char * msgAfter, char * nicknamePrivate){
    communication->flag = 2;
    strcpy(communication->msg,msg+strlen(msgAfter)+1+strlen(nicknamePrivate));
    strcpy(communication->nicknamePrivate,nicknamePrivate+1);
}

/**
 * @brief fonction qui prépare la communication à envoyer en y intégrant le message de l'utilisateur
 *
 * @param communication pointeur vers un struct Communication qui représente la communication classique avec le serveur
 * @param msg pointeur vers une chaine de caractère qui représente le message de l'utilisateur
 * @return aucun
 */
void prepareMessage(struct Communication * communication, char * msg){
    communication->flag = 1;
    strcpy(communication->msg,msg);
}

/**
 * @brief fonction qui traite les entrés du clavier de l'utilisateur
 *
 * @param nickname pointeur vers une chaine de caractère qui représente le pseudo de l'utilisateur
 * @param dS entier qui représente le déscripteur de socket pour les communications classique
 * @param adresse pointeur vers une chaine de caractère qui représente l'adresse du serveur avec lequel l'utilisateur communique
 * @param port entier qui représente le port classique de communication
 * @return aucun
 */
static void manageInput(char * nickname,int dS, char * adresse, int port) {
    char msg[50];
    // initialisation communication classique avec le serveur
    struct Communication *communication = malloc(sizeof(struct Communication));
    communication->flag = 1;
    communication->idChannel = 0;
    strcpy(communication->nickname,nickname);
    // boucle tant que le chat est active (tant qu'il n'y a pas le mot "fin")
    while(active && strcmp( msg, "fin" ) != 0) {
        // récupération de ce qui est tappé
        getInputKeyboard(msg);
        if (strlen(msg) > 1) {
            msg[strlen(msg)-1]='\0';
            // on découpe ce qui a été tappé en fonction des espaces
            char * tmpMessage = malloc(sizeof(msg)) ;
            strcpy(tmpMessage,msg);
            char *tmpMessagePart1 = strtok ( tmpMessage, " " );
            char * tmpMessagePart2 = strtok ( NULL, " " ) ;
            // si c'est une demande d'upload de fichier
            if(msg[0]==47 && tmpMessagePart1!=NULL && strlen(tmpMessagePart1)>1 && strcmp(tmpMessagePart1,"/files")==0 && tmpMessagePart2==NULL){
                sendFileToServer(dS,adresse,port, nickname);
            // si on demande d'afficher le manuel
            } else if(msg[0]==47 && tmpMessagePart1!=NULL && strlen(tmpMessagePart1)>1 && strcmp(tmpMessagePart1,"/man")==0 && tmpMessagePart2==NULL){
                printMan();
            // c'est une demande des fichiers du serveur
            } else if(msg[0]==47 && tmpMessagePart1!=NULL && strlen(tmpMessagePart1)>1 && strcmp(tmpMessagePart1,"/serverFiles")==0 && tmpMessagePart2==NULL){
                recvFileFromServer(communication,dS,adresse,port);
            } else {
                // c'est une demande de gestion de channels
                if(msg[0]==47 && tmpMessagePart1!=NULL && strlen(tmpMessagePart1)>1 && strcmp(tmpMessagePart1,"/channel")==0 && tmpMessagePart2==NULL){
                    prepareRequestManageChannel(communication);
                // c'est un message privé
                } else if(msg[0]==47 && strlen(tmpMessagePart1)>1 && strcmp(tmpMessagePart1,"/mp")==0 && msg[4]==64 && tmpMessagePart2!=NULL){
                    preparePrivateMessage(communication,msg,tmpMessagePart1,tmpMessagePart2);
                // c'est un message à diffuser
                } else {
                    prepareMessage(communication,msg);
                }
                // envoi de la Communication préparé
                if (send(dS, communication, sizeof(struct Communication), 0) == -1) {
                    puts("Error sur envoi\n");
                    pthread_mutex_lock (&mutexActive);
                    active=0;
                    pthread_mutex_unlock (&mutexActive);
                }
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////
///                      GESTION DES RECEPTIONS DE MESSAGES                          ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui effectue un traitement du message reçu et l'affiche
 *
 * @param communication pointeur vers un struct Communication qui représente la communication classique avec le serveur
 * @return aucun
 */
void processRecvMessage(struct  Communication * communication){
    char * display = malloc(sizeof(char)*(strlen(communication->msg)+7+strlen(communication->nickname)+strlen("Channel ")));
    strcpy(display,"Channel ");
    char idChannel[3];
    sprintf(idChannel, "%d", communication->idChannel+1);
    strcat(display,idChannel);
    strcat(display," - ");
    strcat(display,communication->nickname);
    strcat(display," > ");
    strcat(display,communication->msg);
    puts(display) ;
}

/**
 * @brief fonction qui effectue un traitement du message reçu privé et l'affiche
 *
 * @param communication pointeur vers un struct Communication qui représente la communication classique avec le serveur
 * @return aucun
 */
void processRecvPrivateMessage(struct  Communication * communication){
    char * display = malloc(sizeof(char)*(strlen(communication->msg)+12+strlen(communication->nickname)));
    strcpy(display,communication->nickname);
    strcat(display," -- PRIVE > ");
    strcat(display,communication->msg);
    puts(display) ;
}

/**
 * @brief fonction de reception du message si la connexion est établie. Doit être utilisé avec pthread_create()
 *
 * @param p_data pointeur vers un entier qui représente le descripteur de la socket
 * @return aucun
 */
static void * recvMessage(void * p_data) {
    int * dS = (int *) p_data;
    struct Communication *communication = malloc(sizeof(struct Communication));
    // boucle tant que le chat est active (tant qu'il n'y a pas le mot "fin")
    while(active) {
        // Si on ne reçoit rien
        if(recv(*dS,  communication, sizeof(struct Communication), 0) <= 0 ){
            pthread_mutex_lock (&mutexActive);
            active=0;
            pthread_mutex_unlock (&mutexActive);
        // Si on reçoit une commanication
        } else {
            switch(communication->flag)
            {
                case 1: // si je suis un message diffusé
                    processRecvMessage(communication);
                    break;
                case 2 : // si je suis un message privé
                    processRecvPrivateMessage(communication);
                    break;
                case 6 : // si on m'autorise à gérer les channels
                    manageChannels(communication,*dS);
                    break;
            }
        }
    }
    pthread_exit(0);
}

////////////////////////////////////////////////////////////////////////////////////////
///                                     MAIN                                         ///
////////////////////////////////////////////////////////////////////////////////////////

void main(int argc, char *argv[]) {
    // vérification des paramètres
    checkParameters(argc);
    // mise en place de la capture du signal pour stopper le programme
    signal(SIGINT, stopProgramme);
    // création de la socket et création au serveur
    int dS = createSocket(argv[1], atoi(argv[2]));
    // affichage commandes
    printf("Chat actif\n<> CTRL+C ou envoyez fin pour quitter\n<> Entrez /man pour les commandes du serveur \n<> Vos messages ne seront pas transmis tant que votre connexion ne sera pas accepté\n");
    printf("----\n");
    // demande d'authentification
    strcpy(AUTHENTIFICATION_KEY ,"3j9z,9?5EF,;fP9E-48;AryW+e6dzM@");
    struct ConnexionSecure *connexion = malloc(sizeof(struct ConnexionSecure));
    // demande nickname
    char nickname[12];
    nickname[0]='\0';
    while(strlen(nickname)==0){
        printf( "Veuillez saisir votre nom non vide: " );
        fgets (nickname, sizeof (nickname), stdin);
        nickname[strlen(nickname)-1]='\0';
    }
    strcpy(connexion->nickname,nickname);
    strcpy(connexion->key,AUTHENTIFICATION_KEY);
    // envoie du nickname
    if (send(dS, connexion, sizeof (struct ConnexionSecure) , 0) == -1) {
        perror("Error sur envoi");
        exit(1);
    }
    puts("En attente ... ");
    // message retour serveur
    char messageReturn[60];
    int recvIntTest = recv(dS, messageReturn , 60, 0) ;
    printf("%s\n",messageReturn);
    // lancement du thread qui gère les fin des threads du programme
    tempThread = malloc(sizeof(pthread_t));
    pthread_t threadCheck;
    if ( pthread_create( &threadCheck,NULL, checkThread, (void *)NULL )) {
        printf("Impossible de créer le thread\n" );
    }
    // lancement du thread qui s'occupe de gérer et traiter la réception des messages
    pthread_t thread;
    if ( pthread_create( &thread,NULL, recvMessage,(void *)&dS) ) {
        puts("Impossible de créer le thread\n" );
    }
    // gestion des entrés du clavier
    manageInput(nickname,dS,argv[1], atoi(argv[2]));
    // fermeture de la socket client
    shutdownClient(dS);
    puts("Fin de la conversation\n") ;
    // attente de la fin du thread de réception
    pthread_join(thread, NULL);
    // attente de la fin du thread de vérification
    pthread_join(threadCheck,NULL);
}