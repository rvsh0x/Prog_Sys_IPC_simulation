/*
 * =============================================================================
 * Fichier     : utils.h
 * Description : Interface des fonctions utilitaires pour la simulation
 * 
 * Ce module fournit des fonctions pour :
 *   - Generer des nombres aleatoires dans un intervalle
 *   - Simuler des attentes aleatoires
 *   - Calculer des probabilites
 * 
 * =============================================================================
 */

#ifndef UTILS_H
#define UTILS_H

/*
 * Fonction : init_random
 * Description : Initialise le generateur de nombres aleatoires
 * Parametre : seed - graine de base (sera combinee avec le temps et le PID)
 */
void init_random(int seed);

/*
 * Fonction : tirage_aleatoire
 * Description : Tire un nombre aleatoire dans l'intervalle [min, max]
 * Parametres :
 *   - min : valeur minimale
 *   - max : valeur maximale
 * Retour : valeur aleatoire entre min et max inclus
 */
int tirage_aleatoire(int min, int max);

/*
 * Fonction : attente_aleatoire
 * Description : Attend un nombre aleatoire de secondes dans [min, max]
 * Parametres :
 *   - min : duree minimale en secondes
 *   - max : duree maximale en secondes
 */
void attente_aleatoire(int min, int max);

/*
 * Fonction : probabilite
 * Description : Decide si un evenement se produit avec une probabilite donnee
 * Parametre : pourcentage - probabilite de succes (0-100)
 * Retour : 1 si l'evenement se produit, 0 sinon
 */
int probabilite(int pourcentage);

#endif /* UTILS_H */
