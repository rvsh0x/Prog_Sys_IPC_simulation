================================================================================
        RAPPORT PROJET : SIMULATION DE MAGASIN DE BRICOLAGE
          Utilisation des IPC System V sous Linux/Unix
================================================================================

================================================================================
                          TABLE DES MATIERES
================================================================================

1. INTRODUCTION ET ARCHITECTURE GENERALE
2. LES IPC SYSTEM V UTILISES
   2.1. Memoire partagee (Shared Memory)
   2.2. Semaphores
   2.3. Files de messages (Message Queues)
3. FLUX D'EXECUTION DETAILLE
   3.1. Phase d'initialisation (main.c)
   3.2. Creation des IPC (ipc.c)
   3.3. Lancement des processus
   3.4. Cycle de vie d'un client (client.c)
   3.5. Cycle de vie d'un vendeur (vendeur.c)
   3.6. Cycle de vie d'un caissier (caissier.c)
   3.7. Monitoring en temps reel (monitoring.c)
   3.8. Phase de terminaison
4. ANALYSE FONCTION PAR FONCTION
5. MECANISMES DE SYNCHRONISATION
6. GESTION DES ERREURS ET ROBUSTESSE
7. CONCLUSION

================================================================================
                    1. INTRODUCTION ET ARCHITECTURE GENERALE
================================================================================

Le projet simule le fonctionnement d'un magasin de bricolage avec trois types
d'acteurs principaux :

- CLIENTS : Arrivent, cherchent des conseils, decident d'acheter ou non, 
            paient en caisse
- VENDEURS : Specialises par rayon (10 rayons), conseillent les clients
- CAISSIERS : Encaissent les paiements

ARCHITECTURE MULTI-PROCESSUS :
------------------------------
Le programme utilise une architecture basee sur des processus independants
communiquant via des IPC System V. Chaque acteur (vendeur, caissier, client)
est un processus Unix separe.

PROCESSUS CREES :
- 1 processus initial (main) : Chef d'orchestre
- N processus vendeurs (N >= 10, au moins 1 par rayon)
- M processus caissiers (M >= 1)
- P processus clients (P >= 1)
- 1 processus monitoring optionnel (observation en temps reel)

COMMUNICATION INTER-PROCESSUS :
-------------------------------
Le projet utilise les trois mecanismes IPC System V :

