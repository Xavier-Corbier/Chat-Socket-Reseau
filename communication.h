
struct Communication
{
    int flag; // 1 si broadcast, 2 si message priv�, 3 upload fichier, 4 demande download, 5 demande Gestion channel, 6 acceptation demande channel

    char nickname[12];
    char nicknamePrivate[12];
    char msg[50];
    int idChannel;
};

struct CommunicationFile
{
    char size[12];
    char content[200];
};

struct CommunicationChannel
{
    int flag; // 0, si la communication doit �tre coup�, 1 si cre�e un channel , 2 si delete, 3 update, 4 join, 5 resultat commande Success, 6 resultat commande error
    int idChannel;
    char name[12];
    char description[50];
    int nbClientMax;
};

struct ConnexionSecure
{
    char key[32];
    char nickname[12];
};

struct DataClient
{
    int client; // accept
    pthread_t thread; // idthread
    char nickname[12]; // pseudo
};

struct DataUpload
{
    int dS;
    char adresse[20];
    int port;
    char content[12];
    int value;
};

struct FileAvailable
{
    int length;
    char ** files;
};
char AUTHENTIFICATION_KEY[32];

