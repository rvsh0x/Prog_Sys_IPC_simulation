/*
 * =============================================================================
 * Fichier     : vendeur.c
 * Description : Processus vendeur de la simulation du magasin de bricolage
 * 
 * Chaque vendeur :
 *   - Est expert d'un rayon specifique
 *   - Attend les demandes des clients via une file de messages
 *   - Redirige les clients vers un vendeur competent si necessaire
 *   - Engage une discussion avec le client si competent
 *   - Enregistre la vente en memoire partagee pour le caissier
 * 
 * Fonctions :
 *   - gestionnaireSignal() : Gestionnaire pour arret propre
 *   - main()               : Boucle principale du vendeur
 * 
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

int vendeur_id;     /* ID de ce vendeur */
int continuer = 1;  /* Flag pour la boucle principale */

/*
 * -----------------------------------------------------------------------------
 * Fonction    : gestionnaireSignal
 * Description : Gestionnaire de signaux pour arret propre du vendeur
 * Parametre   : sig - numero du signal (ignore)
 * -----------------------------------------------------------------------------
 */
void gestionnaireSignal(int sig) {
    (void)sig;
    continuer = 0;
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : main
 * Description : Point d'entree du processus vendeur
 *               1. Attache les IPC
 *               2. Boucle principale : attente et traitement des clients
 *               3. Termine proprement sur signal
 * Parametre   : argv[1] = ID du vendeur
 * Retour      : EXIT_SUCCESS
 * -----------------------------------------------------------------------------
 */
int main(int argc, char *argv[]) {
    msg_client_vendeur_t msg_client;
    msg_vendeur_client_t msg_reponse;
    char auteur[32];
    int mon_rayon;
    int i;
    
    /* Verifier les arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <id>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    vendeur_id = atoi(argv[1]);
    snprintf(auteur, sizeof(auteur), "VENDEUR %d", vendeur_id);
    
    /* Initialiser le generateur aleatoire */
    srand(time(NULL) + vendeur_id);
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
    
    /* Recuperer mon rayon d'expertise */
    mon_rayon = shm->vendeurs[vendeur_id].rayon;
    log_message(auteur, "Pret. Rayon: %s (%d)", NOMS_RAYONS[mon_rayon], mon_rayon);
    
    /* ===== BOUCLE PRINCIPALE ===== */
    while (continuer && shm->simulation_active) {
        
        /* Attendre un message d'un client (non bloquant) */
        ssize_t ret = msgrcv(msg_vendeur_id, &msg_client,
                            sizeof(msg_client) - sizeof(long),
                            vendeur_id + 1, IPC_NOWAIT);
        
        if (ret == -1) {
            /* Pas de message, attendre un peu */
            usleep(100000);
            continue;
        }
        
        /* Message recu : extraire les informations */
        int client_id = msg_client.client_id;
        int rayon_voulu = msg_client.rayon_voulu;
        
        log_message(auteur, "Client %d demande rayon %s", 
                    client_id, NOMS_RAYONS[rayon_voulu]);
        
        /* Marquer le vendeur comme occupe */
        sem_P(SEM_MUTEX_SHM);
        shm->vendeurs[vendeur_id].occupe = 1;
        sem_V(SEM_MUTEX_SHM);
        
        /* Preparer la reponse */
        msg_reponse.mtype = client_id + 1000;
        msg_reponse.vendeur_id = vendeur_id;
        
        if (mon_rayon != rayon_voulu) {
            /* ===== CAS 1 : PAS COMPETENT ===== */
            /* Trouver un vendeur competent et rediriger le client */
            
            int vendeur_ok = trouver_vendeur_pour_rayon(rayon_voulu);
            log_message(auteur, "Redirection vers vendeur %d", vendeur_ok);
            
            msg_reponse.est_competent = 0;
            msg_reponse.vendeur_recommande = vendeur_ok;
            msg_reponse.vente_terminee = 0;
            
            /* Envoyer la reponse de redirection */
            msgsnd(msg_vendeur_id, &msg_reponse, 
                   sizeof(msg_reponse) - sizeof(long), 0);
            
            /* Liberer le vendeur */
            sem_P(SEM_MUTEX_SHM);
            shm->vendeurs[vendeur_id].occupe = 0;
            sem_V(SEM_MUTEX_SHM);
            
        } else {
            /* ===== CAS 2 : COMPETENT ===== */
            /* Engager la discussion avec le client */
            
            log_message(auteur, "Discussion avec client %d", client_id);
            
            /* Envoyer confirmation de competence */
            msg_reponse.est_competent = 1;
            msg_reponse.vendeur_recommande = -1;
            msg_reponse.vente_terminee = 0;
            msgsnd(msg_vendeur_id, &msg_reponse, 
                   sizeof(msg_reponse) - sizeof(long), 0);
            
            /* Simuler le temps de discussion */
            int temps = tirage_aleatoire(TEMPS_DISCUSSION_MIN, TEMPS_DISCUSSION_MAX);
            sleep(temps);
            
            /* Signaler la fin de la discussion */
            msg_reponse.vente_terminee = 1;
            msgsnd(msg_vendeur_id, &msg_reponse, 
                   sizeof(msg_reponse) - sizeof(long), 0);
            
            /* Attendre la decision du client */
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
                
                /* Verifier si c'est une decision (type_requete = 1) */
                if (decision.type_requete == 1) {
                    recu = 1;
                } else {
                    /* Ce n'est pas une decision, remettre le message */
                    msgsnd(msg_vendeur_id, &decision, 
                           sizeof(decision) - sizeof(long), 0);
                }
            }
            
            if (recu && decision.decision_achat) {
                /* Le client achete : generer le montant et l'enregistrer */
                int montant = tirage_aleatoire(MONTANT_ACHAT_MIN, MONTANT_ACHAT_MAX);
                log_message(auteur, "Vente conclue: %d euros", montant);
                
                /* Enregistrer l'achat en memoire partagee pour le caissier */
                sem_P(SEM_MUTEX_SHM);
                shm->achats[client_id].client_id = client_id;
                shm->achats[client_id].montant = montant;
                shm->achats[client_id].valide = 1;
                sem_V(SEM_MUTEX_SHM);
                
            } else if (recu) {
                log_message(auteur, "Client %d n'achete pas", client_id);
            }
            
            /* Liberer le vendeur */
            sem_P(SEM_MUTEX_SHM);
            shm->vendeurs[vendeur_id].occupe = 0;
            sem_V(SEM_MUTEX_SHM);
        }
    }
    
    /* ===== TERMINAISON ===== */
    log_message(auteur, "Fin du service");
    shmdt(shm);
    log_close();
    
    return EXIT_SUCCESS;
}