1. MEMOIRE PARTAGEE : Pour stocker l'etat global du magasin
   - Etat des vendeurs (rayon, file d'attente, occupation)
   - Etat des caissiers (file d'attente, occupation)
   - Informations sur les achats en cours
   - Compteurs globaux (clients termines, etat de la simulation)

2. SEMAPHORES : Pour la synchronisation et l'exclusion mutuelle
   - Mutex pour proteger l'acces a la memoire partagee
   - Mutex pour le systeme de logs (ecriture concurrente)
   - Semaphores pour la synchronisation client-vendeur
   - Semaphores pour la synchronisation client-caissier

3. FILES DE MESSAGES : Pour la communication asynchrone
   - File vendeurs : Messages entre clients et vendeurs
   - File caissiers : Messages entre clients et caissiers

================================================================================
                      2. LES IPC SYSTEM V UTILISES
================================================================================

--------------------------------------------------------------------------------
2.1. MEMOIRE PARTAGEE (Shared Memory)
--------------------------------------------------------------------------------

PRINCIPE :
----------
La memoire partagee permet a plusieurs processus d'acceder a une meme zone
memoire. C'est le mecanisme IPC le plus rapide car il n'y a pas de copie
de donnees (contrairement aux files de messages).

STRUCTURE DE LA MEMOIRE PARTAGEE (magasin_shm_t) :
---------------------------------------------------

typedef struct {
    int nb_vendeurs;              // Nombre total de vendeurs
    int nb_caissiers;             // Nombre total de caissiers
    int nb_clients;               // Nombre total de clients
    int simulation_active;        // Flag : 1 = active, 0 = terminee
    int clients_termines;         // Compteur de clients ayant termine
    pid_t pid_initial;            // PID du processus initial
    
    vendeur_shm_t vendeurs[MAX_VENDEURS];    // Etat de chaque vendeur
    caissier_shm_t caissiers[MAX_CAISSIERS]; // Etat de chaque caissier
    achat_t achats[MAX_CLIENTS];             // Montants des achats
} magasin_shm_t;

SOUS-STRUCTURES :

vendeur_shm_t {
    int rayon;                    // Numero du rayon (0-9)
    int file_attente;             // Nombre de clients en attente
    int clients_queue[MAX_QUEUE]; // IDs des clients dans la file
    int occupe;                   // 0 = libre, 1 = occupe
    pid_t pid;                    // PID du processus vendeur
}

caissier_shm_t {
    int file_attente;             // Nombre de clients en attente
    int clients_queue[MAX_QUEUE]; // IDs des clients dans la file
    int occupe;                   // 0 = libre, 1 = occupe
    pid_t pid;                    // PID du processus caissier
}

achat_t {
    int client_id;                // ID du client
    int montant;                  // Montant de l'achat en euros
    int valide;                   // 1 = valide, 0 = deja traite
}

CREATION DE LA MEMOIRE PARTAGEE :
----------------------------------
Fichier : ipc.c, Fonction : creer_ipc()

Etape 1 : Generation d'une cle unique
    key_t key_shm = ftok(IPC_KEY_FILE, SHM_KEY_ID);
    
    - ftok() genere une cle basee sur un fichier ("/tmp/magasin.key")
    - SHM_KEY_ID est un identificateur (par ex: 'M')
    - Cette cle est unique au systeme

Etape 2 : Creation du segment
    shm_id = shmget(key_shm, sizeof(magasin_shm_t), IPC_CREAT|IPC_EXCL|0666);
    
    - shmget() cree un segment de memoire partagee
    - IPC_CREAT : Cree le segment s'il n'existe pas
    - IPC_EXCL : Erreur si le segment existe deja
    - 0666 : Permissions (lecture/ecriture pour tous)

Etape 3 : Attachement au processus
    shm = (magasin_shm_t *)shmat(shm_id, NULL, 0);
    
    - shmat() attache le segment au processus
    - Retourne un pointeur utilisable comme une variable normale
    - NULL = le systeme choisit l'adresse

Etape 4 : Initialisation
    memset(shm, 0, sizeof(magasin_shm_t));
    shm->nb_vendeurs = nb_vendeurs;
    shm->simulation_active = 0;
    ...

UTILISATION DANS LE PROGRAMME :
--------------------------------

Exemple 1 : Incrementer le compteur de clients termines (client.c)
    sem_P(SEM_MUTEX_SHM);           // Verrouiller
    shm->clients_termines++;        // Modifier
    sem_V(SEM_MUTEX_SHM);           // Deverrouiller

Exemple 2 : Ajouter un client a la file d'un vendeur (client.c)
    sem_P(SEM_MUTEX_SHM);
    int n = shm->vendeurs[vendeur_idx].file_attente;
    shm->vendeurs[vendeur_idx].clients_queue[n] = client_id;
    shm->vendeurs[vendeur_idx].file_attente++;
    sem_V(SEM_MUTEX_SHM);

Exemple 3 : Enregistrer un montant d'achat (vendeur.c)
    sem_P(SEM_MUTEX_SHM);
    shm->achats[client_id].montant = montant;
    shm->achats[client_id].valide = 1;
    sem_V(SEM_MUTEX_SHM);

DETACHEMENT ET DESTRUCTION :
----------------------------

Detachement (tous les processus a leur fin) :
    shmdt(shm);
    - Detache le segment du processus
    - Ne detruit PAS le segment

Destruction (uniquement le processus initial) :
    shmctl(shm_id, IPC_RMID, NULL);
    - Detruit definitivement le segment
    - Appele dans detruire_ipc()

--------------------------------------------------------------------------------
2.2. SEMAPHORES
--------------------------------------------------------------------------------

PRINCIPE :
----------
Les semaphores sont des compteurs utilises pour :
- EXCLUSION MUTUELLE : Proteger l'acces a des ressources partagees
- SYNCHRONISATION : Coordonner l'execution de processus

TYPES DE SEMAPHORES UTILISES :
-------------------------------

1. SEM_MUTEX_SHM (index 0)
   Role : Proteger l'acces a la memoire partagee
   Valeur initiale : 1 (disponible)
   Utilisation : Avant toute lecture/ecriture dans shm

2. SEM_MUTEX_LOG (index 1)
   Role : Proteger l'ecriture dans le fichier de log
   Valeur initiale : 1 (disponible)
   Utilisation : Dans log_message() et log_erreur()

3. SEMAPHORES VENDEURS (indices 2 a 2+nb_vendeurs-1)
   Role : Synchronisation client-vendeur (non utilise dans cette version)
   Valeur initiale : 0 (bloque)

4. SEMAPHORES CAISSIERS (indices suivants)
   Role : Synchronisation client-caissier (non utilise dans cette version)
   Valeur initiale : 0 (bloque)

CREATION DES SEMAPHORES :
-------------------------
Fichier : ipc.c, Fonction : creer_ipc()

Etape 1 : Calculer le nombre total
    nb_semaphores = SEM_VENDEUR_BASE + nb_vendeurs + nb_caissiers;
    - SEM_VENDEUR_BASE = 2 (les 2 mutex)

Etape 2 : Creer l'ensemble de semaphores
    sem_id = semget(key_sem, nb_semaphores, IPC_CREAT|IPC_EXCL|0666);
    
    - semget() cree un ensemble de semaphores
    - Tous les semaphores sont regroupes dans un seul ensemble

Etape 3 : Initialiser les valeurs
    unsigned short values[nb_semaphores];
    values[SEM_MUTEX_SHM] = 1;    // Mutex disponible
    values[SEM_MUTEX_LOG] = 1;    // Mutex disponible
    values[SEM_VENDEUR_BASE + i] = 0;  // Semaphores vendeurs bloques
    ...
    
    union semun arg;
    arg.array = values;
    semctl(sem_id, 0, SETALL, arg);

OPERATIONS SUR LES SEMAPHORES :
-------------------------------

OPERATION P (Proberen = "tester") : sem_P(int sem_index)
-------------------------------------------------------------
Fichier : ipc.c

    int sem_P(int sem_index) {
        struct sembuf op = {sem_index, -1, 0};
        return semop(sem_id, &op, 1);
    }

Comportement :
- Decremente le semaphore de 1
- Si valeur devient negative : BLOCAGE du processus
- Si valeur >= 0 : Continuer immediatement

Champs de struct sembuf :
- sem_num (sem_index) : Numero du semaphore dans l'ensemble
- sem_op (-1) : Operation a effectuer (decrementer)
- sem_flg (0) : Flags (0 = operation bloquante)

OPERATION V (Verhogen = "incrementer") : sem_V(int sem_index)
--------------------------------------------------------------
Fichier : ipc.c

    int sem_V(int sem_index) {
        struct sembuf op = {sem_index, 1, 0};
        return semop(sem_id, &op, 1);
    }

Comportement :
- Incremente le semaphore de 1
- Si des processus sont bloques sur ce semaphore : En reveiller un
- Toujours non-bloquant

EXEMPLE D'UTILISATION - SECTION CRITIQUE :
-------------------------------------------

    // ENTREE EN SECTION CRITIQUE
    sem_P(SEM_MUTEX_SHM);                    // Acquerir le verrou
    
    // CODE PROTEGE
    shm->vendeurs[i].file_attente++;         // Modifier les donnees
    int valeur = shm->clients_termines;      // Lire les donnees
    
    // SORTIE DE SECTION CRITIQUE
    sem_V(SEM_MUTEX_SHM);                    // Liberer le verrou

POURQUOI LES SEMAPHORES SONT NECESSAIRES :
-------------------------------------------

Sans semaphores, ce scenario est possible :

    Processus A                    Processus B
    ----------------------------------------
    Lit file_attente = 5
                                   Lit file_attente = 5
    Calcule 5 + 1 = 6
                                   Calcule 5 + 1 = 6
    Ecrit file_attente = 6
                                   Ecrit file_attente = 6
    
    RESULTAT : file_attente = 6 au lieu de 7 !
    (Une incrementation est perdue - RACE CONDITION)

Avec semaphores :

    Processus A                    Processus B
    ----------------------------------------
    sem_P(mutex)
    Lit file_attente = 5
    Calcule 5 + 1 = 6
    Ecrit file_attente = 6
    sem_V(mutex)
                                   sem_P(mutex)  <- Attend que A libere
                                   Lit file_attente = 6
                                   Calcule 6 + 1 = 7
                                   Ecrit file_attente = 7
                                   sem_V(mutex)
    
    RESULTAT : file_attente = 7 (CORRECT)

DESTRUCTION :
-------------
Fichier : ipc.c, Fonction : detruire_ipc()

    semctl(sem_id, 0, IPC_RMID);

--------------------------------------------------------------------------------
2.3. FILES DE MESSAGES (Message Queues)
--------------------------------------------------------------------------------

PRINCIPE :
----------
Les files de messages permettent l'echange de messages structures entre 
processus. Contrairement a la memoire partagee :
- Les donnees sont COPIEES
- La communication est ASYNCHRONE
- Le noyau gere la file (FIFO)

DEUX FILES DISTINCTES :
-----------------------

1. FILE VENDEURS (msg_vendeur_id)
   - Communication bidirectionnelle client <-> vendeur
   - Types de messages :
     * Client -> Vendeur : Demandes de renseignements
     * Vendeur -> Client : Reponses (competence, redirection, fin)

2. FILE CAISSIERS (msg_caissier_id)
   - Communication bidirectionnelle client <-> caissier
   - Types de messages :
     * Client -> Caissier : Demande de paiement
     * Caissier -> Client : Montant, confirmation

STRUCTURES DES MESSAGES :
-------------------------

Message Client -> Vendeur :

    typedef struct {
        long mtype;              // Type de message (vendeur_id + 1)
        int client_id;           // ID du client emetteur
        int vendeur_id;          // ID du vendeur destinataire
        int rayon_voulu;         // Numero du rayon recherche
        int type_requete;        // 0 = demande, 1 = decision d'achat
        int decision_achat;      // 0 = n'achete pas, 1 = achete
    } msg_client_vendeur_t;

Message Vendeur -> Client :

    typedef struct {
        long mtype;              // Type de message (client_id + 1000)
        int vendeur_id;          // ID du vendeur emetteur
        int est_competent;       // 0 = non, 1 = oui
        int vendeur_recommande;  // ID d'un autre vendeur si redirection
        int vente_terminee;      // 0 = en cours, 1 = terminee
    } msg_vendeur_client_t;

Message Client -> Caissier :

    typedef struct {
        long mtype;              // Type de message (caissier_id + 1)
        int client_id;           // ID du client
        int caissier_id;         // ID du caissier
    } msg_client_caissier_t;

Message Caissier -> Client :

    typedef struct {
        long mtype;              // Type de message (client_id + 2000)
        int montant;             // Montant a payer
        int paiement_termine;    // 0 = en cours, 1 = termine
    } msg_caissier_client_t;

CREATION DES FILES :
--------------------
Fichier : ipc.c, Fonction : creer_ipc()

    key_t key_msg_v = ftok(IPC_KEY_FILE, MSG_VENDEUR_KEY_ID);
    msg_vendeur_id = msgget(key_msg_v, IPC_CREAT|IPC_EXCL|0666);
    
    key_t key_msg_c = ftok(IPC_KEY_FILE, MSG_CAISSIER_KEY_ID);
    msg_caissier_id = msgget(key_msg_c, IPC_CREAT|IPC_EXCL|0666);

CONVENTION DES TYPES DE MESSAGES (mtype) :
-------------------------------------------

IMPORTANT : mtype DOIT etre > 0

Convention utilisee :
- Messages vers vendeur N : mtype = N + 1
- Messages du vendeur vers client C : mtype = C + 1000
- Messages vers caissier N : mtype = N + 1
- Messages du caissier vers client C : mtype = C + 2000

Pourquoi ces offsets (1000, 2000) ?
- Eviter les collisions entre types de messages
- Permettre le filtrage selectif avec msgrcv()

ENVOI D'UN MESSAGE : msgsnd()
------------------------------

Exemple : Client envoie une demande au vendeur 3

    msg_client_vendeur_t msg;
    msg.mtype = 3 + 1;              // Type = 4 (pour vendeur 3)
    msg.client_id = 7;              // Je suis le client 7
    msg.vendeur_id = 3;
    msg.rayon_voulu = 2;            // Rayon electricite
    msg.type_requete = 0;           // Demande initiale
    
    msgsnd(msg_vendeur_id, &msg, sizeof(msg) - sizeof(long), 0);
    
    Parametres de msgsnd() :
    - msg_vendeur_id : ID de la file
    - &msg : Pointeur vers le message
    - sizeof(msg) - sizeof(long) : Taille sans le champ mtype
    - 0 : Flags (0 = bloquant si file pleine)

RECEPTION D'UN MESSAGE : msgrcv()
----------------------------------

Exemple 1 : Vendeur 3 attend un message de n'importe quel client

    msg_client_vendeur_t msg;
    ssize_t ret = msgrcv(msg_vendeur_id, &msg, 
                         sizeof(msg) - sizeof(long),
                         3 + 1,           // Recevoir seulement type 4
                         IPC_NOWAIT);     // Non-bloquant
    
    Parametres de msgrcv() :
    - msg_vendeur_id : ID de la file
    - &msg : Buffer pour stocker le message recu
    - sizeof(msg) - sizeof(long) : Taille maximale
    - 3 + 1 : Type de message desire (4 = messages pour vendeur 3)
    - IPC_NOWAIT : Retourne immediatement si pas de message
    
    Valeur de retour :
    - Nombre d'octets recus si succes
    - -1 si erreur (errno = ENOMSG si pas de message)

Exemple 2 : Client 7 attend une reponse du vendeur

    msg_vendeur_client_t reponse;
    ssize_t ret = msgrcv(msg_vendeur_id, &reponse,
                         sizeof(reponse) - sizeof(long),
                         7 + 1000,        // Type 1007 (reponse pour client 7)
                         IPC_NOWAIT);

FLUX DE MESSAGES CLIENT-VENDEUR COMPLET :
------------------------------------------

1. Client 5 choisit d'aller voir vendeur 2
2. Client 5 envoie message type 3 (2+1) :
   "Bonjour, je cherche le rayon peinture (rayon 4)"
   
3. Vendeur 2 recoit le message type 3
4. Vendeur 2 consulte son rayon : c'est le rayon 1 (electricite)
5. Vendeur 2 trouve que vendeur 4 est expert du rayon 4
6. Vendeur 2 envoie message type 1005 (5+1000) :
   "Je ne suis pas competent, allez voir vendeur 4"
   
7. Client 5 recoit le message type 1005
8. Client 5 va vers vendeur 4 (appel recursif de contacter_vendeur())
9. Client 5 envoie message type 5 (4+1) au vendeur 4
10. Vendeur 4 est competent pour rayon 4
11. Vendeur 4 envoie message type 1005 : "Je suis competent, discussion..."
12. Vendeur 4 simule la discussion (sleep)
13. Vendeur 4 envoie message type 1005 : "Discussion terminee"
14. Client 5 recoit la fin et decide d'acheter
15. Client 5 envoie message type 5 avec decision_achat = 1
16. Vendeur 4 recoit la decision et enregistre le montant en memoire partagee

DESTRUCTION :
-------------
Fichier : ipc.c, Fonction : detruire_ipc()

    msgctl(msg_vendeur_id, IPC_RMID, NULL);
    msgctl(msg_caissier_id, IPC_RMID, NULL);

================================================================================
                      3. FLUX D'EXECUTION DETAILLE
================================================================================

--------------------------------------------------------------------------------
3.1. PHASE D'INITIALISATION (main.c)
--------------------------------------------------------------------------------

POINT D'ENTREE : int main(int argc, char *argv[])

Ligne 1 : Initialisation du generateur aleatoire
    srand(time(NULL));
    
    Role : Permet d'avoir des comportements differents a chaque execution
    Utilise pour : Durees aleatoires, choix de rayons, decisions d'achat

Lignes 2-10 : Verification des arguments
    if (argc != 4) usage(argv[0]);
    
    nb_vendeurs = atoi(argv[1]);
    nb_caissiers = atoi(argv[2]);
    nb_clients = atoi(argv[3]);
    
    if (nb_vendeurs < NB_RAYONS || nb_caissiers < 1 || nb_clients < 1)
        usage(argv[0]);
    
    Contrainte importante : nb_vendeurs >= 10 (NB_RAYONS)
    Garantit qu'il y a au moins un vendeur par rayon

Lignes 11-15 : Sauvegarde en variables globales
    nb_vendeurs_g = nb_vendeurs;
    nb_caissiers_g = nb_caissiers;
    nb_clients_g = nb_clients;
    
    Pourquoi ? Le gestionnaire de signal gestionnaireSignal() doit pouvoir
    acceder a ces valeurs pour terminer tous les processus

Lignes 16-20 : Initialisation du systeme de logs
    log_init();
    log_message("INITIAL", "=== Demarrage de la simulation ===");
    log_message("INITIAL", "Config: %d vendeurs, %d caissiers, %d clients",
                nb_vendeurs, nb_caissiers, nb_clients);
    
    Ouvre le fichier magasin.log en mode ajout

Lignes 21-25 : Creation des IPC
    if (creer_ipc(nb_vendeurs, nb_caissiers, nb_clients) == -1) {
        log_erreur("INITIAL", "Impossible de creer les IPC");
        exit(EXIT_FAILURE);
    }
    
    APPEL FONCTION : creer_ipc() dans ipc.c (voir section 3.2)
    Cree : Memoire partagee, semaphores, files de messages

Lignes 26-35 : Assignation des rayons aux vendeurs
    for (i = 0; i < nb_vendeurs; i++) {
        shm->vendeurs[i].rayon = (i < NB_RAYONS) ? i : (rand() % NB_RAYONS);
        shm->vendeurs[i].file_attente = 0;
        shm->vendeurs[i].occupe = 0;
    }
    
    Strategie d'assignation :
    - Vendeur 0 -> Rayon 0 (Menuiserie)
    - Vendeur 1 -> Rayon 1 (Electricite)
    - ...
    - Vendeur 9 -> Rayon 9 (Decoration)
    - Vendeur 10+ -> Rayon aleatoire parmi 0-9
    
    Garantit : Au moins un expert par rayon

Lignes 36-40 : Allocation des tableaux de PIDs
    pids_vendeurs = malloc(nb_vendeurs * sizeof(pid_t));
    pids_caissiers = malloc(nb_caissiers * sizeof(pid_t));
    pids_clients = malloc(nb_clients * sizeof(pid_t));
    
    Ces tableaux permettront d'envoyer des signaux aux processus

Lignes 41-47 : Installation des gestionnaires de signaux
    for (i = 1; i < 20; i++) {
        if (i != SIGCHLD && i != SIGKILL && i != SIGSTOP) {
            signal(i, gestionnaireSignal);
        }
    }
    
    Intercepte tous les signaux sauf :
    - SIGCHLD : Mort d'un processus enfant (normal)
    - SIGKILL : Ne peut pas etre intercepte
    - SIGSTOP : Ne peut pas etre intercepte
    
    Permet un arret propre sur Ctrl+C (SIGINT), kill (SIGTERM), etc.

Ligne 48 : Activation de la simulation
    shm->simulation_active = 1;
    
    CRUCIAL : Ce flag indique a tous les processus qu'ils peuvent demarrer
    Tous les vendeurs/caissiers verifient ce flag dans leurs boucles

--------------------------------------------------------------------------------
3.2. CREATION DES IPC (ipc.c - creer_ipc)
--------------------------------------------------------------------------------

FONCTION : int creer_ipc(int nb_vendeurs, int nb_caissiers, int nb_clients)

ETAPE 1 : Creation du fichier de cle
    fd = open(IPC_KEY_FILE, O_CREAT | O_RDWR, 0666);
    close(fd);
    
    IPC_KEY_FILE = "/tmp/magasin.key"
    Sert de base pour ftok()

ETAPE 2 : Generation des cles IPC
    key_shm = ftok(IPC_KEY_FILE, SHM_KEY_ID);      // 'M' -> Memoire
    key_sem = ftok(IPC_KEY_FILE, SEM_KEY_ID);      // 'S' -> Semaphores
    key_msg_v = ftok(IPC_KEY_FILE, MSG_VENDEUR_KEY_ID);  // 'V' -> Vendeurs
    key_msg_c = ftok(IPC_KEY_FILE, MSG_CAISSIER_KEY_ID); // 'C' -> Caissiers
    
    ftok() combine :
    - Inode du fichier
    - Identificateur de projet (caractere)
    -> Genere une cle unique (type key_t)

ETAPE 3 : Creation de la memoire partagee
    shm_id = shmget(key_shm, sizeof(magasin_shm_t), IPC_CREAT|IPC_EXCL|0666);
    
    Si le segment existe deja (erreur EEXIST) :
        - Le recuperer : shmget(key_shm, ..., 0666)
        - Le detruire : shmctl(shm_id, IPC_RMID, NULL)
        - Le recreer : shmget(key_shm, ..., IPC_CREAT|0666)
    
    Taille : sizeof(magasin_shm_t) octets
    Contient : Tous les etats des vendeurs, caissiers, achats

    shm = (magasin_shm_t *)shmat(shm_id, NULL, 0);
    
    Attachement : Le segment devient accessible via le pointeur shm

ETAPE 4 : Initialisation de la memoire partagee
    memset(shm, 0, sizeof(magasin_shm_t));
    shm->nb_vendeurs = nb_vendeurs;
    shm->nb_caissiers = nb_caissiers;
    shm->nb_clients = nb_clients;
    shm->simulation_active = 0;          // Pas encore active
    shm->clients_termines = 0;
    shm->pid_initial = getpid();

ETAPE 5 : Creation des semaphores
    nb_semaphores = SEM_VENDEUR_BASE + nb_vendeurs + nb_caissiers;
    
    Calcul du nombre :
    - SEM_MUTEX_SHM (index 0)
    - SEM_MUTEX_LOG (index 1)
    - nb_vendeurs semaphores (indices 2 a 2+nb_vendeurs-1)
    - nb_caissiers semaphores (indices suivants)
    
    sem_id = semget(key_sem, nb_semaphores, IPC_CREAT|IPC_EXCL|0666);
    
    Gestion des reliquats (comme pour la memoire partagee)

ETAPE 6 : Initialisation des semaphores
    unsigned short *values = malloc(nb_semaphores * sizeof(unsigned short));
    
    values[SEM_MUTEX_SHM] = 1;     // Mutex disponible
    values[SEM_MUTEX_LOG] = 1;     // Mutex disponible
    
    for (i = 0; i < nb_vendeurs; i++) {
        values[SEM_VENDEUR_BASE + i] = 0;     // Bloques au depart
    }
    
    for (i = 0; i < nb_caissiers; i++) {
        values[SEM_VENDEUR_BASE + nb_vendeurs + i] = 0;
    }
    
    union semun arg;
    arg.array = values;
    semctl(sem_id, 0, SETALL, arg);
    free(values);

ETAPE 7 : Creation des files de messages
    msg_vendeur_id = msgget(key_msg_v, IPC_CREAT|IPC_EXCL|0666);
    
    Gestion des reliquats :
        if (errno == EEXIST) {
            msg_vendeur_id = msgget(key_msg_v, 0666);
            msgctl(msg_vendeur_id, IPC_RMID, NULL);
            msg_vendeur_id = msgget(key_msg_v, IPC_CREAT|0666);
        }
    
    msg_caissier_id = msgget(key_msg_c, IPC_CREAT|IPC_EXCL|0666);
    
    Meme traitement pour la file caissiers

RETOUR : 0 si succes, -1 si erreur

--------------------------------------------------------------------------------
3.3. LANCEMENT DES PROCESSUS (main.c, suite)
--------------------------------------------------------------------------------

CREATION DES VENDEURS :
-----------------------
Lignes 50-70 : Boucle de creation

    for (i = 0; i < nb_vendeurs; i++) {
        pid = fork();
        
        if (pid == -1) {
            perror("fork vendeur");
            gestionnaireSignal(SIGTERM);
        }
        
        if (pid == 0) {
            // PROCESSUS ENFANT
            snprintf(id_str, sizeof(id_str), "%d", i);
            execl("./vendeur", "vendeur", id_str, NULL);
            perror("execl vendeur");
            exit(EXIT_FAILURE);
        }
        
        // PROCESSUS PARENT
        pids_vendeurs[i] = pid;
        shm->vendeurs[i].pid = pid;
    }

MECANISME fork() + execl() :
    1. fork() : Duplique le processus actuel
       - Processus parent : fork() retourne le PID de l'enfant
       - Processus enfant : fork() retourne 0
       
    2. Dans l'enfant (pid == 0) :
       execl() remplace le code du processus par le programme "./vendeur"
       
    3. Le processus enfant devient independant :
       - Son propre espace memoire (mais partage les IPC)
       - Son propre PID
       - Executera le code de vendeur.c
    
    4. Arguments passes :
       argv[0] = "vendeur" (nom du programme)
       argv[1] = id_str (numero du vendeur : "0", "1", "2", ...)
       argv[2] = NULL (marque la fin)

CREATION DES CAISSIERS :
------------------------
Lignes 72-90 : Meme principe que les vendeurs

    for (i = 0; i < nb_caissiers; i++) {
        pid = fork();
        if (pid == 0) {
            snprintf(id_str, sizeof(id_str), "%d", i);
            execl("./caissier", "caissier", id_str, NULL);
        }
        pids_caissiers[i] = pid;
        shm->caissiers[i].pid = pid;
    }

CREATION DES CLIENTS :
----------------------
Lignes 92-115 : Creation echelonnee

    for (i = 0; i < nb_clients; i++) {
        sleep(rand() % 2);    // Delai 0 ou 1 seconde
        
        pid = fork();
        if (pid == 0) {
            snprintf(id_str, sizeof(id_str), "%d", i);
            execl("./client", "client", id_str, NULL);
        }
        pids_clients[i] = pid;
    }

POURQUOI le sleep() ?
    Simule des arrivees echelonnees des clients
    Evite que tous arrivent en meme temps (plus realiste)

ETAT A CE MOMENT :
------------------
    PROCESSUS ACTIFS :
    - 1 processus initial (main)
    - N processus vendeurs (en attente de clients)
    - M processus caissiers (en attente de clients)
    - P processus clients (debut de leur parcours)

    IPC CREEES :
    - Memoire partagee attachee par tous
    - Semaphores accessibles par tous
    - Files de messages accessibles par tous

--------------------------------------------------------------------------------
3.4. CYCLE DE VIE D'UN CLIENT (client.c)
--------------------------------------------------------------------------------

FONCTION : int main(int argc, char *argv[])

INITIALISATION DU CLIENT :
--------------------------
Lignes 1-15 :
    client_id = atoi(argv[1]);
    snprintf(auteur, sizeof(auteur), "CLIENT %d", client_id);
    srand(time(NULL) + client_id + 200);
    log_init();
    attacher_ipc();

APPEL : attacher_ipc() (ipc.c)
    - Recupere les IDs de memoire partagee, semaphores, files de messages
    - Attache la memoire partagee au processus
    - Retour : 0 si succes, -1 si erreur

Installation des gestionnaires de signaux :
    for (i = 1; i < 20; i++) {
        signal(i, gestionnaireSignal);
    }

PHASE 1 : CHOIX DU RAYON
-------------------------
Lignes 20-25 :
    rayon_voulu = rand() % NB_RAYONS;
    log_message(auteur, "Entre, cherche rayon: %s", NOMS_RAYONS[rayon_voulu]);

Le client choisit aleatoirement un rayon parmi :
    0 - Menuiserie
    1 - Electricite
    2 - Plomberie
    3 - Peinture
    4 - Jardinage
    5 - Outillage
    6 - Quincaillerie
    7 - Sanitaire
    8 - Carrelage
    9 - Decoration

PHASE 2 : CHOIX DU VENDEUR
---------------------------
Lignes 27-32 :
    sem_P(SEM_MUTEX_SHM);
    int vendeur = trouver_vendeur_moins_charge();
    sem_V(SEM_MUTEX_SHM);
    
    log_message(auteur, "Choisit vendeur %d", vendeur);

APPEL : trouver_vendeur_moins_charge() (ipc.c)
    int trouver_vendeur_moins_charge(void) {
        int idx = 0;
        int min_file = shm->vendeurs[0].file_attente;
        
        for (i = 1; i < shm->nb_vendeurs; i++) {
            if (shm->vendeurs[i].file_attente < min_file) {
                min_file = shm->vendeurs[i].file_attente;
                idx = i;
            }
        }
        return idx;
    }

Strategie : Le client va vers le vendeur avec la file la plus courte
IMPORTANT : Ce vendeur n'est pas necessairement expert du rayon recherche !

PHASE 3 : INTERACTION AVEC LE VENDEUR
--------------------------------------
Lignes 34-40 :
    int vendeur_final = contacter_vendeur(vendeur, rayon_voulu, auteur);
    
    if (vendeur_final < 0 || !shm->simulation_active) {
        log_message(auteur, "Quitte le magasin");
        shmdt(shm);
        log_close();
        return EXIT_SUCCESS;
    }

APPEL : contacter_vendeur(vendeur_idx, rayon_voulu, auteur)

FONCTION contacter_vendeur() - FONCTION CLE RECURSIVE :
--------------------------------------------------------

Signature :
    int contacter_vendeur(int vendeur_idx, int rayon_voulu, char *auteur)

Etape 1 : S'ajouter a la file du vendeur
    ajouter_queue_vendeur(vendeur_idx);

APPEL : ajouter_queue_vendeur(vendeur_idx)
    void ajouter_queue_vendeur(int vendeur_idx) {
        sem_P(SEM_MUTEX_SHM);
        int n = shm->vendeurs[vendeur_idx].file_attente;
        if (n < MAX_QUEUE) {
            shm->vendeurs[vendeur_idx].clients_queue[n] = client_id;
            shm->vendeurs[vendeur_idx].file_attente++;
        }
        sem_V(SEM_MUTEX_SHM);
    }

Etape 2 : Envoyer une demande au vendeur
    msg_client_vendeur_t msg;
    msg.mtype = vendeur_idx + 1;
    msg.client_id = client_id;
    msg.vendeur_id = vendeur_idx;
    msg.rayon_voulu = rayon_voulu;
    msg.type_requete = 0;           // 0 = demande de rayon
    msg.decision_achat = 0;
    
    msgsnd(msg_vendeur_id, &msg, sizeof(msg) - sizeof(long), 0);

Etape 3 : Attendre la reponse (boucle)
    while (shm->simulation_active) {
        ssize_t ret = msgrcv(msg_vendeur_id, &reponse,
                            sizeof(reponse) - sizeof(long),
                            client_id + 1000,      // Type de message attendu
                            IPC_NOWAIT);           // Non-bloquant
        
        if (ret == -1) {
            usleep(50000);                         // Attendre 50ms
            continue;
        }
        
        // Message recu, traiter...
    }

Etape 4a : Si vendeur non competent (redirection)
    if (!reponse.est_competent) {
        retirer_queue_vendeur(vendeur_idx);
        int nouveau = reponse.vendeur_recommande;
        log_message(auteur, "Redirection vers vendeur %d", nouveau);
        
        // APPEL RECURSIF !!!
        return contacter_vendeur(nouveau, rayon_voulu, auteur);
    }

EXEMPLE DE RECURSION :
    Client 5 cherche rayon Peinture (3)
    -> Va vers vendeur 1 (file la plus courte)
    -> Vendeur 1 est expert en Electricite (rayon 1)
    -> Vendeur 1 redirige vers vendeur 3 (expert Peinture)
    -> APPEL RECURSIF : contacter_vendeur(3, 3, "CLIENT 5")
    -> Vendeur 3 est competent
    -> Discussion et retour

Etape 4b : Si discussion en cours
    if (!reponse.vente_terminee) {
        log_message(auteur, "Discussion en cours...");
        continue;        // Continuer d'attendre
    }

Etape 4c : Si discussion terminee
    log_message(auteur, "Discussion terminee");
    return vendeur_idx;

PHASE 4 : DECISION D'ACHAT
---------------------------
Lignes 42-60 :
    int decision = probabilite(PROBA_VENTE_REUSSIE);

APPEL : probabilite(65) (utils.c)
    int probabilite(int pourcentage) {
        return (rand() % 100) < pourcentage;
    }
    
    Retourne 1 avec 65% de probabilite, 0 avec 35%

Envoi de la decision au vendeur :
    msg_client_vendeur_t msg_decision;
    msg_decision.mtype = vendeur_final + 1;
    msg_decision.client_id = client_id;
    msg_decision.vendeur_id = vendeur_final;
    msg_decision.rayon_voulu = rayon_voulu;
    msg_decision.type_requete = 1;           // 1 = decision de vente
    msg_decision.decision_achat = decision;  // 0 ou 1
    
    msgsnd(msg_vendeur_id, &msg_decision, sizeof(msg_decision) - sizeof(long), 0);

Si le client n'achete pas :
    if (!decision) {
        retirer_queue_vendeur(vendeur_final);
        log_message(auteur, "N'achete pas, quitte le magasin");
        shmdt(shm);
        log_close();
        return EXIT_SUCCESS;
    }

Si le client achete, continuer...

PHASE 5 : PASSAGE EN CAISSE
----------------------------
Lignes 62-80 :
    log_message(auteur, "Decide d'acheter!");
    retirer_queue_vendeur(vendeur_final);
    
    sleep(1);    // Temps pour aller a la caisse
    
    sem_P(SEM_MUTEX_SHM);
    int caissier = trouver_caissier_moins_charge();
    int nc = shm->caissiers[caissier].file_attente;
    if (nc < MAX_QUEUE) {
        shm->caissiers[caissier].clients_queue[nc] = client_id;
        shm->caissiers[caissier].file_attente++;
    }
    sem_V(SEM_MUTEX_SHM);

APPEL : trouver_caissier_moins_charge() (ipc.c)
    Meme principe que pour les vendeurs

Envoi d'une demande au caissier :
    msg_client_caissier_t msg_caisse;
    msg_caisse.mtype = caissier + 1;
    msg_caisse.client_id = client_id;
    msg_caisse.caissier_id = caissier;
    
    msgsnd(msg_caissier_id, &msg_caisse, sizeof(msg_caisse) - sizeof(long), 0);

PHASE 6 : PAIEMENT
------------------
Lignes 82-105 :
    msg_caissier_client_t reponse_caisse;
    int montant = 0;
    
    while (shm->simulation_active) {
        ssize_t ret = msgrcv(msg_caissier_id, &reponse_caisse,
                            sizeof(reponse_caisse) - sizeof(long),
                            client_id + 2000,      // Type pour ce client
                            IPC_NOWAIT);
        
        if (ret == -1) {
            usleep(50000);
            continue;
        }
        
        if (!reponse_caisse.paiement_termine) {
            montant = reponse_caisse.montant;
            log_message(auteur, "Doit payer %d euros", montant);
            continue;
        }
        
        log_message(auteur, "Paiement effectue!");
        break;
    }

PHASE 7 : SORTIE DU MAGASIN
----------------------------
Lignes 107-115 :
    log_message(auteur, "Quitte avec ses achats. Au revoir!");
    
    sem_P(SEM_MUTEX_SHM);
    shm->clients_termines++;       // Incrementer le compteur global
    sem_V(SEM_MUTEX_SHM);
    
    shmdt(shm);
    log_close();
    
    return EXIT_SUCCESS;

Le compteur clients_termines permet au processus initial de savoir
quand tous les clients ont termine.

--------------------------------------------------------------------------------
3.5. CYCLE DE VIE D'UN VENDEUR (vendeur.c)
--------------------------------------------------------------------------------

FONCTION : int main(int argc, char *argv[])

INITIALISATION :
----------------
Lignes 1-20 :
    vendeur_id = atoi(argv[1]);
    snprintf(auteur, sizeof(auteur), "VENDEUR %d", vendeur_id);
    srand(time(NULL) + vendeur_id);
    log_init();
    attacher_ipc();
    
    mon_rayon = shm->vendeurs[vendeur_id].rayon;
    log_message(auteur, "Pret. Rayon: %s (%d)", 
                NOMS_RAYONS[mon_rayon], mon_rayon);

Le vendeur recupere son rayon d'expertise depuis la memoire partagee.

BOUCLE PRINCIPALE :
-------------------
Lignes 25-150 :
    while (continuer && shm->simulation_active) {
        
        // Attendre un message d'un client
        ssize_t ret = msgrcv(msg_vendeur_id, &msg_client,
                            sizeof(msg_client) - sizeof(long),
                            vendeur_id + 1,        // Mon type de message
                            IPC_NOWAIT);           // Non-bloquant
        
        if (ret == -1) {
            usleep(100000);     // Pas de message, attendre 100ms
            continue;
        }
        
        // Message recu, traiter le client...
    }

TRAITEMENT D'UN CLIENT :
------------------------

Extraction des informations :
    int client_id = msg_client.client_id;
    int rayon_voulu = msg_client.rayon_voulu;
    
    log_message(auteur, "Client %d demande rayon %s",
                client_id, NOMS_RAYONS[rayon_voulu]);

Marquer comme occupe :
    sem_P(SEM_MUTEX_SHM);
    shm->vendeurs[vendeur_id].occupe = 1;
    sem_V(SEM_MUTEX_SHM);

CAS 1 : VENDEUR NON COMPETENT (REDIRECTION)
--------------------------------------------
Lignes 60-85 :
    if (mon_rayon != rayon_voulu) {
        
        int vendeur_ok = trouver_vendeur_pour_rayon(rayon_voulu);
        log_message(auteur, "Redirection vers vendeur %d", vendeur_ok);
        
        msg_vendeur_client_t msg_reponse;
        msg_reponse.mtype = client_id + 1000;
        msg_reponse.vendeur_id = vendeur_id;
        msg_reponse.est_competent = 0;              // Pas competent
        msg_reponse.vendeur_recommande = vendeur_ok; // Redirection
        msg_reponse.vente_terminee = 0;
        
        msgsnd(msg_vendeur_id, &msg_reponse,
               sizeof(msg_reponse) - sizeof(long), 0);
        
        // Liberer le vendeur
        sem_P(SEM_MUTEX_SHM);
        shm->vendeurs[vendeur_id].occupe = 0;
        sem_V(SEM_MUTEX_SHM);
    }

APPEL : trouver_vendeur_pour_rayon(rayon) (ipc.c)
    int trouver_vendeur_pour_rayon(int rayon) {
        for (i = 0; i < shm->nb_vendeurs; i++) {
            if (shm->vendeurs[i].rayon == rayon) {
                return i;
            }
        }
        return 0;   // Ne devrait jamais arriver
    }

CAS 2 : VENDEUR COMPETENT (DISCUSSION)
---------------------------------------
Lignes 87-150 :
    else {
        log_message(auteur, "Discussion avec client %d", client_id);
        
        // Confirmer la competence
        msg_reponse.mtype = client_id + 1000;
        msg_reponse.est_competent = 1;          // Competent !
        msg_reponse.vendeur_recommande = -1;
        msg_reponse.vente_terminee = 0;         // Discussion en cours
        msgsnd(msg_vendeur_id, &msg_reponse,
               sizeof(msg_reponse) - sizeof(long), 0);
        
        // Simuler le temps de discussion
        int temps = tirage_aleatoire(TEMPS_DISCUSSION_MIN, TEMPS_DISCUSSION_MAX);
        sleep(temps);
        
        // Signaler la fin de la discussion
        msg_reponse.vente_terminee = 1;
        msgsnd(msg_vendeur_id, &msg_reponse,
               sizeof(msg_reponse) - sizeof(long), 0);
        
        // Attendre la decision du client...
    }

APPEL : tirage_aleatoire(min, max) (utils.c)
    int tirage_aleatoire(int min, int max) {
        return min + (rand() % (max - min + 1));
    }
    
    Exemple : tirage_aleatoire(2, 5) retourne 2, 3, 4 ou 5

ATTENTE DE LA DECISION DU CLIENT :
-----------------------------------
Lignes 110-130 :
    msg_client_vendeur_t decision;
    int recu = 0;
    
    while (!recu && continuer && shm->simulation_active) {
        ret = msgrcv(msg_vendeur_id, &decision,
                    sizeof(decision) - sizeof(long),
                    vendeur_id + 1, IPC_NOWAIT);
        
        if (ret == -1) {
            usleep(50000);
            continue;
        }
        
        // Verifier si c'est une decision (type_requete = 1)
        if (decision.type_requete == 1) {
            recu = 1;
        } else {
            // Ce n'est pas une decision, remettre le message
            msgsnd(msg_vendeur_id, &decision,
                   sizeof(decision) - sizeof(long), 0);
        }
    }

POURQUOI REMETTRE LE MESSAGE ?
    Il est possible qu'un autre client envoie une demande pendant
    qu'on attend la decision. On remet ce message dans la file pour
    le traiter au prochain tour de boucle.

TRAITEMENT DE LA DECISION :
----------------------------
Lignes 132-145 :
    if (recu && decision.decision_achat) {
        // Le client achete
        int montant = tirage_aleatoire(MONTANT_ACHAT_MIN, MONTANT_ACHAT_MAX);
        log_message(auteur, "Vente conclue: %d euros", montant);
        
        // Enregistrer l'achat en memoire partagee
        sem_P(SEM_MUTEX_SHM);
        shm->achats[client_id].client_id = client_id;
        shm->achats[client_id].montant = montant;
        shm->achats[client_id].valide = 1;      // Pour le caissier
        sem_V(SEM_MUTEX_SHM);
        
    } else if (recu) {
        log_message(auteur, "Client %d n'achete pas", client_id);
    }
    
    // Liberer le vendeur
    sem_P(SEM_MUTEX_SHM);
    shm->vendeurs[vendeur_id].occupe = 0;
    sem_V(SEM_MUTEX_SHM);

ROLE DE shm->achats[] :
    Le vendeur enregistre le montant de l'achat
    Le caissier le lira plus tard pour encaisser le client
    C'est le moyen de transferer l'information entre vendeur et caissier

TERMINAISON :
-------------
Lignes 152-157 :
    log_message(auteur, "Fin du service");
    shmdt(shm);
    log_close();
    return EXIT_SUCCESS;

Le vendeur sort de sa boucle quand :
- continuer = 0 (signal SIGINT recu)
- shm->simulation_active = 0 (simulation terminee)

--------------------------------------------------------------------------------
3.6. CYCLE DE VIE D'UN CAISSIER (caissier.c)
--------------------------------------------------------------------------------

FONCTION : int main(int argc, char *argv[])

INITIALISATION :
----------------
Lignes 1-20 :
    caissier_id = atoi(argv[1]);
    snprintf(auteur, sizeof(auteur), "CAISSIER %d", caissier_id);
    srand(time(NULL) + caissier_id + 100);
    log_init();
    attacher_ipc();
    log_message(auteur, "Caisse ouverte");

BOUCLE PRINCIPALE :
-------------------
Lignes 25-120 :
    while (continuer && shm->simulation_active) {
        
        // Attendre un client
        ssize_t ret = msgrcv(msg_caissier_id, &msg_client,
                            sizeof(msg_client) - sizeof(long),
                            caissier_id + 1,       // Mon type de message
                            IPC_NOWAIT);
        
        if (ret == -1) {
            usleep(100000);    // Pas de client, attendre 100ms
            continue;
        }
        
        // Client recu, traiter...
    }

TRAITEMENT D'UN CLIENT :
------------------------

Extraction :
    int client_id = msg_client.client_id;
    log_message(auteur, "Client %d arrive", client_id);

Marquer comme occupe :
    sem_P(SEM_MUTEX_SHM);
    shm->caissiers[caissier_id].occupe = 1;
    sem_V(SEM_MUTEX_SHM);

RECUPERATION DU MONTANT :
-------------------------
Lignes 45-55 :
    sem_P(SEM_MUTEX_SHM);
    int montant = 0;
    if (shm->achats[client_id].valide) {
        montant = shm->achats[client_id].montant;
        shm->achats[client_id].valide = 0;    // Marquer comme traite
    }
    sem_V(SEM_MUTEX_SHM);
    
    if (montant == 0) {
        montant = MONTANT_ACHAT_MIN;          // Valeur par defaut
    }

Le caissier recupere le montant enregistre par le vendeur.
Si valide = 0, le montant a deja ete traite ou n'existe pas.

ANNONCE DU PRIX :
-----------------
Lignes 57-63 :
    log_message(auteur, "Client %d doit payer %d euros", client_id, montant);
    
    msg_caissier_client_t msg_reponse;
    msg_reponse.mtype = client_id + 2000;
    msg_reponse.montant = montant;
    msg_reponse.paiement_termine = 0;       // Pas encore termine
    msgsnd(msg_caissier_id, &msg_reponse,
           sizeof(msg_reponse) - sizeof(long), 0);

SIMULATION DU PAIEMENT :
------------------------
Lignes 65-70 :
    int temps = tirage_aleatoire(TEMPS_PAIEMENT_MIN, TEMPS_PAIEMENT_MAX);
    sleep(temps);
    
    msg_reponse.paiement_termine = 1;       // Termine !
    msgsnd(msg_caissier_id, &msg_reponse,
           sizeof(msg_reponse) - sizeof(long), 0);
    
    log_message(auteur, "Client %d a paye %d euros", client_id, montant);

RETRAIT DE LA FILE :
--------------------
Lignes 72-90 :
    sem_P(SEM_MUTEX_SHM);
    int n = shm->caissiers[caissier_id].file_attente;
    
    for (i = 0; i < n; i++) {
        if (shm->caissiers[caissier_id].clients_queue[i] == client_id) {
            // Decaler les clients suivants
            for (j = i; j < n - 1; j++) {
                shm->caissiers[caissier_id].clients_queue[j] =
                    shm->caissiers[caissier_id].clients_queue[j + 1];
            }
            shm->caissiers[caissier_id].file_attente--;
            break;
        }
    }
    shm->caissiers[caissier_id].occupe = 0;
    sem_V(SEM_MUTEX_SHM);

TERMINAISON :
-------------
Lignes 92-97 :
    log_message(auteur, "Caisse fermee");
    shmdt(shm);
    log_close();
    return EXIT_SUCCESS;

--------------------------------------------------------------------------------
3.7. MONITORING EN TEMPS REEL (monitoring.c)
--------------------------------------------------------------------------------

ROLE :
------
Processus independant lance depuis un terminal separe.
Affiche en temps reel l'etat du magasin (files d'attente, occupation).

FONCTION : int main(void)

INITIALISATION :
----------------
Lignes 1-15 :
    printf("Monitoring du magasin de bricolage\n");
    printf("Tentative de connexion aux IPC...\n");
    
    if (attacher_ipc() != 0) {
        fprintf(stderr, "Erreur: Impossible d'attacher les IPC.\n");
        return EXIT_FAILURE;
    }
    
    printf("Connexion reussie!\n");
    sleep(2);

Le monitoring n'est pas lance par main.c, mais manuellement par l'utilisateur.
Il se connecte aux IPC existantes.

INSTALLATION DU GESTIONNAIRE DE SIGNAL :
-----------------------------------------
Lignes 17-22 :
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_term;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);        // Ctrl+C
    sigaction(SIGTERM, &sa, NULL);       // kill

