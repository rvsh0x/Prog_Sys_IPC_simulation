/*
 * =============================================================================
 * Fichier     : main.c
 * Description : Processus initial de la simulation du magasin 
 * 
 * Ce processus est responsable de :
 *   - Creer tous les IPC (memoire partagee, semaphores, files de messages)
 *   - Lancer les processus vendeurs, caissiers et clients
 *   - Attendre la fin de tous les clients
 *   - Terminer proprement les vendeurs et caissiers
 *   - Nettoyer les IPC a la fin
 * 
 * Fonctions :
 *   - gestionnaireSignal() : Gestionnaire pour arret propre sur signal
 *   - usage()              : Affiche l'aide en cas d'erreur d'arguments
 *   - main()               : Point d'entree principal
 * 
 * Usage : ./main <nb_vendeurs> <nb_caissiers> <nb_clients>
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "config.h"
#include "ipc.h"
#include "log.h"
#include "utils.h"

/* ============== VARIABLES GLOBALES ============== */

pid_t *pids_vendeurs;   /* Tableau des PIDs des vendeurs */
pid_t *pids_caissiers;  /* Tableau des PIDs des caissiers */
pid_t *pids_clients;    /* Tableau des PIDs des clients */
int nb_vendeurs_g;      /* Nombre de vendeurs (copie globale) */
int nb_caissiers_g;     /* Nombre de caissiers (copie globale) */
int nb_clients_g;       /* Nombre de clients (copie globale) */

/*
 * -----------------------------------------------------------------------------
 * Fonction    : gestionnaireSignal
 * Description : Gestionnaire de signaux pour terminer proprement la simulation
 *               Envoie SIGINT a tous les processus fils puis nettoie les IPC
 * Parametre   : sig - numero du signal recu
 * Retour      : Aucun (termine le programme)
 * -----------------------------------------------------------------------------
 */
