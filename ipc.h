/*
 * =============================================================================
 * Fichier     : ipc.h
 * Description : Definitions des structures et fonctions IPC pour la simulation
 * 
 * Ce fichier definit :
 *   - Les structures de donnees en memoire partagee (vendeurs, caissiers, achats)
 *   - Les structures de messages pour les files de messages
 *   - Les index des semaphores
 *   - Les prototypes des fonctions IPC
 * 
 * IPC System V utilisees :
 *   - Memoire partagee (shmget, shmat, shmdt, shmctl)
 *   - Semaphores (semget, semop, semctl)
 *   - Files de messages (msgget, msgsnd, msgrcv, msgctl)
 * 
 * =============================================================================
 */

#ifndef IPC_H
#define IPC_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <signal.h>
#include "config.h"

/* ============== STRUCTURES DE DONNEES ============== */

/*
 * Structure : vendeur_info_t
 * Description : Informations sur un vendeur en memoire partagee
 */
typedef struct {
    int rayon;                      /* Rayon d'expertise (0 a NB_RAYONS-1) */
    int file_attente;               /* Nombre de clients en attente */
    int occupe;                     /* 1 si occupe avec un client */
    pid_t pid;                      /* PID du processus vendeur */
    int clients_queue[MAX_QUEUE];   /* IDs des clients dans la queue */
} vendeur_info_t;

/*
 * Structure : caissier_info_t
 * Description : Informations sur un caissier en memoire partagee
 */
typedef struct {
    int file_attente;               /* Nombre de clients en attente */
    int occupe;                     /* 1 si occupe avec un client */
    pid_t pid;                      /* PID du processus caissier */
    int clients_queue[MAX_QUEUE];   /* IDs des clients dans la queue */
} caissier_info_t;

/*
 * Structure : achat_info_t
 * Description : Information sur un achat (transmission vendeur -> caissier)
 */
typedef struct {
    int client_id;                  /* Numero du client */
    int montant;                    /* Montant de l'achat en euros */
    int valide;                     /* 1 si l'entree est valide */
} achat_info_t;

/*
 * Structure : magasin_shm_t
 * Description : Structure principale en memoire partagee
 *               Contient l'etat global du magasin
 */
typedef struct {
    int nb_vendeurs;                /* Nombre de vendeurs */
    int nb_caissiers;               /* Nombre de caissiers */
    int nb_clients;                 /* Nombre total de clients */
    int simulation_active;          /* 1 si la simulation est en cours */
    
    vendeur_info_t vendeurs[MAX_VENDEURS];
    caissier_info_t caissiers[MAX_CAISSIERS];
    achat_info_t achats[MAX_CLIENTS];  /* Table des achats en attente */
    
    pid_t pid_initial;              /* PID du processus initial */
    int clients_termines;           /* Nombre de clients ayant termine */
    long chiffre_affaires;          /* Total des ventes encaissees (euros) */
} magasin_shm_t;

/* ============== STRUCTURES DE MESSAGES ============== */

/*
 * Conventions pour les mtype :
 *   - client -> vendeur : mtype = vendeur_id + 1
 *   - vendeur -> client : mtype = client_id + 1000
 *   - client -> caissier : mtype = caissier_id + 1
 *   - caissier -> client : mtype = client_id + 2000
 */

/*
 * Structure : msg_client_vendeur_t
 * Description : Message envoye par un client a un vendeur
 */
typedef struct {
    long mtype;             /* Type du message (vendeur_id + 1) */
    int client_id;          /* ID du client */
    int vendeur_id;         /* ID du vendeur cible */
    int rayon_voulu;        /* Rayon recherche par le client */
    int type_requete;       /* 0: demande rayon, 1: decision vente */
    int decision_achat;     /* 0: non, 1: oui (si type_requete=1) */
} msg_client_vendeur_t;

/*
 * Structure : msg_vendeur_client_t
 * Description : Message envoye par un vendeur a un client
 */
