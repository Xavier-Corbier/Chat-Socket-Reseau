#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include "communication.h"
#include "structClient.h"

// valeur de contrôle d'activité
int active = 1;
// valeurs de socket Upload
int dSUpload= 0;
socklen_t * lgUpload;
// valeurs de socket Download
int dSDownload= 0;
socklen_t * lgDownload;
// valeurs gestion des clients
// ! pas besoin de mutex
int nbMaxChannels ;
// ! besoin d'un mutex
int nbCurrentChannels = 1;
struct Channel * channels;
pthread_t * tempThread;
int nbThread = 0;
// mutex et sémaphore
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexActive = PTHREAD_MUTEX_INITIALIZER;
sem_t semaphore;

////////////////////////////////////////////////////////////////////////////////////////
///                             GESTION PROGRAMME                                    ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui retourne la date courante JJMMYYHHMMSS_
 *
 * @return chaine de caractères qui représente la date
 */
char * getDateCurrent(){
  int h, min, s, day, mois, an;
  time_t now;
  char * date = malloc(50*sizeof(char));
  // Renvoie l'heure actuelle
  time(&now);
  struct tm *local = localtime(&now);
  h = local->tm_hour;
  min = local->tm_min;
  s = local->tm_sec;
  day = local->tm_mday;
  mois = local->tm_mon + 1;
  an = local->tm_year - 100;
  sprintf(date,"%02d%02d%d%02d%02d%02d_", day, mois, an, h, min, s);
  return date;
}

/**
 * @brief fonction à utliser pour stopper le programme
 *
 * @param value entier
 * @return le programme est en cours d'arrêt
 */
void stopProgramme(int value) {
    printf("\nServeur en cours d'arrêt - Un client doit se connecter pour stopper le serveur \n");
    pthread_mutex_lock (&mutexActive);
    active = 0;
    pthread_mutex_unlock (&mutexActive);
}

/**
 * @brief fonction qui vérifie le nombre de paramètres
 *
 * @param nbParamters entier qui représente le nombre de paramètres renseigné au lancement du programme
 * @param argv tableau de chaine de caractères qui représente les arguments au lancement du programme
 * @return aucun
 */
