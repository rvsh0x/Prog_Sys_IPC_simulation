/*
 * =============================================================================
 * Fichier     : log.c
 * Description : Implementation du systeme de logging pour la simulation
 * 
 * Ce module gere l'ecriture des logs dans :
 *   - Le terminal (stdout pour les messages, stderr pour les erreurs)
 *   - Un fichier de log (magasin.log)
 * 
 * Les acces sont proteges par un semaphore mutex pour eviter les melanges
 * de messages entre processus.
 * 
 * =============================================================================
 */

#include "log.h"
#include "ipc.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>

/* Fichier de log (partage entre tous les processus) */
static FILE *log_file = NULL;

/*
 * -----------------------------------------------------------------------------
 * Fonction    : log_init
 * Description : Ouvre le fichier de log en mode ajout
 * Retour      : 0 si succes, -1 si erreur
 * -----------------------------------------------------------------------------
 */
int log_init(void) {
    log_file = fopen(FICHIER_LOG, "a");
    if (log_file == NULL) {
        perror("[ERREUR] Ouverture fichier de log");
        return -1;
    }
    return 0;
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : log_close
 * Description : Ferme le fichier de log
 * -----------------------------------------------------------------------------
 */
void log_close(void) {
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : get_timestamp
 * Description : Obtient l'horodatage actuel au format HH:MM:SS
 * Parametres  :
 *   - buffer : buffer pour stocker le resultat
 *   - size   : taille du buffer
 * -----------------------------------------------------------------------------
 */
static void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%H:%M:%S", tm_info);
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : log_message
 * Description : Ecrit un message de log avec horodatage
 *               Le message est ecrit dans stdout ET dans le fichier de log
 *               L'acces est protege par un mutex (semaphore)
 * Parametres  :
 *   - auteur : identifiant de l'auteur du message
 *   - format : chaine de format (printf-like)
 *   - ...    : arguments variables
 * -----------------------------------------------------------------------------
 */
void log_message(const char *auteur, const char *format, ...) {
    char timestamp[16];
    char message[512];
    va_list args;
    
    /* Formater le message utilisateur */
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    /* Obtenir l'horodatage actuel */
    get_timestamp(timestamp, sizeof(timestamp));
    
    /* Prendre le mutex pour eviter les melanges de messages */
    if (sem_id != -1) {
        sem_P(SEM_MUTEX_LOG);
    }
    
    /* Ecrire dans le terminal */
    printf("[%s][%s] %s\n", timestamp, auteur, message);
    fflush(stdout);
    
    /* Ecrire dans le fichier de log */
    if (log_file != NULL) {
        fprintf(log_file, "[%s][%s] %s\n", timestamp, auteur, message);
        fflush(log_file);
    }
    
    /* Liberer le mutex */
    if (sem_id != -1) {
        sem_V(SEM_MUTEX_LOG);
    }
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : log_erreur
 * Description : Ecrit un message d'erreur avec horodatage
 *               Le message est ecrit dans stderr ET dans le fichier de log
 * Parametres  : identiques a log_message
 * -----------------------------------------------------------------------------
 */
void log_erreur(const char *auteur, const char *format, ...) {
    char timestamp[16];
    char message[512];
    va_list args;
    
    /* Formater le message utilisateur */
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    /* Obtenir l'horodatage */
    get_timestamp(timestamp, sizeof(timestamp));
    
    /* Prendre le mutex */
    if (sem_id != -1) {
        sem_P(SEM_MUTEX_LOG);
    }
    
    /* Ecrire sur stderr */
    fprintf(stderr, "[%s][%s][ERREUR] %s\n", timestamp, auteur, message);
    fflush(stderr);
    
    /* Ecrire dans le fichier de log */
    if (log_file != NULL) {
        fprintf(log_file, "[%s][%s][ERREUR] %s\n", timestamp, auteur, message);
        fflush(log_file);
    }
    
    /* Liberer le mutex */
    if (sem_id != -1) {
        sem_V(SEM_MUTEX_LOG);
    }
}
