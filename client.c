/*
 * =============================================================================
 * Fichier     : client.c
 * Description : Processus client de la simulation du magasin de bricolage
 * 
 * Chaque client :
 *   1. Choisit un rayon aleatoirement
 *   2. Va vers le vendeur le moins charge
 *   3. Est redirige si le vendeur n'est pas competent
 *   4. Discute avec un vendeur competent
 *   5. Decide d'acheter ou non (probabilite 65%)
 *   6. Si achat : va en caisse, paie, et quitte
 *   7. Si pas d'achat : quitte directement
 * 
 * Fonctions :
 *   - gestionnaireSignal()     : Gestionnaire pour arret propre
 *   - ajouter_queue_vendeur()  : Ajoute le client dans une queue vendeur
 *   - retirer_queue_vendeur()  : Retire le client d'une queue vendeur
 *   - contacter_vendeur()      : Gere l'interaction avec un vendeur
 *   - main()                   : Parcours complet du client
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "ipc.h"
#include "log.h"
#include "utils.h"

/* Declaration des noms de rayons */
DECLARE_NOMS_RAYONS;

/* ============== VARIABLES GLOBALES ============== */

int client_id;  /* ID de ce client */

/*
 * -----------------------------------------------------------------------------
 * Fonction    : gestionnaireSignal
 * Description : Gestionnaire de signaux pour arret propre du client
 * Parametre   : sig - numero du signal (ignore)
 * -----------------------------------------------------------------------------
 */
