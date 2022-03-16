# ![](https://www.polytech.umontpellier.fr/images/logo_entete.png) Projet FAR 
### Polytech Montpellier & Informatique et Gestion

* **Groupe :** Florian Davila, [Xavier Corbier](https://xaviercorbier.fr)
* **Matière :** FAR

# Projet FAR 2020-2021 : une application de messagerie instantanée

## Organisation du répertoire
Les fichiers nécéssaire au client et au serveur ont été rassemblé dans le même répertoire. 
Pour le fonctionnement du serveur il est nécéssaire d'utiliser le fichier serveur.c et les fichiers .h.
Pour exécuter compiler le serveur, vous pouvez utiliser la commande suivante :
```
gcc serveur.c -o serveur -lpthread
```
Pour le fonctionnement du client il est nécéssaire d'utiliser le fichier client.c, le fichiers communication.h et le fichier manuel.txt.
Pour exécuter compiler le client, vous pouvez utiliser la commande suivante :
```
gcc client.c -o client -lpthread
```

## Organisation des bibliothèques 

Le fichier communication.h contient tout les structures nécessaires uniquement à la communication entre le serveur et les clients. 
Le fichier structClient.h contient les structures qu'utilise le serveur pour stocker les clients et les salons.