typedef struct {
    long mtype;             /* Type du message (client_id + 1000) */
    int vendeur_id;         /* ID du vendeur */
    int est_competent;      /* 1 si competent pour le rayon demande */
    int vendeur_recommande; /* Vendeur recommande si pas competent */
    int vente_terminee;     /* 1 quand la discussion est finie */
    int montant;            /* Montant de la vente */
} msg_vendeur_client_t;

/*
 * Structure : msg_client_caissier_t
 * Description : Message envoye par un client a un caissier
 */
typedef struct {
    long mtype;             /* Type du message (caissier_id + 1) */
    int client_id;          /* ID du client */
    int caissier_id;        /* ID du caissier cible */
} msg_client_caissier_t;

/*
 * Structure : msg_caissier_client_t
 * Description : Message envoye par un caissier a un client
 */
typedef struct {
    long mtype;             /* Type du message (client_id + 2000) */
    int montant;            /* Montant a payer */
    int paiement_termine;   /* 1 quand le paiement est effectue */
} msg_caissier_client_t;

/* ============== SEMAPHORES ============== */

/* Index des semaphores dans l'ensemble */
#define SEM_MUTEX_SHM       0   /* Mutex pour acces memoire partagee */
#define SEM_MUTEX_LOG       1   /* Mutex pour le fichier de log */
#define SEM_VENDEUR_BASE    2   /* Base pour les semaphores vendeurs */
/* Les semaphores caissiers suivent : SEM_VENDEUR_BASE + nb_vendeurs + i */

/* ============== VARIABLES GLOBALES ============== */

extern int shm_id;              /* ID de la memoire partagee */
extern int sem_id;              /* ID de l'ensemble de semaphores */
extern int msg_vendeur_id;      /* ID de la file de messages vendeurs */
extern int msg_caissier_id;     /* ID de la file de messages caissiers */
extern magasin_shm_t *shm;      /* Pointeur vers la memoire partagee */

/* ============== FONCTIONS ============== */

/*
 * Fonction : creer_ipc
 * Description : Cree toutes les IPC (appele par le processus initial)
 * Parametres : nb_vendeurs, nb_caissiers, nb_clients
 * Retour : 0 si succes, -1 si erreur
 */
int creer_ipc(int nb_vendeurs, int nb_caissiers, int nb_clients);

/*
 * Fonction : attacher_ipc
 * Description : Attache les IPC existantes (appele par les processus fils)
 * Retour : 0 si succes, -1 si erreur
 */
int attacher_ipc(void);

/*
 * Fonction : detacher_ipc
 * Description : Detache la memoire partagee
 */
void detacher_ipc(void);

/*
 * Fonction : detruire_ipc
 * Description : Detruit toutes les IPC (appele par le processus initial)
 */
void detruire_ipc(void);

/*
 * Fonction : sem_P
 * Description : Operation P (wait) sur un semaphore
 * Parametre : sem_index - index du semaphore
 * Retour : 0 si succes, -1 si erreur
 */
int sem_P(int sem_index);

/*
 * Fonction : sem_V
 * Description : Operation V (signal) sur un semaphore
 * Parametre : sem_index - index du semaphore
 * Retour : 0 si succes, -1 si erreur
 */
int sem_V(int sem_index);

/*
 * Fonction : sem_wait_zero
 * Description : Attend que le semaphore soit a 0
 * Parametre : sem_index - index du semaphore
 * Retour : 0 si succes, -1 si erreur
 */
int sem_wait_zero(int sem_index);

/*
 * Fonction : trouver_vendeur_moins_charge
 * Description : Trouve le vendeur avec la file d'attente la plus courte
 * Retour : Index du vendeur
 */
int trouver_vendeur_moins_charge(void);

/*
 * Fonction : trouver_caissier_moins_charge
 * Description : Trouve le caissier avec la file d'attente la plus courte
 * Retour : Index du caissier
 */
int trouver_caissier_moins_charge(void);

/*
 * Fonction : trouver_vendeur_pour_rayon
 * Description : Trouve un vendeur competent pour un rayon donne
 * Parametre : rayon - numero du rayon (0 a NB_RAYONS-1)
 * Retour : Index du vendeur
 */
int trouver_vendeur_pour_rayon(int rayon);

#endif /* IPC_H */