void gestionnaireSignal(int sig) {
    (void)sig;
    shmdt(shm);
    exit(EXIT_SUCCESS);
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : ajouter_queue_vendeur
 * Description : Ajoute ce client dans la file d'attente d'un vendeur
 * Parametre   : vendeur_idx - index du vendeur
 * -----------------------------------------------------------------------------
 */
void ajouter_queue_vendeur(int vendeur_idx) {
    sem_P(SEM_MUTEX_SHM);
    int n = shm->vendeurs[vendeur_idx].file_attente;
    if (n < MAX_QUEUE) {
        shm->vendeurs[vendeur_idx].clients_queue[n] = client_id;
        shm->vendeurs[vendeur_idx].file_attente++;
    }
    sem_V(SEM_MUTEX_SHM);
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : retirer_queue_vendeur
 * Description : Retire ce client de la file d'attente d'un vendeur
 * Parametre   : vendeur_idx - index du vendeur
 * -----------------------------------------------------------------------------
 */
void retirer_queue_vendeur(int vendeur_idx) {
    int i, j;
    
    sem_P(SEM_MUTEX_SHM);
    int n = shm->vendeurs[vendeur_idx].file_attente;
    
    /* Chercher le client dans la queue */
    for (i = 0; i < n; i++) {
        if (shm->vendeurs[vendeur_idx].clients_queue[i] == client_id) {
            /* Decaler les clients suivants */
            for (j = i; j < n - 1; j++) {
                shm->vendeurs[vendeur_idx].clients_queue[j] = 
                    shm->vendeurs[vendeur_idx].clients_queue[j + 1];
            }
            shm->vendeurs[vendeur_idx].file_attente--;
            break;
        }
    }
    sem_V(SEM_MUTEX_SHM);
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : contacter_vendeur
 * Description : Gere l'interaction complete avec un vendeur
 *               - S'ajoute a la queue
 *               - Envoie une demande
 *               - Attend la reponse
 *               - Gere la redirection si necessaire (appel recursif)
 * Parametres  :
 *   - vendeur_idx : index du vendeur a contacter
 *   - rayon_voulu : numero du rayon recherche
 *   - auteur      : chaine pour le logging
 * Retour      : Index du vendeur final (competent) ou -1 si erreur
 * -----------------------------------------------------------------------------
 */
int contacter_vendeur(int vendeur_idx, int rayon_voulu, char *auteur) {
    msg_client_vendeur_t msg;
    msg_vendeur_client_t reponse;
    
    /* S'ajouter a la file d'attente du vendeur */
    ajouter_queue_vendeur(vendeur_idx);
    
    log_message(auteur, "Va vers vendeur %d", vendeur_idx);
    
    /* Preparer et envoyer la demande */
    msg.mtype = vendeur_idx + 1;
    msg.client_id = client_id;
    msg.vendeur_id = vendeur_idx;
    msg.rayon_voulu = rayon_voulu;
    msg.type_requete = 0;  /* Demande de rayon */
    msg.decision_achat = 0;
    
    msgsnd(msg_vendeur_id, &msg, sizeof(msg) - sizeof(long), 0);
    
    /* Attendre la reponse du vendeur */
    while (shm->simulation_active) {
        ssize_t ret = msgrcv(msg_vendeur_id, &reponse, sizeof(reponse) - sizeof(long), client_id + 1000, IPC_NOWAIT);
                            
        if (ret == -1) {
            usleep(50000);
            continue;
        }
        
        if (!reponse.est_competent) {
            /* Vendeur pas competent : redirection */
            retirer_queue_vendeur(vendeur_idx);
            int nouveau = reponse.vendeur_recommande;
            log_message(auteur, "Redirection vers vendeur %d", nouveau);
            
            /* Appel recursif vers le nouveau vendeur */
            return contacter_vendeur(nouveau, rayon_voulu, auteur);
        }
        
        if (!reponse.vente_terminee) {
            /* Discussion en cours */
            log_message(auteur, "Discussion en cours...");
            continue;
        }
        
        /* Discussion terminee avec un vendeur competent */
        log_message(auteur, "Discussion terminee");
        return vendeur_idx;
    }
    
    return -1;  /* Simulation arretee */
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : main
 * Description : Point d'entree du processus client
 *               Simule le parcours complet du client dans le magasin
 * Parametre   : argv[1] = ID du client
 * Retour      : EXIT_SUCCESS
 * -----------------------------------------------------------------------------
 */
 
int main(int argc, char *argv[]) {
    char auteur[32];
    int rayon_voulu;
    int i;
    
    /* Verifier les arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <id>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    client_id = atoi(argv[1]);
    snprintf(auteur, sizeof(auteur), "CLIENT %d", client_id);
    
    /* Initialiser le generateur aleatoire */
    srand(time(NULL) + client_id + 200);
    log_init();
    
    /* Attacher les IPC */
    if (attacher_ipc() == -1) {
        log_erreur(auteur, "Impossible d'attacher les IPC");
        exit(EXIT_FAILURE);
    }
    
    /* Installer les gestionnaires de signaux */
    for (i = 1; i < 20; i++) {
        signal(i, gestionnaireSignal);
    }
    
    /* ===== ETAPE 1 : CHOIX DU RAYON ===== */
    rayon_voulu = rand() % NB_RAYONS;
    log_message(auteur, "Entre, cherche rayon: %s", NOMS_RAYONS[rayon_voulu]);
    
    /* ===== ETAPE 2 : CHOIX DU VENDEUR ===== */
    sem_P(SEM_MUTEX_SHM);
    int vendeur = trouver_vendeur_moins_charge();
    sem_V(SEM_MUTEX_SHM);
    
    log_message(auteur, "Choisit vendeur %d", vendeur);
    
    /* ===== ETAPE 3 : INTERACTION AVEC LE VENDEUR ===== */
    int vendeur_final = contacter_vendeur(vendeur, rayon_voulu, auteur);
    
    if (vendeur_final < 0 || !shm->simulation_active) {
        log_message(auteur, "Quitte le magasin");
        shmdt(shm);
        log_close();
        return EXIT_SUCCESS;
    }
    
    /* ===== ETAPE 4 : DECISION D'ACHAT ===== */
    /* Probabilite de 65% que la vente se conclue */
    int decision = probabilite(PROBA_VENTE_REUSSIE);
    
    /* Envoyer la decision au vendeur */
    msg_client_vendeur_t msg_decision;
    msg_decision.mtype = vendeur_final + 1;
    msg_decision.client_id = client_id;
    msg_decision.vendeur_id = vendeur_final;
    msg_decision.rayon_voulu = rayon_voulu;
    msg_decision.type_requete = 1;  /* Decision de vente */
    msg_decision.decision_achat = decision;
    
    msgsnd(msg_vendeur_id, &msg_decision, sizeof(msg_decision) - sizeof(long), 0);
    
    if (!decision) {
        /* Pas d'achat : quitter le magasin */
        retirer_queue_vendeur(vendeur_final);
        log_message(auteur, "N'achete pas, quitte le magasin");
        shmdt(shm);
        log_close();
        return EXIT_SUCCESS;
    }
    
    log_message(auteur, "Decide d'acheter!");
    
    /* Quitter la queue du vendeur */
    retirer_queue_vendeur(vendeur_final);
    
    /* ===== ETAPE 5 : PASSAGE EN CAISSE ===== */
    sleep(1);  /* Temps pour aller a la caisse */
    
    /* Choisir le caissier le moins charge */
    sem_P(SEM_MUTEX_SHM);
    int caissier = trouver_caissier_moins_charge();
    int nc = shm->caissiers[caissier].file_attente;
    if (nc < MAX_QUEUE) {
        shm->caissiers[caissier].clients_queue[nc] = client_id;
        shm->caissiers[caissier].file_attente++;
    }
    sem_V(SEM_MUTEX_SHM);
    
    log_message(auteur, "Va vers caissier %d", caissier);
    
    /* Envoyer une demande au caissier */
    msg_client_caissier_t msg_caisse;
    msg_caisse.mtype = caissier + 1;
    msg_caisse.client_id = client_id;
    msg_caisse.caissier_id = caissier;
    
    msgsnd(msg_caissier_id, &msg_caisse, sizeof(msg_caisse) - sizeof(long), 0);
    
    /* ===== ETAPE 6 : PAIEMENT ===== */
    msg_caissier_client_t reponse_caisse;
    int montant = 0;
    
    while (shm->simulation_active) {
        ssize_t ret = msgrcv(msg_caissier_id, &reponse_caisse,
                            sizeof(reponse_caisse) - sizeof(long),
                            client_id + 2000, IPC_NOWAIT);
                            
        if (ret == -1) {
            usleep(50000);
            continue;
        }
        
        if (!reponse_caisse.paiement_termine) {
            /* Le caissier annonce le prix */
            montant = reponse_caisse.montant;
            log_message(auteur, "Doit payer %d euros", montant);
            continue;
        }
        
        /* Paiement termine */
        log_message(auteur, "Paiement effectue!");
        break;
    }
    
    /* ===== ETAPE 7 : SORTIE DU MAGASIN ===== */
    log_message(auteur, "Quitte avec ses achats. Au revoir!");
    
    /* Incrementer le compteur de clients termines */
    sem_P(SEM_MUTEX_SHM);
    shm->clients_termines++;
    sem_V(SEM_MUTEX_SHM);
    
    shmdt(shm);
    log_close();
    
    return EXIT_SUCCESS;
}
