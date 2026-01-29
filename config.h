/*
 * =============================================================================
 * Fichier     : config.h
 * Description : Fichier de configuration de la simulation du magasin
 * 
 * Ce fichier centralise tous les parametres modifiables de la simulation :
 *   - Nombre et noms des rayons
 *   - Limites maximales (vendeurs, caissiers, clients)
 *   - Temps d'attente (discussion, paiement)
 *   - Montants des achats
 *   - Probabilites (vente reussie)
 *   - Cles IPC et signaux
 * =============================================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

/* ============== RAYONS DU MAGASIN ============== */

/* Nombre total de rayons dans le magasin */
#define NB_RAYONS 10

/*
 * Noms des rayons (pour l'affichage)
 * Utilisation : ajouter DECLARE_NOMS_RAYONS; dans les fichiers .c qui en ont besoin
 */
#define DECLARE_NOMS_RAYONS \
    static const char* NOMS_RAYONS[] = { \
        "Peinture", \
        "Menuiserie", \
        "Plomberie/Chauffage", \
        "Eclairage/Luminaires", \
        "Revetement sols/murs", \
        "Jardin", \
        "Droguerie", \
        "Decoration", \
        "Outillage", \
        "Quincaillerie" \
    }

/* ============== LIMITES MAXIMALES ============== */

#define MAX_VENDEURS    50      /* Nombre max de vendeurs */
#define MAX_CAISSIERS   20      /* Nombre max de caissiers */
#define MAX_CLIENTS     500     /* Nombre max de clients */
#define MAX_QUEUE       20      /* Taille max d'une file d'attente */

/* ============== TEMPS D'ATTENTE (en secondes) ============== */

/* Duree de la discussion client-vendeur */
#define TEMPS_DISCUSSION_MIN    1
#define TEMPS_DISCUSSION_MAX    3

/* Duree du paiement chez le caissier */
#define TEMPS_PAIEMENT_MIN      1
#define TEMPS_PAIEMENT_MAX      2

/* Delai entre les actions (pour lisibilite des logs) */
#define DELAI_ACTION_MIN        1
#define DELAI_ACTION_MAX        2

/* ============== MONTANTS DES ACHATS (en euros) ============== */

#define MONTANT_ACHAT_MIN       10
#define MONTANT_ACHAT_MAX       500

/* ============== PROBABILITES (en pourcentage) ============== */

/* Probabilite qu'une vente se conclue avec succes */
#define PROBA_VENTE_REUSSIE     65

/* ============== FICHIER DE LOG ============== */

#define FICHIER_LOG             "magasin.log"

/* ============== CLES IPC ============== */

/* Fichier utilise par ftok() pour generer les cles IPC */
#define IPC_KEY_FILE            "/tmp/magasin_ipc"

/* Identifiants pour ftok() */
#define SHM_KEY_ID              1   /* Memoire partagee */
#define SEM_KEY_ID              2   /* Semaphores */
#define MSG_VENDEUR_KEY_ID      3   /* File de messages vendeurs */
#define MSG_CAISSIER_KEY_ID     4   /* File de messages caissiers */

/* ============== SIGNAUX ============== */

#define SIGNAL_START            SIGUSR1   /* Signal de demarrage */
#define SIGNAL_STOP             SIGUSR2   /* Signal d'arret */

#endif /* CONFIG_H */
