struct Channel
{
    char name[12];
    char description[50];
    int nbClientCurrent;
    int nbClientMax;
    char nicknameAdmin[12];
    struct DataClient * dataclient;
};

struct LocationClient
{
    int idChannel;
    int idClient;
};