BOUCLE DE RAFRAICHISSEMENT :
----------------------------
Lignes 24-40 :
    while (continuer) {
        afficher_etat();
        
        usleep(500000);      // Rafraichir toutes les 500ms
        
        // Verifier si la simulation est terminee
        if (!shm->simulation_active && 
            shm->clients_termines >= shm->nb_clients) {
            afficher_etat();
            printf("\n\033[33mSimulation terminee.\033[0m\n");
            break;
        }
    }
    
    detacher_ipc();
    return EXIT_SUCCESS;

FONCTION : void afficher_etat(void)
------------------------------------

EFFACEMENT DE L'ECRAN :
    void effacer_ecran(void) {
        printf("\033[2J\033[H");    // Codes ANSI
        fflush(stdout);
    }
    
    \033[2J : Efface tout l'ecran
    \033[H  : Positionne le curseur en haut a gauche

AFFICHAGE DE L'EN-TETE :
    printf("+==============================================================+\n");
    printf("|        MONITORING MAGASIN DE BRICOLAGE                      |\n");
    printf("+==============================================================+\n");

AFFICHAGE DE L'ETAT :
    if (shm->simulation_active) {
        printf("\033[32mACTIVE\033[0m");      // Vert
    } else {
        printf("\033[31mINACTIVE\033[0m");    // Rouge
    }

AFFICHAGE DES VENDEURS :
    for (i = 0; i < shm->nb_vendeurs; i++) {
        printf("| V%02d [%-15.15s] ", i, NOMS_RAYONS[shm->vendeurs[i].rayon]);
        
        if (shm->vendeurs[i].occupe) {
            printf("\033[33m[OCC]\033[0m");   // Jaune
        } else {
            printf("\033[32m[LIB]\033[0m");   // Vert
        }
        
        printf(" Clients(%d): ", shm->vendeurs[i].file_attente);
        
        // Afficher les IDs des clients (max 8)
        for (j = 0; j < shm->vendeurs[i].file_attente && j < 8; j++) {
            printf("C%d ", shm->vendeurs[i].clients_queue[j]);
        }
        if (shm->vendeurs[i].file_attente > 8) {
            printf("...");
        }
        printf("\n");
    }

EXEMPLE D'AFFICHAGE :
    | V00 [Menuiserie     ] [LIB] Clients(0): -
    | V01 [Electricite    ] [OCC] Clients(3): C2 C5 C8
    | V02 [Plomberie      ] [LIB] Clients(1): C3

AFFICHAGE DES CAISSIERS :
    Meme principe que pour les vendeurs

--------------------------------------------------------------------------------
3.8. PHASE DE TERMINAISON (main.c)
--------------------------------------------------------------------------------

ATTENTE DES CLIENTS :
---------------------
Lignes 117-122 (main.c) :
    log_message("INITIAL", "En attente de la fin des clients...");
    
    for (i = 0; i < nb_clients; i++) {
        waitpid(pids_clients[i], NULL, 0);
        pids_clients[i] = 0;
    }
    
    log_message("INITIAL", "Tous les clients ont termine.");

waitpid() bloque jusqu'a ce que le processus specifie se termine.
Le processus initial attend sequentiellement chaque client.

ARRET DE LA SIMULATION :
-------------------------
Lignes 124-127 :
    log_message("INITIAL", "Arret des vendeurs et caissiers...");
    shm->simulation_active = 0;

Ce flag indique a tous les processus de terminer leurs boucles.

ARRET DES VENDEURS :
--------------------
Lignes 129-135 :
    for (i = 0; i < nb_vendeurs; i++) {
        if (pids_vendeurs[i] > 0) {
            kill(pids_vendeurs[i], SIGINT);
        }
    }
    
    for (i = 0; i < nb_vendeurs; i++) {
        if (pids_vendeurs[i] > 0) {
            waitpid(pids_vendeurs[i], NULL, 0);
        }
    }

kill() envoie SIGINT au processus
Le gestionnaire gestionnaireSignal() met continuer = 0
Le vendeur sort de sa boucle et se termine
waitpid() attend la terminaison effective

ARRET DES CAISSIERS :
---------------------
Lignes 137-145 :
    Meme principe que pour les vendeurs

NETTOYAGE FINAL :
-----------------
Lignes 147-157 :
    log_message("INITIAL", "Nettoyage des IPC...");
    
    detacher_ipc();
    detruire_ipc();
    
    free(pids_vendeurs);
    free(pids_caissiers);
    free(pids_clients);
    
    log_message("INITIAL", "=== Simulation terminee ===");
    log_close();
    
    return EXIT_SUCCESS;

APPEL : detruire_ipc() (ipc.c)
    void detruire_ipc(void) {
        if (shm_id != -1) {
            shmctl(shm_id, IPC_RMID, NULL);     // Detruit memoire partagee
            shm_id = -1;
        }
        
        if (sem_id != -1) {
            semctl(sem_id, 0, IPC_RMID);        // Detruit semaphores
            sem_id = -1;
        }
        
        if (msg_vendeur_id != -1) {
            msgctl(msg_vendeur_id, IPC_RMID, NULL);  // Detruit file vendeurs
            msg_vendeur_id = -1;
        }
        
        if (msg_caissier_id != -1) {
            msgctl(msg_caissier_id, IPC_RMID, NULL); // Detruit file caissiers
            msg_caissier_id = -1;
        }
        
        unlink(IPC_KEY_FILE);                   // Supprime le fichier de cle
    }

IPC_RMID : Remove IDentifier - Detruit definitivement la ressource IPC

================================================================================
                  4. ANALYSE FONCTION PAR FONCTION
================================================================================

--------------------------------------------------------------------------------
4.1. FICHIER : log.c - Systeme de logging
--------------------------------------------------------------------------------

FONCTION : int log_init(void)
------------------------------
Role : Ouvre le fichier de log en mode ajout

Implementation :
    log_file = fopen(FICHIER_LOG, "a");
    if (log_file == NULL) {
        perror("[ERREUR] Ouverture fichier de log");
        return -1;
    }
    return 0;

Mode "a" : Append - Ajoute a la fin sans ecraser le contenu existant

FONCTION : void log_close(void)
--------------------------------
Role : Ferme le fichier de log

Implementation :
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }

FONCTION : static void get_timestamp(char *buffer, size_t size)
----------------------------------------------------------------
Role : Obtient l'horodatage actuel au format HH:MM:SS

Implementation :
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%H:%M:%S", tm_info);

time() : Obtient le temps Unix (secondes depuis 1970)
localtime() : Convertit en heure locale
strftime() : Formate la date/heure

FONCTION : void log_message(const char *auteur, const char *format, ...)
-------------------------------------------------------------------------
Role : Ecrit un message de log avec horodatage et protection mutex

Implementation :
    char timestamp[16];
    char message[512];
    va_list args;
    
    // Formater le message avec arguments variables
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    get_timestamp(timestamp, sizeof(timestamp));
    
    // SECTION CRITIQUE
    if (sem_id != -1) {
        sem_P(SEM_MUTEX_LOG);
    }
    
    // Ecrire dans stdout
    printf("[%s][%s] %s\n", timestamp, auteur, message);
    fflush(stdout);
    
    // Ecrire dans le fichier
    if (log_file != NULL) {
        fprintf(log_file, "[%s][%s] %s\n", timestamp, auteur, message);
        fflush(log_file);
    }
    
    // FIN SECTION CRITIQUE
    if (sem_id != -1) {
        sem_V(SEM_MUTEX_LOG);
    }

POURQUOI LE MUTEX ?
    Plusieurs processus ecrivent simultanement dans le fichier de log.
    Sans mutex, les lignes pourraient se melanger :
    
    Sans mutex :
        [12:34:56][VENDEUR 1] Client 3 [12:34:56][CLIENT 5] Va vers...
    
    Avec mutex :
        [12:34:56][VENDEUR 1] Client 3 arrive
        [12:34:56][CLIENT 5] Va vers vendeur 2

FONCTION : void log_erreur(const char *auteur, const char *format, ...)
------------------------------------------------------------------------
Role : Identique a log_message mais ecrit sur stderr

Difference :
    fprintf(stderr, "[%s][%s][ERREUR] %s\n", timestamp, auteur, message);

stderr : Flux d'erreur standard (non bufferise, affichage immediat)

--------------------------------------------------------------------------------
4.2. FICHIER : utils.c - Fonctions utilitaires
--------------------------------------------------------------------------------

FONCTION : int tirage_aleatoire(int min, int max)
--------------------------------------------------
Role : Genere un nombre aleatoire dans [min, max]

Implementation :
    return min + (rand() % (max - min + 1));

Exemple :
    tirage_aleatoire(2, 5)
    = 2 + (rand() % 4)
    = 2 + {0, 1, 2, 3}
    = {2, 3, 4, 5}

Utilisation :
    - Temps de discussion : tirage_aleatoire(2, 5) secondes
    - Temps de paiement : tirage_aleatoire(1, 3) secondes
    - Montant achat : tirage_aleatoire(10, 200) euros

FONCTION : int probabilite(int pourcentage)
--------------------------------------------
Role : Retourne 1 avec une probabilite donnee, 0 sinon

Implementation :
    return (rand() % 100) < pourcentage;

Exemple :
    probabilite(65) retourne 1 avec 65% de chance
    
    rand() % 100 donne un nombre entre 0 et 99
    Si ce nombre < 65 (donc 0-64, soit 65 valeurs) : retourne 1
    Sinon (65-99, soit 35 valeurs) : retourne 0

Utilisation :
    int decision = probabilite(PROBA_VENTE_REUSSIE);  // 65%

--------------------------------------------------------------------------------
4.3. FICHIER : ipc.c - Autres fonctions
--------------------------------------------------------------------------------

FONCTION : int sem_wait_zero(int sem_index)
--------------------------------------------
Role : Attend que le semaphore atteigne la valeur 0

Implementation :
    struct sembuf op = {sem_index, 0, 0};
    
    if (semop(sem_id, &op, 1) == -1) {
        if (errno != EINTR) {
            perror("[ERREUR] semop wait_zero");
        }
        return -1;
    }
    return 0;

sem_op = 0 : Attend que le semaphore soit a 0, puis continue
Utilisation : Synchronisation (attendre qu'une condition soit remplie)

Note : Cette fonction est definie mais non utilisee dans cette version

FONCTION : void retirer_queue_vendeur(int vendeur_idx)
-------------------------------------------------------
Role : Retire le client de la file d'attente d'un vendeur

Implementation :
    int i, j;
    
    sem_P(SEM_MUTEX_SHM);
    int n = shm->vendeurs[vendeur_idx].file_attente;
    
    // Chercher le client dans la queue
    for (i = 0; i < n; i++) {
        if (shm->vendeurs[vendeur_idx].clients_queue[i] == client_id) {
            // Decaler les clients suivants
            for (j = i; j < n - 1; j++) {
                shm->vendeurs[vendeur_idx].clients_queue[j] =
                    shm->vendeurs[vendeur_idx].clients_queue[j + 1];
            }
            shm->vendeurs[vendeur_idx].file_attente--;
            break;
        }
    }
    sem_V(SEM_MUTEX_SHM);

ALGORITHME DE DECALAGE :
    Queue initiale : [C3, C5, C7, C9]
    Retirer C5 :
        Position i = 1
        Decaler : C7 -> position 1, C9 -> position 2
    Queue finale : [C3, C7, C9, ?]
    file_attente = 3

================================================================================
                  5. MECANISMES DE SYNCHRONISATION
================================================================================

--------------------------------------------------------------------------------
5.1. EXCLUSION MUTUELLE SUR LA MEMOIRE PARTAGEE
--------------------------------------------------------------------------------

PROBLEME :
    Plusieurs processus lisent/ecrivent simultanement dans shm
    Risque de RACE CONDITION (resultats imprevisibles)

SOLUTION : Semaphore mutex SEM_MUTEX_SHM

SCHEMA D'UTILISATION :
    sem_P(SEM_MUTEX_SHM);           // Acquérir le verrou
    
    // SECTION CRITIQUE
    // Lecture/ecriture dans shm
    int valeur = shm->clients_termines;
    shm->vendeurs[i].file_attente++;
    
    sem_V(SEM_MUTEX_SHM);           // Liberer le verrou

GARANTIE :
    Un seul processus a la fois dans la section critique
    Les operations sont atomiques

EXEMPLE CONCRET :
    Client A veut s'ajouter a la file du vendeur 3 (actuellement 2 clients)
    Client B veut s'ajouter a la file du vendeur 3 (actuellement 2 clients)
    
    SANS MUTEX :
        A lit file_attente = 2
        B lit file_attente = 2
        A ecrit clients_queue[2] = client_A
        B ecrit clients_queue[2] = client_B    // ECRASE client_A !
        A ecrit file_attente = 3
        B ecrit file_attente = 3               // PAS d'incrementation !
        
        RESULTAT : 1 client perdu, file_attente incorrecte
    
    AVEC MUTEX :
        A acquiert le mutex
        A lit file_attente = 2
        A ecrit clients_queue[2] = client_A
        A ecrit file_attente = 3
        A libere le mutex
        B acquiert le mutex
        B lit file_attente = 3
        B ecrit clients_queue[3] = client_B
        B ecrit file_attente = 4
        B libere le mutex
        
        RESULTAT : Correct

--------------------------------------------------------------------------------
5.2. SYNCHRONISATION CLIENT-VENDEUR VIA FILES DE MESSAGES
--------------------------------------------------------------------------------

PROBLEME :
    Le client et le vendeur doivent communiquer de maniere asynchrone
    Le client envoie une demande et attend une reponse
    Le vendeur traite la demande quand il est disponible

SOLUTION : Files de messages avec types specifiques

MECANISME :
    1. Client envoie message type (vendeur_id + 1)
    2. Vendeur ecoute uniquement son type de message
    3. Vendeur traite et envoie reponse type (client_id + 1000)
    4. Client ecoute uniquement son type de reponse

AVANTAGE :
    - Communication asynchrone (pas de blocage inutile)
    - Filtrage automatique par type de message
    - File geree par le noyau (FIFO garanti)

SCHEMA :
    [Client 5]                              [Vendeur 2]
        |                                        |
        | msgsnd(type=3)                         |
        |--------------------------------------->|
        |    "Je cherche le rayon peinture"      |
        |                                        | (traitement)
        |                           msgrcv(type=3)
        |                                        |
        |                        msgsnd(type=1005)
        |<---------------------------------------|
        |    "Je ne suis pas competent"          |
    msgrcv(type=1005)                            |
        |                                        |

--------------------------------------------------------------------------------
5.3. COMMUNICATION CLIENT-CAISSIER
--------------------------------------------------------------------------------

MECANISME SIMILAIRE :
    1. Client envoie message type (caissier_id + 1)
    2. Caissier ecoute son type
    3. Caissier envoie reponse type (client_id + 2000)
    4. Client ecoute son type de reponse

OFFSET DIFFERENT (2000 au lieu de 1000) :
    Evite les collisions entre reponses vendeurs et caissiers
    Client ne confondra pas une reponse vendeur avec une reponse caissier

--------------------------------------------------------------------------------
5.4. SYNCHRONISATION DU DEMARRAGE
--------------------------------------------------------------------------------

PROBLEME :
    Les processus vendeurs/caissiers demarrent avant les clients
    Ils doivent attendre que tout soit pret avant de commencer

SOLUTION : Flag simulation_active dans la memoire partagee

SEQUENCE :
    1. Processus initial cree les IPC
    2. shm->simulation_active = 0
    3. Lancement des vendeurs (attendent simulation_active = 1)
    4. Lancement des caissiers (attendent simulation_active = 1)
    5. shm->simulation_active = 1          <-- FEU VERT
    6. Lancement des clients
    7. Tous les processus commencent a fonctionner

CODE DANS LES BOUCLES :
    while (continuer && shm->simulation_active) {
        // Traiter les demandes
    }

--------------------------------------------------------------------------------
5.5. SYNCHRONISATION DE LA TERMINAISON
--------------------------------------------------------------------------------

PROBLEME :
    Comment savoir quand tous les clients ont termine ?
    Comment arreter proprement vendeurs et caissiers ?

SOLUTION 1 : Compteur clients_termines
    Chaque client incremente ce compteur a sa sortie
    
    sem_P(SEM_MUTEX_SHM);
    shm->clients_termines++;
    sem_V(SEM_MUTEX_SHM);

SOLUTION 2 : Flag simulation_active
    Le processus initial attend tous les clients (waitpid)
    Puis met simulation_active = 0
    Les vendeurs/caissiers sortent de leurs boucles
    
    shm->simulation_active = 0;

SOLUTION 3 : Signaux SIGINT
    Le processus initial envoie SIGINT a tous les vendeurs/caissiers
    Leurs gestionnaires mettent continuer = 0
    Ils terminent proprement
    
    kill(pids_vendeurs[i], SIGINT);

SEQUENCE COMPLETE :
    1. Clients terminent leurs parcours
    2. Processus initial detecte la fin (waitpid sur tous les clients)
    3. shm->simulation_active = 0
    4. Envoi SIGINT aux vendeurs
    5. Envoi SIGINT aux caissiers
    6. Attente de leur terminaison (waitpid)
    7. Nettoyage des IPC
    8. Fin du programme

================================================================================
                  6. GESTION DES ERREURS ET ROBUSTESSE
================================================================================

--------------------------------------------------------------------------------
6.1. VERIFICATION DES APPELS SYSTEME
--------------------------------------------------------------------------------

PRINCIPE :
    Tous les appels systeme sont verifies
    En cas d'erreur, message d'erreur et terminaison propre

EXEMPLES :

Creation memoire partagee :
    shm_id = shmget(key_shm, sizeof(magasin_shm_t), IPC_CREAT|IPC_EXCL|0666);
    if (shm_id == -1) {
        if (errno == EEXIST) {
            // Gestion des reliquats
        }
        if (shm_id == -1) {
            perror("[ERREUR] shmget");
            return -1;
        }
    }

Fork :
    pid = fork();
    if (pid == -1) {
        perror("fork vendeur");
        gestionnaireSignal(SIGTERM);    // Arret propre
    }

Attachement IPC :
    if (attacher_ipc() == -1) {
        log_erreur(auteur, "Impossible d'attacher les IPC");
        exit(EXIT_FAILURE);
    }

--------------------------------------------------------------------------------
6.2. GESTION DES RELIQUATS IPC
--------------------------------------------------------------------------------

PROBLEME :
    Si le programme se termine anormalement (crash, kill -9)
    Les IPC restent dans le systeme (orphelines)
    La prochaine execution echoue (IPC deja existantes)

SOLUTION :
    Lors de la creation, si EEXIST :
    1. Recuperer l'IPC existante
    2. La detruire
    3. La recreer
    
    if (errno == EEXIST) {
        shm_id = shmget(key_shm, sizeof(magasin_shm_t), 0666);
        if (shm_id != -1) {
            shmctl(shm_id, IPC_RMID, NULL);
        }
        shm_id = shmget(key_shm, sizeof(magasin_shm_t), IPC_CREAT|0666);
    }

VERIFICATION MANUELLE :
    ipcs        : Lister les IPC existantes
    ipcrm       : Supprimer manuellement une IPC

--------------------------------------------------------------------------------
6.3. GESTION DES SIGNAUX
--------------------------------------------------------------------------------

PRINCIPE :
    Intercepter tous les signaux pour terminer proprement
    Eviter de laisser des processus zombies ou des IPC orphelines

GESTIONNAIRE DANS main.c :
    void gestionnaireSignal(int sig) {
        log_message("INITIAL", "Signal recu [%d], arret...", sig);
        
        // Arreter tous les enfants
        for (i = 0; i < nb_vendeurs_g; i++) {
            kill(pids_vendeurs[i], SIGINT);
        }
        for (i = 0; i < nb_caissiers_g; i++) {
            kill(pids_caissiers[i], SIGINT);
        }
        
        // Attendre leur terminaison
        while (wait(NULL) > 0);
        
        // Nettoyer les IPC
        detacher_ipc();
        detruire_ipc();
        
        // Liberer la memoire
        free(pids_vendeurs);
        free(pids_caissiers);
        free(pids_clients);
        
        exit(EXIT_SUCCESS);
    }

GESTIONNAIRE DANS LES AUTRES PROCESSUS :
    void gestionnaireSignal(int sig) {
        continuer = 0;    // Sort de la boucle principale
    }

INSTALLATION :
    for (i = 1; i < 20; i++) {
        if (i != SIGCHLD && i != SIGKILL && i != SIGSTOP) {
            signal(i, gestionnaireSignal);
        }
    }

--------------------------------------------------------------------------------
6.4. OPERATIONS NON-BLOQUANTES
--------------------------------------------------------------------------------

PRINCIPE :
    Les operations msgrcv() sont non-bloquantes (IPC_NOWAIT)
    Evite qu'un processus reste bloque indefiniment

EXEMPLE :
    ret = msgrcv(msg_vendeur_id, &msg_client,
                sizeof(msg_client) - sizeof(long),
                vendeur_id + 1, IPC_NOWAIT);
    
    if (ret == -1) {
        usleep(100000);    // Attendre 100ms
        continue;
    }

AVANTAGE :
    Le processus peut verifier simulation_active regulierement
    Peut reagir rapidement a un signal ou un arret de simulation

--------------------------------------------------------------------------------
6.5. VERIFICATION DES LIMITES
--------------------------------------------------------------------------------

EXEMPLE : Limite de la file d'attente
    if (n < MAX_QUEUE) {
        shm->vendeurs[vendeur_idx].clients_queue[n] = client_id;
        shm->vendeurs[vendeur_idx].file_attente++;
    }

Evite un debordement de tableau si trop de clients dans la file

EXEMPLE : Verification du montant
    if (montant == 0) {
        montant = MONTANT_ACHAT_MIN;    // Valeur par defaut
    }

Garantit qu'un montant valide est toujours utilise

--------------------------------------------------------------------------------
6.6. DETACHEMENT SYSTEMATIQUE
--------------------------------------------------------------------------------

PRINCIPE :
    Chaque processus detache la memoire partagee avant de terminer
    
    shmdt(shm);
    log_close();
    return EXIT_SUCCESS;

Evite les fuites de ressources
Seul le processus initial detruit les IPC (detruire_ipc)

================================================================================
                            7. CONCLUSION
================================================================================

--------------------------------------------------------------------------------
7.1. POINTS FORTS DU PROJET
--------------------------------------------------------------------------------

1. UTILISATION COMPLETE DES IPC SYSTEM V
   - Memoire partagee : Etat global partage efficacement
   - Semaphores : Synchronisation et exclusion mutuelle
   - Files de messages : Communication asynchrone structuree

2. ARCHITECTURE MULTI-PROCESSUS REALISTE
   - Chaque acteur est un processus independant
   - Communication via IPC (pas de variables globales partagees)
   - Simulation realiste d'un environnement concurrent

3. SYNCHRONISATION ROBUSTE
   - Mutex pour proteger les sections critiques
   - Files de messages avec types pour le routage
   - Flags de synchronisation (simulation_active)

4. GESTION PROPRE DES RESSOURCES
   - Creation et destruction systematique des IPC
   - Gestion des reliquats
   - Interception des signaux pour terminaison propre

5. MONITORING EN TEMPS REEL
   - Visualisation de l'etat du systeme
   - Affichage dynamique avec rafraichissement
   - Codes ANSI pour les couleurs

6. LOGGING COMPLET
   - Traçabilite de tous les evenements
   - Protection mutex pour ecriture concurrente
   - Horodatage de chaque message

--------------------------------------------------------------------------------
7.2. CONCEPTS IPC SYSTEM V DEMONTRES
--------------------------------------------------------------------------------

1. MEMOIRE PARTAGEE (shmget, shmat, shmdt, shmctl)
   - Creation avec ftok() pour cle unique
   - Attachement a chaque processus
   - Acces direct (pointeur)
   - Destruction par processus initial uniquement

2. SEMAPHORES (semget, semop, semctl)
   - Creation d'ensembles de semaphores
   - Initialisation des valeurs
   - Operations P (wait) et V (signal)
   - Union semun pour semctl

3. FILES DE MESSAGES (msgget, msgsnd, msgrcv, msgctl)
   - Creation de files independantes
   - Types de messages pour routage selectif
   - Envoi asynchrone
   - Reception filtree et non-bloquante

--------------------------------------------------------------------------------
7.3. PATTERNS DE CONCEPTION UTILISES
--------------------------------------------------------------------------------

1. PRODUCER-CONSUMER
   - Clients produisent des demandes
   - Vendeurs/Caissiers consomment les demandes

2. LOAD BALANCING
   - Choix du vendeur/caissier le moins charge
   - Repartition automatique de la charge

3. REDIRECTION/ROUTING
   - Vendeur non competent redirige vers expert
   - Appel recursif pour gerer les redirections

4. STATE MACHINE
   - Clients : Entre -> Rayon -> Vendeur -> Decision -> Caisse -> Sortie
   - Vendeurs : Attente -> Traitement -> Attente
   - Caissiers : Attente -> Encaissement -> Attente

5. MASTER-WORKER
   - Processus initial (master) cree et gere les workers
   - Workers (vendeurs, caissiers, clients) executent leurs taches

--------------------------------------------------------------------------------
7.4. AMELIORATIONS POSSIBLES
--------------------------------------------------------------------------------

1. GESTION DES PRIORITES
   - Clients VIP avec priorite
   - Files prioritaires

2. STATISTIQUES AVANCEES
   - Temps d'attente moyen
   - Taux d'occupation des vendeurs/caissiers
   - Taux de conversion (achats / visites)

3. SCENARIOS COMPLEXES
   - Pauses des vendeurs/caissiers
   - Pannes de caisse
   - Promotions temporaires

4. OPTIMISATION
   - Semaphores client-vendeur pour synchronisation directe
   - Pool de messages pour reduire les allocations
   - Cache pour les recherches frequentes

5. INTERFACE GRAPHIQUE
   - Interface SDL/GTK pour visualisation
   - Animation des deplacements
   - Graphiques en temps reel

6. PERSISTANCE
   - Sauvegarde des statistiques
   - Historique des transactions
   - Analyse post-simulation

--------------------------------------------------------------------------------
7.5. COMMANDES UTILES
--------------------------------------------------------------------------------

COMPILATION :
    make                    # Compiler tous les executables
    make clean              # Nettoyer les fichiers objets

EXECUTION :
    ./main 10 3 20          # 10 vendeurs, 3 caissiers, 20 clients

MONITORING :
    ./monitoring            # Dans un terminal separe

GESTION IPC :
    ipcs                    # Lister les IPC
    ipcs -m                 # Memoires partagees
    ipcs -s                 # Semaphores
    ipcs -q                 # Files de messages
    
    ipcrm -m <shm_id>       # Supprimer une memoire partagee
    ipcrm -s <sem_id>       # Supprimer des semaphores
    ipcrm -q <msg_id>       # Supprimer une file de messages

DEBUGGAGE :
    ps aux | grep main      # Voir les processus actifs
    ps aux | grep vendeur
    ps aux | grep caissier
    ps aux | grep client
    
    kill -9 <pid>           # Tuer un processus force
    killall main            # Tuer tous les processus main
    
    tail -f magasin.log     # Suivre le log en temps reel
    cat magasin.log         # Voir tout le log

--------------------------------------------------------------------------------
7.6. RESUME TECHNIQUE
--------------------------------------------------------------------------------

LANGAGE : C (POSIX)
SYSTEME : Linux/Unix
IPC : System V (memoire partagee, semaphores, files de messages)
PROCESSUS : fork() + execl()
SYNCHRONISATION : Semaphores mutex, files de messages typees
LOGS : Fichier + stdout/stderr avec mutex
MONITORING : Processus independant en lecture seule

FICHIERS SOURCE :
    main.c          - Processus initial, creation IPC, orchestration
    ipc.c           - Gestion des IPC (creation, destruction, utilitaires)
    client.c        - Parcours du client dans le magasin
    vendeur.c       - Conseil et vente
    caissier.c      - Encaissement
    monitoring.c    - Visualisation en temps reel
    log.c           - Systeme de logging
    utils.c         - Fonctions utilitaires

FICHIERS HEADER :
    config.h        - Constantes de configuration
    ipc.h           - Declarations IPC
    log.h           - Declarations logging
    utils.h         - Declarations utilitaires

FICHIERS GENERES :
    magasin.log     - Fichier de log
    /tmp/magasin.key- Fichier pour ftok()

STRUCTURES DE DONNEES :
    magasin_shm_t   - Etat global du magasin
    vendeur_shm_t   - Etat d'un vendeur
    caissier_shm_t  - Etat d'un caissier
    achat_t         - Information d'achat
    msg_*_t         - Messages pour les files

--------------------------------------------------------------------------------
7.7. FLUX DE DONNEES PRINCIPAL
--------------------------------------------------------------------------------

1. INITIALISATION
   main.c : Cree IPC -> Lance processus -> Active simulation

2. ARRIVEE CLIENT
   client.c : Choisit rayon -> Trouve vendeur moins charge

3. INTERACTION VENDEUR
   client.c : Envoie demande (file messages)
   vendeur.c : Recoit demande
   vendeur.c : Verifie competence
   
   SI NON COMPETENT :
       vendeur.c : Trouve expert -> Envoie redirection
       client.c : Recoit redirection -> RECURSION
   
   SI COMPETENT :
       vendeur.c : Discussion (sleep) -> Envoie fin discussion
       client.c : Recoit fin -> Decide acheter/pas acheter

4. DECISION ACHAT
   client.c : Envoie decision (file messages)
   vendeur.c : Recoit decision
   
   SI ACHAT :
       vendeur.c : Genere montant -> Enregistre shm->achats[]
       client.c : Va en caisse
   
   SI PAS ACHAT :
       client.c : Quitte le magasin

5. PASSAGE CAISSE (si achat)
   client.c : Choisit caissier moins charge -> Envoie demande
   caissier.c : Recoit demande -> Lit montant (shm->achats[])
   caissier.c : Envoie montant -> Paiement (sleep) -> Envoie confirmation
   client.c : Recoit montant -> Recoit confirmation

6. SORTIE
   client.c : Incremente shm->clients_termines -> Termine

7. TERMINAISON
   main.c : Attend tous clients -> simulation_active = 0
   main.c : Kill vendeurs/caissiers -> Attend terminaison
   main.c : Detruit IPC -> Termine

--------------------------------------------------------------------------------
7.8. SCENARIOS D'EXECUTION
--------------------------------------------------------------------------------

SCENARIO 1 : EXECUTION NORMALE
    $ ./main 10 2 5
    [12:00:00][INITIAL] === Demarrage de la simulation ===
    [12:00:00][INITIAL] Config: 10 vendeurs, 2 caissiers, 5 clients
    [12:00:00][INITIAL] IPC crees
    [12:00:01][VENDEUR 0] Pret. Rayon: Menuiserie (0)
    [12:00:01][VENDEUR 1] Pret. Rayon: Electricite (1)
    ...
    [12:00:02][CLIENT 0] Entre, cherche rayon: Peinture
    [12:00:02][CLIENT 0] Choisit vendeur 3
    [12:00:02][CLIENT 0] Va vers vendeur 3
    [12:00:02][VENDEUR 3] Client 0 demande rayon Peinture
    [12:00:02][VENDEUR 3] Discussion avec client 0
    [12:00:05][VENDEUR 3] Vente conclue: 45 euros
    [12:00:05][CLIENT 0] Decide d'acheter!
    [12:00:06][CLIENT 0] Va vers caissier 0
    [12:00:06][CAISSIER 0] Client 0 arrive
    [12:00:06][CAISSIER 0] Client 0 doit payer 45 euros
    [12:00:08][CAISSIER 0] Client 0 a paye 45 euros
    [12:00:08][CLIENT 0] Paiement effectue!
    [12:00:08][CLIENT 0] Quitte avec ses achats. Au revoir!
    ...
    [12:05:30][INITIAL] Tous les clients ont termine.
    [12:05:30][INITIAL] === Simulation terminee ===

SCENARIO 2 : REDIRECTION
    [12:00:02][CLIENT 1] Entre, cherche rayon: Electricite
    [12:00:02][CLIENT 1] Choisit vendeur 5
    [12:00:02][VENDEUR 5] Client 1 demande rayon Electricite
    [12:00:02][VENDEUR 5] Redirection vers vendeur 1
    [12:00:02][CLIENT 1] Redirection vers vendeur 1
    [12:00:02][VENDEUR 1] Client 1 demande rayon Electricite
    [12:00:02][VENDEUR 1] Discussion avec client 1
    ...

SCENARIO 3 : REFUS D'ACHAT
    [12:00:10][CLIENT 3] Entre, cherche rayon: Jardinage
    [12:00:10][CLIENT 3] Choisit vendeur 4
    [12:00:10][VENDEUR 4] Client 3 demande rayon Jardinage
    [12:00:10][VENDEUR 4] Discussion avec client 3
    [12:00:13][VENDEUR 4] Client 3 n'achete pas
    [12:00:13][CLIENT 3] N'achete pas, quitte le magasin

SCENARIO 4 : INTERRUPTION (Ctrl+C)
    [12:02:00][INITIAL] Signal recu [2], arret du programme...
    [12:02:00][VENDEUR 0] Fin du service
    [12:02:00][VENDEUR 1] Fin du service
    [12:02:00][CAISSIER 0] Caisse fermee
    [12:02:00][INITIAL] Nettoyage termine. Au revoir!

--------------------------------------------------------------------------------
7.9. VERIFICATION DE LA CORRECTION
--------------------------------------------------------------------------------

TESTS A EFFECTUER :

1. TEST BASIQUE
   ./main 10 2 5
   -> Verifier que tous les clients terminent
   -> Verifier que les IPC sont nettoyees (ipcs)

2. TEST CHARGE
   ./main 15 5 50
   -> Verifier pas de deadlock
   -> Verifier pas de fuite memoire

3. TEST INTERRUPTION
   ./main 10 2 20
   -> Ctrl+C pendant l'execution
   -> Verifier terminaison propre
   -> Verifier nettoyage IPC

4. TEST MONITORING
   Terminal 1 : ./main 10 3 30
   Terminal 2 : ./monitoring
   -> Verifier affichage temps reel
   -> Verifier coherence des donnees

5. TEST LOGS
   ./main 10 2 10
   cat magasin.log
   -> Verifier absence de melange de lignes
   -> Verifier chronologie coherente

6. TEST RELIQUATS
   kill -9 <pid_main>     # Tuer brutalement
   ipcs                   # Verifier IPC orphelines
   ./main 10 2 5          # Doit nettoyer et redemarrer

VERIFICATION MEMOIRE :
   valgrind --leak-check=full ./main 10 2 5
   -> Verifier absence de fuites memoire

VERIFICATION DEADLOCK :
   Lancer avec beaucoup de clients
   -> Si blocage : deadlock
   -> Verifier semaphores bien liberes

--------------------------------------------------------------------------------
7.10. COMPREHENSION DES IPC SYSTEM V
--------------------------------------------------------------------------------

MEMOIRE PARTAGEE :
    QUAND : Partage d'etat entre processus, acces frequents
    POURQUOI : Plus rapide (pas de copie), acces direct
    ATTENTION : Necessite synchronisation (mutex)

SEMAPHORES :
    QUAND : Exclusion mutuelle, synchronisation
    POURQUOI : Gere par le noyau, atomic operations
    ATTENTION : Risque de deadlock si mal utilises

FILES DE MESSAGES :
    QUAND : Communication asynchrone, messages structures
    POURQUOI : FIFO garanti, filtrage par type
    ATTENTION : Copie des donnees (moins rapide que SHM)

COMPARAISON AVEC AUTRES IPC :

PIPES :
    + Simple, rapide
    - Unidirectionnel, pas de structure
    - Seulement pere-fils

SIGNAUX :
    + Asynchrone, simple
    - Information limitee (juste le numero)
    - Pas de donnees complexes

SOCKETS :
    + Reseau, flexible
    - Plus complexe, plus lent localement
    - Overhead protocole

IPC POSIX (shm_open, sem_open, mq_open) :
    + Interface plus moderne
    + Noms symboliques
    - Moins portable (pas tous les Unix)

================================================================================
                        FIN DU RAPPORT
================================================================================

Ce projet illustre parfaitement l'utilisation des IPC System V dans un
contexte reel de programmation systeme. La comprehension de ces mecanismes
est essentielle pour le developpement d'applications multi-processus robustes
et performantes sous Unix/Linux.

Les trois types d'IPC (memoire partagee, semaphores, files de messages) sont
utilises de maniere complementaire pour resoudre differents problemes de
communication et de synchronisation.

La conception modulaire, avec un fichier par type de processus, permet une
maintenance aisee et une comprehension claire des interactions entre les
differents acteurs du systeme.

AUTEUR : Rapport genere pour le projet de simulation de magasin de bricolage
DATE : 2026
VERSION : 1.0
================================================================================
