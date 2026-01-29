/*
 * =============================================================================
 * Fichier     : log.h
 * Description : Interface du systeme de logging pour la simulation
 * 
 * Ce module fournit des fonctions pour :
 *   - Ecrire des messages de log dans le terminal ET dans un fichier
 *   - Ajouter automatiquement un horodatage [HH:MM:SS]
 *   - Proteger les acces concurrents avec un mutex (semaphore)
 * 
 * Format des logs : [HH:MM:SS][AUTEUR] Message
 * 
 * =============================================================================
 */

#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

/*
 * Fonction : log_init
 * Description : Initialise le systeme de log (ouvre le fichier)
 * Retour : 0 si succes, -1 si erreur
 */
int log_init(void);

/*
 * Fonction : log_close
 * Description : Ferme le fichier de log
 */
void log_close(void);

/*
 * Fonction : log_message
 * Description : Ecrit un message de log formate
 * Parametres :
 *   - auteur : identifiant de l'auteur (ex: "CLIENT 1", "VENDEUR 0")
 *   - format : chaine de format (comme printf)
 *   - ... : arguments variables
 */
void log_message(const char *auteur, const char *format, ...);

/*
 * Fonction : log_erreur
 * Description : Ecrit un message d'erreur (sur stderr + fichier)
 * Parametres : identiques a log_message
 */
void log_erreur(const char *auteur, const char *format, ...);

#endif /* LOG_H */
