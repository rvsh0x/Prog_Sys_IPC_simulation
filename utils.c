/*
 * =============================================================================
 * Fichier     : utils.c
 * Description : Implementation des fonctions utilitaires
 * 
 * Fonctions :
 *   - init_random()       : Initialise le generateur aleatoire
 *   - tirage_aleatoire()  : Genere un nombre dans un intervalle
 *   - attente_aleatoire() : Attend un temps aleatoire
 *   - probabilite()       : Decide selon une probabilite
 * 
 * =============================================================================
 */

#include "utils.h"
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/*
 * -----------------------------------------------------------------------------
 * Fonction    : init_random
 * Description : Initialise le generateur de nombres aleatoires avec une graine
 *               unique basee sur le temps, le PID et un seed fourni
 * Parametre   : seed - graine supplementaire
 * -----------------------------------------------------------------------------
 */
void init_random(int seed) {
    srand(time(NULL) + seed + getpid());
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : tirage_aleatoire
 * Description : Genere un nombre aleatoire dans l'intervalle [min, max]
 * Parametres  :
 *   - min : borne inferieure
 *   - max : borne superieure
 * Retour      : Valeur aleatoire entre min et max (inclus)
 * -----------------------------------------------------------------------------
 */
int tirage_aleatoire(int min, int max) {
    /* Si min >= max, retourner min */
    if (min >= max) {
        return min;
    }
    return min + rand() % (max - min + 1);
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : attente_aleatoire
 * Description : Fait une pause d'une duree aleatoire entre min et max secondes
 * Parametres  :
 *   - min : duree minimale (secondes)
 *   - max : duree maximale (secondes)
 * -----------------------------------------------------------------------------
 */
void attente_aleatoire(int min, int max) {
    int duree = tirage_aleatoire(min, max);
    sleep(duree);
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : probabilite
 * Description : Determine si un evenement se produit selon une probabilite
 * Parametre   : pourcentage - probabilite de succes (0 a 100)
 * Retour      : 1 si l'evenement se produit, 0 sinon
 * Exemple     : probabilite(65) retourne 1 dans 65% des cas
 * -----------------------------------------------------------------------------
 */
int probabilite(int pourcentage) {
    /* Cas limites */
    if (pourcentage >= 100) {
        return 1;
    }
    if (pourcentage <= 0) {
        return 0;
    }
    
    /* Tirer un nombre entre 0 et 99, comparer au seuil */
    return (rand() % 100) < pourcentage;
}