void checkParameters(int nbParameters, char *argv[]){
    if (nbParameters!=4){
        puts("Erreur : <port> <nbClientMax> <nbChannelMax> ");
        exit(1);
    } else if (atoi(argv[2])<=0){
        puts("Erreur : <port> <nbClientMax> <nbChannelMax>  -> <nbClientMax> invalide ");
        exit(1);
    } else if (atoi(argv[3])<=0){
        puts("Erreur : <port> <nbClientMax> <nbChannelMax>  -> <nbChannelMax>  invalide ");
        exit(1);
    }
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
 * @brief fonction de création de la socket
 *
 * @param port entier qui représente le port
 * @param lg pointeur vers un socklen_t
 * @return entier qui représente la socket
 */
int createSocket(int port, socklen_t* lg){
    struct sockaddr_in ad;
    int dS = socket(PF_INET, SOCK_STREAM, 0);
    if(dS==-1){
        perror("Erreur socket");
        exit(1);
    }
    ad.sin_family = AF_INET;
    ad.sin_addr.s_addr = INADDR_ANY ;
    ad.sin_port = htons(port) ;
    if(bind(dS, (struct sockaddr*)&ad, sizeof(ad))==-1){
        perror("Erreur bind");
        exit(1);
    }
    if(listen(dS, 7)==-1){
        perror("Erreur listen");
        exit(1);
    }
    *lg = sizeof(struct sockaddr_in) ;
    return dS;
}

/**
 * @brief fonction qui accepte un nouveau client
 *
 * @param dS entier qui représente la socket
 * @param lg pointeur vers un socklen_t
 * @return entier qui représente le client
 */
int acceptClient(int dS, struct sockaddr_in* aC, socklen_t* lg){
    int client= accept(dS, (struct sockaddr*) aC,lg) ;
    if(client==-1){
        printf("\nClient  : Erreur accept\n");
    } else {
        printf("\nClient  : connexion reçu\n");
    }
    return client;
}

/**
 * @brief fonction qui ferme une socket
 *
 * @param dS entier qui représente la socket
 * @return aucun
 */
void shutdownSocket(int fd){
    if(shutdown(fd,2)==-1){
        printf("\nLa connection est fermée\n");
    }
}

////////////////////////////////////////////////////////////////////////////////////////
///                           GESTION DES DATAS CLIENTS                              ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui récupère l'identifiant du client à partir du pseudo
 *
 * @param nicknamePrivate un chaine de charactère qui représente le pseudo du client
 * @return un pointeur vers une struct LocationClient avec les identifiants à -1 si le client n'existe pas sinon avec sa position
 */
struct LocationClient * getIdClientFromnickname(char * nicknamePrivate){
    struct LocationClient * location = malloc(sizeof(struct LocationClient));
    location->idClient=-1;
    int j = 0;
    while(j<nbMaxChannels){
        // si le channel existe
        if(strlen(channels[j].name)!=0){
            for (int i = 0 ; i < channels[j].nbClientCurrent-1 ; i++) {
                pthread_mutex_lock (&mutex);
                if(strcmp(nicknamePrivate,channels[j].dataclient[i].nickname)==0){
                    location->idClient=i;
                    location->idChannel=j;
                }
                pthread_mutex_unlock (&mutex);
            }
        }
        j+=1;
    }
    return location;
}

/**
 * @brief fonction qui supprime et arrange les données client
 *
 * @param location un pointeur vers une struct LocationClient avec les identifiants à -1 si le client n'existe pas sinon avec sa position
 * @return aucun
 */
void arangeArrayClient(struct LocationClient * location){
    // si il y a au moins un client
    if (channels[location->idChannel].nbClientCurrent>1){
        pthread_mutex_lock (&mutex);
        channels[location->idChannel].dataclient[location->idClient].nickname[0]='\0';
        // pour chaque client du même channel à partir du client à supprimer
        for (int i = location->idClient ; i < channels[location->idChannel].nbClientCurrent-2 ; i++) {
            channels[location->idChannel].dataclient[i].client=channels[location->idChannel].dataclient[i+1].client;
            channels[location->idChannel].dataclient[i].thread=channels[location->idChannel].dataclient[i+1].thread;
            strcpy(channels[location->idChannel].dataclient[i].nickname,channels[location->idChannel].dataclient[i+1].nickname);
        }
        channels[location->idChannel].dataclient[channels[location->idChannel].nbClientCurrent-1].nickname[0]='\0';
        pthread_mutex_unlock (&mutex);
    }
}

/**
 * @brief fonction qui déconnecte le client courrant
 *
 * @param nickname un chaine de charactère qui représente le pseudo du client
 * @return aucun
 */
void stopClient(char * nickname){
    puts("Client déconecté\n");
    struct LocationClient * location = malloc(sizeof(struct LocationClient));
    location = getIdClientFromnickname(nickname);
    // si le client existe
    if (location->idClient!=-1){
        pthread_mutex_lock (&mutex);
        shutdownSocket(channels[location->idChannel].dataclient[location->idClient].client);
        pthread_mutex_unlock (&mutex);
        arangeArrayClient(location);
        addTempThread();
        pthread_mutex_lock (&mutex);
        channels[location->idChannel].nbClientCurrent-=1;
        pthread_mutex_unlock (&mutex);
        // On relache le sémaphore
        sem_post(&semaphore);
    }
}

////////////////////////////////////////////////////////////////////////////////////////
///                 GESTION DES RECEPTIONS DE FICHIERS DU CLIENT                     ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui récupère la connection d'un client sur le port d'upload
 *
 * @param authSecure pointeur vers un struct ConnexionSecure qui représente la communication sécurisé avec le serveur
 * @param clientActive pointeur vers un entier qui représente l'activité du client courrant
 * @return entier qui représente le client connecté
 */
int getAuthenticatedClientUpload(struct ConnexionSecure * authSecure,int * clientActive){
    int clientUpload;
    struct sockaddr_in * aCUpload = malloc(sizeof(struct sockaddr_in));
    // tant que l'on a pas un client authentifié
    while(strcmp("348;AryW+e6",authSecure->key)!=0){
        clientUpload = acceptClient(dSUpload,aCUpload,lgUpload);
        if(recv(clientUpload, authSecure , sizeof(struct ConnexionSecure), 0)<=0){
                *clientActive=0;
        }
        if(strcmp("348;AryW+e6",authSecure->key)!=0){
            shutdownSocket(clientUpload);
        }
    }
    return clientUpload;
}

/**
 * @brief fonction qui récupère la connection d'un client sur le port d'upload
 *
 * @param communication pointeur vers un struct CommunicationFile qui représente la communication fichier avec le serveur
 * @param clientUpload un entier qui représente le client courrant
 * @param clientActive pointeur vers un entier qui représente l'activité du client courrant
 * @return aucun
 */
void recvFile(struct CommunicationFile * communication,int clientUpload, int * clientActive){
    // récupération du nom du fichier
    char * nameFile = malloc(sizeof(communication->content));
    strcpy(nameFile,communication->content);
    // récupération de la taille du fichier
    int endSize = atoi(communication->size);
    int currentSize = 0;
    int sizeBuffer = 1024;
    // création du lien vers le fichier
    char * date = getDateCurrent();
    char pathFile [strlen(nameFile)+strlen("./download/")+strlen(date)];
    strcpy(pathFile,"./download/");
    strcat(pathFile,date);
    strcat(pathFile,nameFile);
    // ouverture du fichier
    char buffer[sizeBuffer];
    FILE * fichier = fopen(pathFile,"w+");
    // copie en cours
    puts("\nCopie en cours ...\n");
    int val = 0;
    while(currentSize<endSize-1024){
        val = recv(clientUpload, buffer , 1024, 0);
        if(val<0){
            *clientActive=0;
        }
        fwrite( buffer, 1, val, fichier );
        currentSize+=strlen(buffer);
    }
    val = recv(clientUpload, buffer , 1024, 0);
    if(val<0){
        *clientActive=0;
    }
    fwrite( buffer, 1, val, fichier );
    // fin de la copie
    fclose(fichier);
    puts("Copie terminé !\n");
}

/**
 * @brief fonction qui s'occupe de gérer la demmande de recevoir un fichier du client
 *
 * @param p_data pointeur vers un entier qui représente l'activité du client courrant
 * @return aucun
 */
static void * recvFileFromClient(void * p_data) {
    puts("<> Demande d'upload de fichier");
    int * clientActive = (int *)p_data;
    // authentification
    struct ConnexionSecure * authSecure = malloc(sizeof(struct ConnexionSecure));
    struct CommunicationFile * communication = malloc(sizeof(struct CommunicationFile));
    // récupération du client authentifié
    int clientUpload=getAuthenticatedClientUpload(authSecure,clientActive);
    // récupération des caractéristiques du fichier
    if(recv(clientUpload, communication , sizeof(struct CommunicationFile), 0)<=0){
            *clientActive=0;
    }
    // vérification de la taille
    if(atoi(communication->size)<10000000&&strlen(communication->content)<30){
        recvFile(communication, clientUpload, clientActive);
    }
    free(communication);
    free(authSecure);
    shutdownSocket(clientUpload);
    addTempThread();
    pthread_exit(0);
}

////////////////////////////////////////////////////////////////////////////////////////
///                         GESTION DES FICHIERS LOCAUX                              ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui récupère la taille d'un fichier
 *
 * @param nameFile chaine de caractère qui représente un nom de fichier
 * @return taille en chaine de caractère
 */
char * getSizeOfFile(char * nameFile){
    FILE *fp;
    char size[1035];
    char command[strlen(nameFile)+strlen("cd download; ls -l ")+strlen(" | cut -d \" \" -f5; cd ../; ")];
    strcpy(command,"cd download; ls -l ");
    strcat(command,nameFile);
    strcat(command," | cut -d \" \" -f5; cd ../; ");
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
    fp = popen("cd download; ls | xargs stat --printf \"%A %n \n\" | egrep \"^-r\" | cut -d \" \" -f2; cd ../","r");
    if (fp == NULL) {
        puts("Failed to run command\n" );
    }
    int nbLignes = 0;
    // pour chaque fichiers
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
 * @brief fonction qui récupère la connection d'un client sur le port de téléchargement
 *
 * @param communication pointeur vers un struct ConnexionSecure qui représente la communication sécurisé avec le serveur
 * @param clientActive pointeur vers un entier qui représente l'activité du client courrant
 * @return entier qui représente le client connecté
 */
int getAuthenticatedClientDownload(struct ConnexionSecure * authSecure,int * clientActive){
    int clientDownload;
    struct sockaddr_in * aCDownload = malloc(sizeof(struct sockaddr_in));
    // tant que l'on a pas un utilisateur authentifié
    while(strcmp("348;AryW+e6",authSecure->key)!=0){
        clientDownload = acceptClient(dSDownload,aCDownload,lgDownload);
        if(recv(clientDownload, authSecure , sizeof(struct ConnexionSecure), 0)<=0){
                *clientActive=0;
        }
        // vérification de l'authentification
        if(strcmp("348;AryW+e6",authSecure->key)!=0){
            shutdownSocket(clientDownload);
        }
    }
    return clientDownload;
}

/**
 * @brief fonction qui s'occupe de gérer la demmande d'envoie d'un fichier au client
 *
 * @param clientDownload un entier qui représente le client
 * @param communication pointeur vers un struct CommunicationFile qui représente la communication fichier avec le serveur
 * @param files pointeur struct FileAvailable avec la liste des fichiers
 * @param clientActive pointeur vers un entier qui représente l'activité du client courrant
 * @return aucun
 */
void getFileChooseByTheClient(int clientDownload,struct CommunicationFile * communication, struct FileAvailable * files,int * clientActive){
    int i;
    communication->content[0]='\0';
    // on envoi chaque nom de fichier au client
    for (i= 0; i < files->length; ++i){
        strcpy(communication->content,files->files[i]);
        if(send(clientDownload, communication, sizeof(struct CommunicationFile), 0)==-1){
                *clientActive=0;
        }
        communication->content[0]='\0';
    }
    if(send(clientDownload, communication, sizeof(struct CommunicationFile), 0)==-1){
            *clientActive=0;
    }
    // on attend le fichier choisi par le client
    if(recv(clientDownload, communication , sizeof(struct CommunicationFile), 0)<=0){
       *clientActive=0;
    }
}

/**
 * @brief fonction qui s'occupe de gérer la demmande d'envoie d'un fichier au client
 *
 * @param clientDownload un entier qui représente le client
 * @param nameFileLocal chaine de caractère qui représente le nom de fichier à envoyer
 * @return aucun
 */
void sendFile(char * nameFileLocal, int clientDownload){
    // création du lien vers le fichier
    char nameFile[strlen(nameFileLocal)+strlen("/download/")+strlen(getenv("PWD"))];
    strcpy(nameFile,getenv("PWD"));
    strcat(nameFile,"/download/");
    strcat(nameFile,nameFileLocal);
    int endSize = atoi(getSizeOfFile(nameFile));
    int currentSize = 0;
    char buffer[1024];
    // ouverture du fichier
    FILE * fp = fopen(nameFile, "r");
    if (fp == NULL) {
       puts("[-]Error in reading file.");
    } else{
        int nb = fread(buffer,1,sizeof(buffer),fp);
        // tant qu'on est pas à la fin du fichier
        while(!feof(fp)){
            if (send(clientDownload, buffer, nb, 0) == -1) {
                puts("[-]Error in sending file.");
            }
            nb = fread(buffer,1,sizeof(buffer),fp);
        }
        if (send(clientDownload, buffer, nb, 0) == -1) {
            puts("[-]Error in sending file.");
        }
        fclose(fp);
    }
    puts("Envoi terminé !\n");
}

/**
 * @brief fonction qui s'occupe de gérer la demmande d'envoie d'un fichier au client
 *
 * @param p_data pointeur vers un entier qui représente l'activité du client courrant
 * @return aucun
 */
static void * sendFilesToClient(void * p_data){
    puts("<> Demande liste des fichiers disponibles");
    int * clientActive = (int *)p_data;
    struct FileAvailable * files =getListOfFiles();
    // authentification
    struct ConnexionSecure * authSecure = malloc(sizeof(struct ConnexionSecure));
    struct CommunicationFile * communication = malloc(sizeof(struct CommunicationFile));
    int clientDownload = getAuthenticatedClientDownload(authSecure, clientActive);
    // récupération de l'identifiant du fichier choisi par le client
    getFileChooseByTheClient(clientDownload,communication,files, clientActive);
    // si l'identifiant du fichier choisi est correct
    if (atoi(communication->content)!=0&&atoi(communication->content)>0&&atoi(communication->content)<=files->length){
        int idFiles = atoi(communication->content)-1;
        char * size = getSizeOfFile(files->files[idFiles]);
        strcpy(communication->size,size);
        strcpy(communication->content,files->files[idFiles]);
        puts(communication->content);
        // envoie des caractéristiques du fichier choisi
        if(send(clientDownload, communication, sizeof(struct CommunicationFile), 0)==-1){
            *clientActive=0;
        }
        // envoi du fichier au client
        sendFile(files->files[idFiles], clientDownload);
    } else {
        puts("Id fichier invalide");
        strcpy(communication->size,"Erreur");
        if(send(clientDownload, communication, sizeof(struct CommunicationFile), 0)==-1){
            *clientActive=0;
        }
    }
    free(communication);
    free(authSecure);
    shutdownSocket(clientDownload);
    addTempThread();
    pthread_exit(0);
}

////////////////////////////////////////////////////////////////////////////////////////
///                              GESTION DES CHANNELS                                ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui récupère les channels sauvegardé
 *
 * @return aucun
 */
void loadChannel(){
    // crée le dossier de téléchargement
    int cmd = system("mkdir logs 2>/dev/null");
    for(int i = 1 ; i<nbMaxChannels; i++){
        // création du lien vers le fichier
        char pathFile [strlen("channelii.log")+strlen("./logs/")];
        strcpy(pathFile,"./logs/");
        strcat(pathFile,"channel");
        char id[2];
        sprintf(id,"%d",i);
        strcat(pathFile,id);
        strcat(pathFile,".log");
        // ouverture du fichier
        FILE * fichier = fopen(pathFile,"r");
        if(fichier!=NULL){
            // copie en cours
            char name[12];
            fgets(name, 12, fichier);
            char description[50];
            fgets(description, 50, fichier);
            char nicknameAdmin[12];
            fgets(nicknameAdmin, 2, fichier);
            char nbClient[4];
            fgets(nbClient, 4, fichier);
            name[strlen(name)-1]='\0';
            description[strlen(description)-1]='\0';
            nicknameAdmin[strlen(nicknameAdmin)-1]='\0';
            nbClient[3]='\0';
            pthread_mutex_lock (&mutex);
            strcpy(channels[i].name,name);
            strcpy(channels[i].description,description);
            strcpy(channels[i].nicknameAdmin,nicknameAdmin);
            channels[i].nbClientMax=atoi(nbClient);
            channels[i].nbClientCurrent=1;
            channels[i].dataclient=malloc(channels[i].nbClientMax*(sizeof(struct DataClient)));
            nbCurrentChannels=i+1;
            pthread_mutex_unlock (&mutex);
            // fin de la copie
            fclose(fichier);
        }
    }
}

/**
 * @brief fonction qui sauvegarde le channel demandé
 *
 * @param idChannel un entier qui représente l'identifiant d'un channel
 * @return aucun
 */
void saveChannel(int idChannel){
    // crée le dossier de téléchargement
    int cmd = system("mkdir logs 2>/dev/null");
    // création du lien vers le fichier
    char pathFile [strlen("channelii.log")+strlen("./logs/")];
    strcpy(pathFile,"./logs/");
    strcat(pathFile,"channel");
    char id[2];
    sprintf(id,"%d",idChannel);
    strcat(pathFile,id);
    strcat(pathFile,".log");
    // si le channel existe
    if(strlen(channels[idChannel].name)!=0){
        // ouverture du fichier
        char buffer[50];
        FILE * fichier = fopen(pathFile,"w+");
        if (fichier !=NULL){
            // copie en cours
            puts("\nCopie en cours ...\n");
            fputs(channels[idChannel].name,fichier);
            fputs("\n",fichier);
            fputs(channels[idChannel].description,fichier);
            fputs("\n",fichier);
            fputs(channels[idChannel].nicknameAdmin,fichier);
            fputs("\n",fichier);
            char nbClient[2];
            sprintf(nbClient,"%d",channels[idChannel].nbClientMax);
            fputs(nbClient,fichier);
        }
        // fin de la copie
        fclose(fichier);
        puts("Copie terminé !\n");
    } else {
        char command[strlen(pathFile)+strlen("rm ")+strlen(" 2>/dev/null")];
        strcpy(command,"rm ");
        strcat(command,pathFile);
        strcat(command," 2>/dev/null");
        cmd = system(command);
    }
}

/**
 * @brief fonction qui supprime et arrange le channel demandé
 *
 * @param idChannel un entier qui représente l'identifiant d'un channel
 * @return aucun
 */
void arangeArrayChannel(int idChannel){
    if (idChannel<=nbMaxChannels&&idChannel>0){
        pthread_mutex_lock (&mutex);
        // pour chaque channel à partir de celui que l'on supprime
        for (int i = idChannel ; i < nbMaxChannels-1 ; i++) {
            channels[i].nbClientCurrent=channels[i+1].nbClientCurrent;
            channels[i].nbClientMax=channels[i+1].nbClientMax;
            channels[i].dataclient=channels[i+1].dataclient;
            strcpy(channels[i].name,channels[i+1].name);
            strcpy(channels[i].nicknameAdmin,channels[i+1].nicknameAdmin);
            strcpy(channels[i].description,channels[i+1].description);
            saveChannel(i);
        }
        channels[nbCurrentChannels-1].name[0]='\0';
        pthread_mutex_unlock (&mutex);
        saveChannel(nbCurrentChannels-1);
    }
}

/**
 * @brief fonction qui envoie la liste de channel au client courrant
 *
 * @param clientChannel un entier qui représente le client
 * @param clientActive pointeur vers un entier qui représente l'activité du client courrant
 * @return aucun
 */
void sendListOfChannel(int clientChannel,int * clientActive){
    int i = 0;
    while(i<nbCurrentChannels) {
        char stringChannels[strlen(channels[i].name)+18+strlen(channels[i].description)] ;
        char numberChannel[2];
        sprintf(numberChannel, "%d", i+1);
        char usersInChannel[2];
        sprintf(usersInChannel, "%d", channels[i].nbClientCurrent-1);
        char usersMaxChannel[2];
        sprintf(usersMaxChannel, "%d", channels[i].nbClientMax);
        strcpy(stringChannels,numberChannel);
        strcat(stringChannels," - ");
        strcat(stringChannels,channels[i].name);
        strcat(stringChannels," : ");
        strcat(stringChannels,usersInChannel);
        strcat(stringChannels,"/");
        strcat(stringChannels,usersMaxChannel);
        strcat(stringChannels," - ");
        strcat(stringChannels,channels[i].description);
        // send liste de channel
        if(send(clientChannel, stringChannels, 250, 0)==-1){
            *clientActive=0;
        }
        i+=1;
    }
    // send liste de channel
    if(send(clientChannel, "FIN", 4, 0)==-1){
        *clientActive=0;
    }
}

/**
 * @brief fonction qui traite la demmande de créer un channel du client
 *
 * @param communication pointeur vers un struct CommunicationChannel qui représente la communication channel avec le serveur
 * @return aucun
 */
void createChannelFromClient(struct CommunicationChannel * communication){
    // si on a pas atteins la limite du nombre de channels
    if(nbCurrentChannels<nbMaxChannels){
        // si les paramètres sont valides
        if(strlen(communication->name)>0&&strlen(communication->description)>0&&communication->nbClientMax>0){
            pthread_mutex_lock (&mutex);
            strcpy(channels[nbCurrentChannels].name,communication->name);
            strcpy(channels[nbCurrentChannels].description,communication->description);
            channels[nbCurrentChannels].nbClientMax=communication->nbClientMax;
            channels[nbCurrentChannels].nbClientCurrent=1;
            channels[nbCurrentChannels].dataclient=malloc(communication->nbClientMax*(sizeof(struct DataClient)));
            pthread_mutex_unlock (&mutex);
            saveChannel(nbCurrentChannels);
            pthread_mutex_lock (&mutex);
            nbCurrentChannels+=1;
            pthread_mutex_unlock (&mutex);
            communication->flag=5;
            strcpy(communication->description,"<> Création channel");
        } else {
            strcpy(communication->description,"Un paramètre est mal rempli");
        }
    } else {
        strcpy(communication->description,"<> Nombre de channel atteint");
    }
}

/**
 * @brief fonction qui traite la demmande de supprimer un channel du client
 *
 * @param communication pointeur vers un struct CommunicationChannel qui représente la communication channel avec le serveur
 * @return aucun
 */
void deleteChannelFromClient(struct CommunicationChannel * communication){
    // si l'identifiant est correcte
    if(communication->idChannel<=nbMaxChannels&&communication->idChannel>1){
        if(channels[(communication->idChannel)-1].nbClientCurrent<=1){
            arangeArrayChannel((communication->idChannel)-1);
            pthread_mutex_lock (&mutex);
            nbCurrentChannels-=1;
            pthread_mutex_unlock (&mutex);
            communication->flag=5;
            strcpy(communication->description,"Suppression channel ");
        } else {
            strcpy(communication->description,"Le channel est occupé");
        }
    } else {
        strcpy(communication->description,"Identifiant invalide");
    }
}

/**
 * @brief fonction qui traite la demmande de mettre à jour un channel du client
 *
 * @param communication pointeur vers un struct CommunicationChannel qui représente la communication channel avec le serveur
 * @return aucun
 */
void updateChannelFromClient(struct CommunicationChannel * communication){
    // si les paramètres sont valide
    if(communication->idChannel<=nbMaxChannels&&communication->idChannel>1&&strlen(communication->name)>0&&strlen(communication->description)>0&&communication->nbClientMax>0){
        if(channels[(communication->idChannel)-1].nbClientCurrent==1){
            pthread_mutex_lock (&mutex);
            strcpy(channels[(communication->idChannel)-1].name,communication->name);
            strcpy(channels[(communication->idChannel)-1].description,communication->description);
            channels[(communication->idChannel)-1].nbClientMax=communication->nbClientMax;
            channels[(communication->idChannel)-1].dataclient = (struct DataClient *) realloc( channels[(communication->idChannel)-1].dataclient, channels[(communication->idChannel)-1].nbClientMax * sizeof(struct DataClient *) );
            pthread_mutex_unlock (&mutex);
            saveChannel((communication->idChannel)-1);
            communication->flag=5;
            strcpy(communication->description,"Modification Channel");
        } else {
            strcpy(communication->description,"Le channel est occupế ");
        }
    } else {
        strcpy(communication->description,"Un paramètre est mal rempli");
    }
}

/**
 * @brief fonction qui traite la demmande de joindre un channel du client
 *
 * @param communication pointeur vers un struct CommunicationChannel qui représente la communication channel avec le serveur
 * @param communicationClient pointeur vers un struct Communication qui représente la communication classique avec le serveur
 * @return aucun
 */
void joinChannelFromClient(struct CommunicationChannel * communication,struct Communication * communicationClient){
    // si le channel choisi est valide
    if(communication->idChannel<=nbMaxChannels&&communication->idChannel>0){
        // si il y a de la place dans le channel
        if(channels[(communication->idChannel)-1].nbClientCurrent<=channels[(communication->idChannel)-1].nbClientMax){
            struct LocationClient * location = malloc(sizeof(struct LocationClient));
            location = getIdClientFromnickname(communicationClient->nickname);
            pthread_mutex_lock (&mutex);
            channels[(communication->idChannel)-1].dataclient[channels[(communication->idChannel)-1].nbClientCurrent-1].client=channels[location->idChannel].dataclient[location->idClient].client;
            channels[(communication->idChannel)-1].dataclient[channels[(communication->idChannel)-1].nbClientCurrent-1].thread=channels[location->idChannel].dataclient[location->idClient].thread;
            strcpy(channels[(communication->idChannel)-1].dataclient[channels[(communication->idChannel)-1].nbClientCurrent-1].nickname,channels[location->idChannel].dataclient[location->idClient].nickname);
            channels[(communication->idChannel)-1].nbClientCurrent+=1;
            pthread_mutex_unlock (&mutex);
            arangeArrayClient(location);
            pthread_mutex_lock (&mutex);
            channels[location->idChannel].nbClientCurrent-=1;
            pthread_mutex_unlock (&mutex);
            communication->flag=5;
            strcpy(communication->description,"Join channel");
        } else {
            strcpy(communication->description,"Impossible de joindre channel");
        }
    } else {
        strcpy(communication->description,"Identifiant invalide");
    }
}

/**
 * @brief fonction qui gére les différentes commandes du client sur les channels
 *
 * @param communicationClient pointeur vers un struct Communication qui représente la communication classique avec le serveur
 * @param clientActive pointeur vers un entier qui représente l'activité du client courrant
 * @return aucun
 */
void  manageChannels(struct Communication * communicationClient,int * clientActive) {
    puts("<> Demande de gestion des channels");
    // recupération identifiant client
    struct LocationClient * location = malloc(sizeof(struct LocationClient));
    location = getIdClientFromnickname(communicationClient->nickname);
    int clientChannel = channels[location->idChannel].dataclient[location->idClient].client;
    // envoie acceptation gestion channel
    communicationClient->flag=6;
    if(send(clientChannel, communicationClient, sizeof(struct Communication), 0)==-1){
        *clientActive=0;
    }
    struct CommunicationChannel * communication = malloc(sizeof(struct CommunicationChannel));
    int currentFlag=-1;
    while(currentFlag!=0&&*clientActive!=0){
        sendListOfChannel(clientChannel,clientActive);
        // recv command client
        if(recv(clientChannel, communication, sizeof(struct CommunicationChannel), 0) <=0) {
            *clientActive=0;
        }
        // traitement command
        switch(communication->flag)
        {
            case 1: // si le client demmande de créer un channel
                createChannelFromClient(communication);
                break;
            case 2: // si le client demmande de supprimer un channel
                deleteChannelFromClient(communication);
                break;
            case 3: // si le client demmande de mettre à jour un channel
                updateChannelFromClient(communication);
                break;
            case 4 : // si le client demmande de joindre un channel
                joinChannelFromClient(communication,communicationClient);
                break;
            default : // si le flag n'est pas reconu
                strcpy(communication->description,"<> Commande incomprise");
                break;
        }
        // si la commande a réussi
        currentFlag = communication->flag;
        if(communication->flag<5){
            communication->flag=6;
        }
        // envoie réponse au client
        if(currentFlag!=0){
            if (send(clientChannel, communication, sizeof(struct CommunicationChannel), 0) == -1) {
                *clientActive=0;
            }
        }
    }
    puts("Channel fini");
}

////////////////////////////////////////////////////////////////////////////////////////
///                                GESTION DES CLIENTS                               ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief fonction qui récupère les messages du client courrant
 *
 * @param client entier qui représente le client expéditeur
 * @param communication pointeur vers un struct Communication qui représente la communication classique avec le serveur
 * @param clientActive pointeur vers un entier qui représente l'activité du client courrant
 * @return aucun
 */
void recvMessageFromClient(int client, struct Communication * communication, int * clientActive){
    if(recv(client, communication , sizeof(struct Communication), 0)<=0){
        *clientActive=0;
    } else {
        // affichage message reçu
        printf("\nContenu reçu : %s\n", communication->msg) ;
    }
}

/**
 * @brief fonction qui envoie les messages à tous les clients du même channel que le client courrant
 *
 * @param client entier qui représente le client expéditeur
 * @param communication pointeur vers un struct Communication qui représente la communication classique avec le serveur
 * @param clientActive pointeur vers un entier qui représente l'activité du client courrant
 * @return aucun
 */
void sendMessageFromClient(int client, struct Communication *communication,int * clientActive){
    puts("<> Demande de diffusion de message");
    // on envoie le messsage à chaque client du même channel
    for (int i = 0 ; i < channels[communication->idChannel].nbClientCurrent-1 ; i++) {
        pthread_mutex_lock (&mutex);
        if (strcmp(channels[communication->idChannel].dataclient[i].nickname, communication->nickname)!=0) {
            if(send(channels[communication->idChannel].dataclient[i].client, communication, sizeof(struct Communication), 0)==-1){
                *clientActive=0;
            }
        }
        pthread_mutex_unlock (&mutex);
    }
}

/**
 * @brief fonction qui envoie un message privé à un client
 *
 * @param client entier qui représente le client expéditeur
 * @param communication pointeur vers un struct Communication qui représente la communication classique avec le serveur
 * @param clientActive pointeur vers un entier qui représente l'activité du client courrant
 * @return aucun
 */
void sendMessagePrivateFromClient(int client, struct Communication *communication,int * clientActive){
    puts("<> Demande d'envoi de message privé");
    int clientResult;
    // si le destinataire existe
    struct LocationClient * location = malloc(sizeof(struct LocationClient));
    location = getIdClientFromnickname(communication->nicknamePrivate);
    if (location->idClient!=-1) {
        pthread_mutex_lock (&mutex);
        clientResult=channels[location->idChannel].dataclient[location->idClient].client;
        pthread_mutex_unlock (&mutex);
    // si le destinataire n'existe pas
    } else {
        communication->flag=2;
        strcpy(communication->msg,"ERROR : Destinataire invalide");
        clientResult=client;
    }
    // on envoie le message
    if(send(clientResult, communication, sizeof(struct Communication), 0)==-1){
        *clientActive=0;
    }
}

/**
 * @brief fonction qui gère les communications d'un client
 *
 * @param p_data pointeur vers une chaine de caractère qui représente le pseudo du client
 * @return aucun
 */
static void * manageClient(void * p_data) {
    char msg[20];
    char * test = (char *) p_data;
    struct Communication * communication = malloc(sizeof(struct Communication));
    strcpy(communication->nickname,test);
    int  * clientActive=malloc(sizeof(int));
    *clientActive=1;
    struct LocationClient * location = malloc(sizeof(struct LocationClient));
    do{
        // mise à jour identifiant client en cas de changement
        location = getIdClientFromnickname(communication->nickname);
        communication->idChannel=location->idChannel;
        if (strlen(communication->msg) == 0){
            recvMessageFromClient(channels[location->idChannel].dataclient[location->idClient].client,communication,clientActive);
        }
        else {
            pthread_t thread;
            switch(communication->flag)
            {
                case 1: // si je suis un message diffusé
                    sendMessageFromClient(channels[location->idChannel].dataclient[location->idClient].client, communication,clientActive);
                    break;
                case 2 : // si je suis un message privé
                    sendMessagePrivateFromClient(channels[location->idChannel].dataclient[location->idClient].client, communication,clientActive);
                    break;
                case 3 : // si on demmande de m'envoyer un fichier
                    if ( pthread_create( &thread,NULL, recvFileFromClient, (void *)clientActive) ) {
                        printf("Impossible de créer le thread\n" );
                    }
                    break;
                case 4 : // si on demmande de recevoir un fichier
                    if ( pthread_create( &thread,NULL, sendFilesToClient, (void *)clientActive) ) {
                        printf("Impossible de créer le thread\n" );
                    }
                    break;
                case 5 : // si on demmande de gérer des channels
                    manageChannels(communication,clientActive);
                    break;
            }
            communication->msg[0]='\0';
        }
    } while(*clientActive&&(strcmp(communication->msg,"fin")!=0)&&active);
    stopClient(communication->nickname);
    pthread_exit(0);
}

////////////////////////////////////////////////////////////////////////////////////////
///                                     MAIN                                         ///
////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief initialise les variables du channel
 *
 * @param nbClient entier qui représente le nombre de client maximum
 * @param port entier qui représente le port du serveur
 * @param nbChannel entier qui représente le nombre de channel maximum
 * @return aucun
 */
void initDataChannel(int nbClient, int port, int nbChannel){
    channels=malloc(sizeof(struct Channel)*nbChannel);
    strcpy(channels[0].name,"Général");
    strcpy(channels[0].description,"Salon-Général");
    channels[0].nbClientCurrent=1;
    channels[0].nbClientMax=nbClient;
    channels[0].dataclient=malloc(channels[0].nbClientMax*(sizeof(struct DataClient)));
    nbMaxChannels = nbChannel;
    loadChannel();
    strcpy(AUTHENTIFICATION_KEY ,"3j9z,9?5EF,;fP9E-48;AryW+e6dzM@");
    // Initialisation du sémaphore
    sem_init(&semaphore, PTHREAD_PROCESS_SHARED, nbClient);
    // crée la socket upload
    lgUpload = malloc(sizeof(socklen_t));
    dSUpload = createSocket(port+1,lgUpload);
    lgDownload = malloc(sizeof(socklen_t));
    dSDownload = createSocket(port+2,lgDownload);
}

/**
 * @brief ejecte le client non authentifié du processus
 *
 * @param tempClient entier qui représente la socket
 * @param messageReturn pointeur vers une chaine de caractère qui représente le message à retourner à l'utilisateur
 * @return aucun
 */
void ejectUnauthenticatedClient(int tempClient, char * messageReturn){
    printf("Connexion non authentifié - Ejection de l'utilisateur\n");
    strcpy(messageReturn,"Erreur d'authentification");
    if (send(tempClient, messageReturn, 30 , 0) == -1) {
        perror("Error sur envoi");

    }
    shutdownSocket(tempClient);
    // On relache le sémaphore
    sem_post(&semaphore);
}

/**
 * @brief ejecte le client non autorisé du processus
 *
 * @param tempClient entier qui représente la socket
 * @param messageReturn pointeur vers une chaine de caractère qui représente le message à retourner à l'utilisateur
 * @return aucun
 */
void ejectUnautorizedClient(int tempClient, char * messageReturn){
    printf("Connexion non autorisé - le client utilise un nickname déjà existant ou il utilise un pseudo vide - Ejection de l'utilisateur\n");
    strcpy(messageReturn,"Le pseudo est déjà utilisé - ou votre pseudo est vide");
    if (send(tempClient, messageReturn, 60 , 0) == -1) {
        perror("Error sur envoi");

    }
    shutdownSocket(tempClient);
    // On relache le sémaphore
    sem_post(&semaphore);
}

/**
 * @brief ajout du client au processus du serveur
 *
 * @param tempClient entier qui représente la socket
 * @param messageReturn pointeur vers une chaine de caractère qui représente le message à retourner à l'utilisateur
 * @param connexion pointeur vers une struct ConnexionSecure
 * @return aucun
 */
void addClientToServeur(int tempClient, char * messageReturn, struct ConnexionSecure *connexion){
    // informer le client qu'il est connecté
    strcpy(messageReturn,"Connection accepté");
    if (send(tempClient, messageReturn, 30 , 0) == -1) {
      perror("Error sur envoi");

    }
    // enregistrement du client
    printf("Connexion authentifié \n");
    pthread_mutex_lock (&mutex);
    strcpy(channels[0].dataclient[channels[0].nbClientCurrent-1].nickname,connexion->nickname);
    channels[0].dataclient[channels[0].nbClientCurrent-1].client = tempClient;
    // creation du thread
    if ( pthread_create( &channels[0].dataclient[channels[0].nbClientCurrent-1].thread,NULL, manageClient, (void *)connexion->nickname)) {
        printf("Impossible de créer le thread\n" );
    }
    channels[0].nbClientCurrent++;
    pthread_mutex_unlock (&mutex);
}

int main(int argc, char *argv[]) {
    // vérification des paramètres
    checkParameters(argc,argv);
    // mise en place de la capture du signal pour stopper le programme
    signal(SIGINT, stopProgramme);
    // création de la socket
    socklen_t * lg = malloc(sizeof(socklen_t));
    int dS = createSocket(atoi(argv[1]),lg);
    printf("Serveur actif \n<> CTRL+C pour l'éteindre\n");
    printf("----\n");
    // initialisation dataClient
    initDataChannel(atoi(argv[2]),atoi(argv[1]),atoi(argv[3]));
    struct sockaddr_in * aC = malloc(sizeof(struct sockaddr_in));
    // crée le dossier de téléchargement
    int cmd = system("mkdir download 2>/dev/null");
    // thread fin des threads
    tempThread = malloc(sizeof(pthread_t));
    pthread_t threadCheck;
    if ( pthread_create( &threadCheck,NULL, checkThread, (void *)NULL )) {
        printf("Impossible de créer le thread\n" );
    }
    // boucle continue
    while(active){
        char messageReturn[30];
        struct ConnexionSecure *connexion = malloc(sizeof(struct ConnexionSecure));
        // en attente d'un client
        sem_wait(&semaphore);
        int tempClient = acceptClient(dS,aC,lg);
        if(recv(tempClient, connexion , sizeof(struct ConnexionSecure), 0) == -1) {
            perror("Error sur envoi");
        }
        // si le client est authentifié
        if(strcmp(connexion->key,AUTHENTIFICATION_KEY)==0){
            // si il n'a pas un nickname qui existe déjà
            struct LocationClient * location = malloc(sizeof(struct LocationClient));
            location = getIdClientFromnickname(connexion->nickname);
            if(location->idClient==-1 && strlen(connexion->nickname)>0){
                addClientToServeur(tempClient, messageReturn, connexion);
            } else {
                ejectUnautorizedClient(tempClient,messageReturn);
            }
        } else {
            ejectUnauthenticatedClient(tempClient,messageReturn);
        }
    }
    // on déconnecte la socket du serveur
    shutdownSocket(dS);
    shutdownSocket(dSUpload);
    shutdownSocket(dSDownload);
    // on attend que le thread de vérification des threads soit fini
    pthread_join(threadCheck,NULL);
    printf("Serveur fermé\n");
    return 0;
}