void gestionnaireSignal(int sig) {
    int i;
    
    log_message("INITIAL", "Signal recu [%d], arret du programme...", sig);
    
    /* Envoyer SIGINT a tous les vendeurs */
    for (i = 0; i < nb_vendeurs_g; i++) {
        if (pids_vendeurs[i] > 0) {
            kill(pids_vendeurs[i], SIGINT);
        }
    }
    
    /* Envoyer SIGINT a tous les caissiers */
    for (i = 0; i < nb_caissiers_g; i++) {
        if (pids_caissiers[i] > 0) {
            kill(pids_caissiers[i], SIGINT);
        }
    }
    
    /* Attendre la terminaison de tous les enfants */
    while (wait(NULL) > 0);
    
    /* Nettoyer les IPC */
    detacher_ipc();
    detruire_ipc();
    
    /* Liberer la memoire */
    free(pids_vendeurs);
    free(pids_caissiers);
    free(pids_clients);
    
    log_message("INITIAL", "Nettoyage termine. Au revoir!");
    log_close();
    
    exit(EXIT_SUCCESS);
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : usage
 * Description : Affiche l'aide d'utilisation et termine le programme
 * Parametre   : prog - nom du programme (argv[0])
 * Retour      : Aucun (termine le programme)
 * -----------------------------------------------------------------------------
 */
void usage(char *prog) {
    fprintf(stderr, "Usage: %s <nb_vendeurs> <nb_caissiers> <nb_clients>\n", prog);
    fprintf(stderr, "  nb_vendeurs  >= %d (un par rayon)\n", NB_RAYONS);
    fprintf(stderr, "  nb_caissiers >= 1\n");
    fprintf(stderr, "  nb_clients   >= 1\n");
    exit(EXIT_FAILURE);
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : main
 * Description : Point d'entree principal du processus initial
 *               1. Parse les arguments
 *               2. Cree les IPC
 *               3. Lance les vendeurs, caissiers et clients
 *               4. Attend la fin des clients
 *               5. Arrete les vendeurs et caissiers
 *               6. Nettoie les IPC
 * Parametres  : argc, argv (nb_vendeurs, nb_caissiers, nb_clients)
 * Retour      : EXIT_SUCCESS ou EXIT_FAILURE
 * -----------------------------------------------------------------------------
 */
int main(int argc, char *argv[]) {
    int nb_vendeurs, nb_caissiers, nb_clients;
    int i;
    pid_t pid;
    char id_str[16];
    
    /* Initialiser le generateur aleatoire */
    srand(time(NULL));
    
    /* Verification des arguments */
    if (argc != 4) {
        usage(argv[0]);
    }
    
    nb_vendeurs = atoi(argv[1]);
    nb_caissiers = atoi(argv[2]);
    nb_clients = atoi(argv[3]);
    
    /* Verifier les contraintes */
    if (nb_vendeurs < NB_RAYONS || nb_caissiers < 1 || nb_clients < 1) {
        usage(argv[0]);
    }
    
    /* Sauvegarder les valeurs en global pour le gestionnaire de signaux */
    nb_vendeurs_g = nb_vendeurs;
    nb_caissiers_g = nb_caissiers;
    nb_clients_g = nb_clients;
    
    /* Initialiser le systeme de log */
    log_init();
    
    log_message("INITIAL", "=== Demarrage de la simulation ===");
    log_message("INITIAL", "Config: %d vendeurs, %d caissiers, %d clients", 
                nb_vendeurs, nb_caissiers, nb_clients);
    
    /* Creer les IPC (memoire partagee, semaphores, files de messages) */
    if (creer_ipc(nb_vendeurs, nb_caissiers, nb_clients) == -1) {
        log_erreur("INITIAL", "Impossible de creer les IPC");
        exit(EXIT_FAILURE);
    }
    
    log_message("INITIAL", "IPC crees (SHM, Semaphores, Files de messages)");
    
    /* Assigner les rayons aux vendeurs */
    /* Les 10 premiers vendeurs ont chacun un rayon different */
    /* Les vendeurs supplementaires ont un rayon aleatoire */
    for (i = 0; i < nb_vendeurs; i++) {
        shm->vendeurs[i].rayon = (i < NB_RAYONS) ? i : (rand() % NB_RAYONS);
        shm->vendeurs[i].file_attente = 0;
        shm->vendeurs[i].occupe = 0;
    }
    
    /* Allouer les tableaux de PIDs */
    pids_vendeurs = malloc(nb_vendeurs * sizeof(pid_t));
    pids_caissiers = malloc(nb_caissiers * sizeof(pid_t));
    pids_clients = malloc(nb_clients * sizeof(pid_t));
    
    /* Installer les gestionnaires de signaux pour arret propre */
    for (i = 1; i < 20; i++) {
        if (i != SIGCHLD && i != SIGKILL && i != SIGSTOP) {
            signal(i, gestionnaireSignal);
        }
    }
    
    /* Activer la simulation AVANT de creer les vendeurs */
    shm->simulation_active = 1;
    
    /* ===== CREATION DES VENDEURS ===== */
    log_message("INITIAL", "Creation des %d vendeurs...", nb_vendeurs);
    for (i = 0; i < nb_vendeurs; i++) {
        pid = fork();
        if (pid == -1) {
            perror("fork vendeur");
            gestionnaireSignal(SIGTERM);
        }
        if (pid == 0) {
            /* Processus enfant - devient un vendeur */
            snprintf(id_str, sizeof(id_str), "%d", i);
            execl("./vendeur", "vendeur", id_str, NULL);
            perror("execl vendeur");
            exit(EXIT_FAILURE);
        }
        pids_vendeurs[i] = pid;
        shm->vendeurs[i].pid = pid;
    }
    
    /* ===== CREATION DES CAISSIERS ===== */
    log_message("INITIAL", "Creation des %d caissiers...", nb_caissiers);
    for (i = 0; i < nb_caissiers; i++) {
        pid = fork();
        if (pid == -1) {
            perror("fork caissier");
            gestionnaireSignal(SIGTERM);
        }
        if (pid == 0) {
            /* Processus enfant - devient un caissier */
            snprintf(id_str, sizeof(id_str), "%d", i);
            execl("./caissier", "caissier", id_str, NULL);
            perror("execl caissier");
            exit(EXIT_FAILURE);
        }
        pids_caissiers[i] = pid;
        shm->caissiers[i].pid = pid;
    }
    
    /* ===== CREATION DES CLIENTS ===== */
    log_message("INITIAL", "Creation des %d clients...", nb_clients);
    for (i = 0; i < nb_clients; i++) {
        /* Delai aleatoire entre les clients pour simuler des arrivees echelonnees */
        /*sleep(rand() % 2);*/
        
        pid = fork();
        if (pid == -1) {
            perror("fork client");
            continue;
        }
        if (pid == 0) {
            /* Processus enfant - devient un client */
            snprintf(id_str, sizeof(id_str), "%d", i);
            execl("./client", "client", id_str, NULL);
            perror("execl client");
            exit(EXIT_FAILURE);
        }
        pids_clients[i] = pid;
    }
    
    log_message("INITIAL", "Tous les processus sont lances!");
    log_message("INITIAL", "En attente de la fin des clients...");
    
    /* ===== ATTENTE DES CLIENTS ===== */
    /* Attendre que tous les clients aient termine */
    for (i = 0; i < nb_clients; i++) {
        waitpid(pids_clients[i], NULL, 0);
        pids_clients[i] = 0;
    }
    
    log_message("INITIAL", "Tous les clients ont termine.");
    log_message("INITIAL", "Arret des vendeurs et caissiers...");
    
    /* ===== ARRET DE LA SIMULATION ===== */
    /* Desactiver la simulation */
    shm->simulation_active = 0;
    
    /* Envoyer SIGINT aux vendeurs pour les faire terminer */
    for (i = 0; i < nb_vendeurs; i++) {
        if (pids_vendeurs[i] > 0) {
            kill(pids_vendeurs[i], SIGINT);
        }
    }
    
    /* Envoyer SIGINT aux caissiers pour les faire terminer */
    for (i = 0; i < nb_caissiers; i++) {
        if (pids_caissiers[i] > 0) {
            kill(pids_caissiers[i], SIGINT);
        }
    }
    
    /* Attendre la terminaison des vendeurs */
    for (i = 0; i < nb_vendeurs; i++) {
        if (pids_vendeurs[i] > 0) {
            waitpid(pids_vendeurs[i], NULL, 0);
        }
    }
    
    /* Attendre la terminaison des caissiers */
    for (i = 0; i < nb_caissiers; i++) {
        if (pids_caissiers[i] > 0) {
            waitpid(pids_caissiers[i], NULL, 0);
        }
    }
    
    /* ===== NETTOYAGE ===== */
    log_message("INITIAL", "Nettoyage des IPC...");
    
    detacher_ipc();
    detruire_ipc();
    
    free(pids_vendeurs);
    free(pids_caissiers);
    free(pids_clients);
    
    log_message("INITIAL", "=== Simulation terminee ===");
    log_close();
    
    return EXIT_SUCCESS;
}
