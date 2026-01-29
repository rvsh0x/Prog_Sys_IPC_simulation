/*
 * =============================================================================
 * Fichier     : monitoring.c
 * Description : Processus de monitoring de la simulation du magasin de bricolage
 *
 * Interface temps reel : colonnes a largeur fixe, couleurs, chiffre d'affaires.
 *
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include "config.h"
#include "ipc.h"

DECLARE_NOMS_RAYONS;

#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_DIM     "\033[2m"
#define C_RED     "\033[1;91m"
#define C_GREEN   "\033[1;92m"
#define C_YELLOW  "\033[1;93m"
#define C_BLUE    "\033[1;94m"
#define C_MAGENTA "\033[1;95m"
#define C_CYAN    "\033[1;96m"

/* Largeur totale du cadre (visible) */
#define W 78

int continuer = 1;

void handler_term(int sig) { (void)sig; continuer = 0; }

void effacer_ecran(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

/* Trace une ligne du type  +----+----+  */
static void sep(char c) {
    putchar(' ');
    putchar('+');
    for (int i = 0; i < W - 2; i++) putchar(c);
    putchar('+');
    putchar('\n');
}

/* Affiche une ligne de texte centree entre |  */
static void ligne_centree(const char *s, const char *color) {
    int len = (int)strlen(s);
    int gauche = (W - 4 - len) / 2;
    int droite = W - 4 - len - gauche;
    printf("  |");
    for (int i = 0; i < gauche; i++) putchar(' ');
    if (color) printf("%s%s%s", color, s, C_RESET);
    else printf("%s", s);
    for (int i = 0; i < droite; i++) putchar(' ');
    printf("|\n");
}

void afficher_etat(void) {
    int i, j;
    char buf[128];

    effacer_ecran();

    /* ----- En-tete ----- */
    printf("\n  ");
    sep('=');
    ligne_centree("MAGASIN DE BRICOLAGE - MONITORING", C_BOLD C_BLUE);
    ligne_centree("Temps reel - Rafraichi toutes les 0,5 s", C_DIM);
    sep('=');

    /* ----- Bloc resume ----- */
    printf("  |" C_BOLD "  RESUME" C_RESET);
    for (int i = 8; i < W - 2; i++) putchar(' ');
    printf("|\n  |");
    for (int i = 0; i < W - 4; i++) putchar('-');
    printf("|\n");

    printf("  |  Etat:    ");
    printf(shm->simulation_active ? C_GREEN "ACTIF " C_RESET : C_RED "INACTIF" C_RESET);
    printf("  |  Clients:  %3d / %-3d  |  En cours: %3d",
           shm->clients_termines, shm->nb_clients,
           shm->nb_clients - shm->clients_termines);
    /* Padding fixe : resume ~63 car visibles */
    for (int i = 63; i < W - 2; i++) putchar(' ');
    printf("|\n");

    printf("  |  " C_BOLD C_MAGENTA "Chiffre d'affaires:" C_RESET "  " C_BOLD C_GREEN "%12ld EUR" C_RESET,
           (long)shm->chiffre_affaires);
    for (int i = 42; i < W - 2; i++) putchar(' ');
    printf("|\n");

    sep('=');

    /* ----- Tableau vendeurs ----- */
    printf("  |" C_BOLD C_CYAN "  VENDEURS" C_RESET);
    for (int i = 11; i < W - 2; i++) putchar(' ');
    printf("|\n  |");
    for (int i = 0; i < W - 4; i++) putchar('-');
    printf("|\n  | ");
    printf("%-5s %-18s %-8s %4s %-20s", "Id", "Rayon", "Etat", "File", "Clients");
    printf(" |\n  |");
    for (int i = 0; i < W - 4; i++) putchar('-');
    printf("|\n");

    for (i = 0; i < shm->nb_vendeurs; i++) {
        const char *etat = shm->vendeurs[i].occupe ? C_YELLOW "OCCUPE " C_RESET : C_GREEN "LIBRE  " C_RESET;
        int nq = shm->vendeurs[i].file_attente;
        buf[0] = '\0';
        if (nq > 0) {
            char *p = buf;
            for (j = 0; j < nq && j < 8 && p - buf < (int)sizeof(buf) - 8; j++) {
                p += snprintf(p, sizeof(buf) - (p - buf), "C%d ", shm->vendeurs[i].clients_queue[j]);
            }
            if (nq > 8) snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "...");
        } else {
            snprintf(buf, sizeof(buf), "-");
        }
        printf("  | V%02d  %-18.18s %s %3d %-20.20s |\n",
               i, NOMS_RAYONS[shm->vendeurs[i].rayon], etat, nq, buf);
    }

    sep('=');

    /* ----- Tableau caissiers ----- */
    printf("  |" C_BOLD C_CYAN "  CAISSIERS" C_RESET);
    for (int i = 12; i < W - 2; i++) putchar(' ');
    printf("|\n  |");
    for (int i = 0; i < W - 4; i++) putchar('-');
    printf("|\n  | ");
    printf("%-8s %-8s %4s %-20s", "Caisse", "Etat", "File", "Clients");
    printf("      |\n  |");
    for (int i = 0; i < W - 4; i++) putchar('-');
    printf("|\n");

    for (i = 0; i < shm->nb_caissiers; i++) {
        const char *etat = shm->caissiers[i].occupe ? C_YELLOW "OCCUPEE" C_RESET : C_GREEN "LIBRE  " C_RESET;
        int nq = shm->caissiers[i].file_attente;
        buf[0] = '\0';
        if (nq > 0) {
            char *p = buf;
            for (j = 0; j < nq && j < 8 && p - buf < (int)sizeof(buf) - 8; j++) {
                p += snprintf(p, sizeof(buf) - (p - buf), "C%d ", shm->caissiers[i].clients_queue[j]);
            }
            if (nq > 8) snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "...");
        } else {
            snprintf(buf, sizeof(buf), "-");
        }
        printf("  | %-7d %s %3d %-20.20s      |\n", i, etat, nq, buf);
    }

    sep('=');
    printf("  | " C_DIM "Ctrl+C pour quitter le monitoring" C_RESET);
    for (int i = 35; i < W - 2; i++) putchar(' ');
    printf("|\n");
    sep('=');
    printf("\n");
    fflush(stdout);
}

int main(void) {
    struct sigaction sa;

    printf("Monitoring magasin de bricolage - Connexion IPC...\n");

    if (attacher_ipc() != 0) {
        fprintf(stderr, "Erreur: impossible d'attacher les IPC (simulation en cours ?).\n");
        return EXIT_FAILURE;
    }

    printf(C_GREEN "OK." C_RESET " Demarrage dans 2 s...\n");
    sleep(2);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_term;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    while (continuer) {
        afficher_etat();
        usleep(500000);
        if (!shm->simulation_active && shm->clients_termines >= shm->nb_clients) {
            afficher_etat();
            printf("\n" C_YELLOW "Simulation terminee." C_RESET "\n");
            break;
        }
    }

    printf("Arret du monitoring.\n");
    detacher_ipc();
    return EXIT_SUCCESS;
}
