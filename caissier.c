/*
 * =============================================================================
 * Fichier     : caissier.c
 * Description : Processus caissier de la simulation du magasin de bricolage
 * 
 * Chaque caissier :
 *   - Attend les clients via une file de messages
 *   - Recupere le montant de l'achat en memoire partagee
 *   - Annonce le prix au client
 *   - Simule le temps de paiement
 *   - Confirme la fin du paiement
 * 
 * Fonctions :
 *   - gestionnaireSignal() : Gestionnaire pour arret propre
 *   - main()               : Boucle principale du caissier
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

/* ============== VARIABLES GLOBALES ============== */

int caissier_id;    /* ID de ce caissier */
int continuer = 1;  /* Flag pour la boucle principale */

/*
 * -----------------------------------------------------------------------------
 * Fonction    : gestionnaireSignal
 * Description : Gestionnaire de signaux pour arret propre du caissier
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
 * Description : Point d'entree du processus caissier
 *               1. Attache les IPC
 *               2. Boucle principale : encaissement des clients
 *               3. Termine proprement sur signal
 * Parametre   : argv[1] = ID du caissier
 * Retour      : EXIT_SUCCESS
 * -----------------------------------------------------------------------------
 */
int main(int argc, char *argv[]) {
    msg_client_caissier_t msg_client;
    msg_caissier_client_t msg_reponse;
    char auteur[32];
    int i, j;
    
    /* Verifier les arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <id>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    caissier_id = atoi(argv[1]);
    snprintf(auteur, sizeof(auteur), "CAISSIER %d", caissier_id);
    
    /* Initialiser le generateur aleatoire */
    srand(time(NULL) + caissier_id + 100);
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
    
    log_message(auteur, "Caisse ouverte");
    
    /* ===== BOUCLE PRINCIPALE ===== */
    while (continuer && shm->simulation_active) {
        
        /* Attendre un client (non bloquant) */
        ssize_t ret = msgrcv(msg_caissier_id, &msg_client,
                            sizeof(msg_client) - sizeof(long),
                            caissier_id + 1, IPC_NOWAIT);
        
        if (ret == -1) {
            /* Pas de client, attendre un peu */
            usleep(100000);
            continue;
        }
        
        /* Client recu */
        int client_id = msg_client.client_id;
        log_message(auteur, "Client %d arrive", client_id);
        
        /* Marquer le caissier comme occupe */
        sem_P(SEM_MUTEX_SHM);
        shm->caissiers[caissier_id].occupe = 1;
        sem_V(SEM_MUTEX_SHM);
        
        /* Recuperer le montant de l'achat en memoire partagee */
        sem_P(SEM_MUTEX_SHM);
        int montant = 0;
        if (shm->achats[client_id].valide) {
            montant = shm->achats[client_id].montant;
            shm->achats[client_id].valide = 0;  /* Marquer comme traite */
        }
        sem_V(SEM_MUTEX_SHM);
        
        /* Si montant invalide, utiliser un montant par defaut */
        if (montant == 0) {
            montant = MONTANT_ACHAT_MIN;
        }
        
        log_message(auteur, "Client %d doit payer %d euros", client_id, montant);
        
        /* Envoyer le montant au client */
        msg_reponse.mtype = client_id + 2000;
        msg_reponse.montant = montant;
        msg_reponse.paiement_termine = 0;
        msgsnd(msg_caissier_id, &msg_reponse, 
               sizeof(msg_reponse) - sizeof(long), 0);
        
        /* Simuler le temps de paiement */
        int temps = tirage_aleatoire(TEMPS_PAIEMENT_MIN, TEMPS_PAIEMENT_MAX);
        sleep(temps);
        
        /* Signaler la fin du paiement */
        msg_reponse.paiement_termine = 1;
        msgsnd(msg_caissier_id, &msg_reponse, 
               sizeof(msg_reponse) - sizeof(long), 0);
        
        log_message(auteur, "Client %d a paye %d euros", client_id, montant);
        
        /* Retirer le client de la file d'attente et mettre a jour le CA */
        sem_P(SEM_MUTEX_SHM);
        shm->chiffre_affaires += montant;
        int n = shm->caissiers[caissier_id].file_attente;
        for (i = 0; i < n; i++) {
            if (shm->caissiers[caissier_id].clients_queue[i] == client_id) {
                /* Decaler les clients suivants */
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
    }
    
    /* ===== TERMINAISON ===== */
    log_message(auteur, "Caisse fermee");
    shmdt(shm);
    log_close();
    
    return EXIT_SUCCESS;
}
