/*
 * =============================================================================
 * Fichier     : ipc.c
 * Description : Implementation des fonctions IPC pour la simulation
 * 
 * Ce fichier implemente toutes les fonctions de gestion des IPC System V :
 *   - Creation et destruction de la memoire partagee
 *   - Creation et destruction des semaphores
 *   - Creation et destruction des files de messages
 *   - Operations P et V sur les semaphores
 *   - Fonctions utilitaires (recherche vendeur/caissier moins charge)
 * 
 * =============================================================================
 */

#include "ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* ============== VARIABLES GLOBALES ============== */

int shm_id = -1;            /* ID de la memoire partagee */
int sem_id = -1;            /* ID de l'ensemble de semaphores */
int msg_vendeur_id = -1;    /* ID de la file de messages vendeurs */
int msg_caissier_id = -1;   /* ID de la file de messages caissiers */
magasin_shm_t *shm = NULL;  /* Pointeur vers la memoire partagee */

/* Union pour semctl (necessaire sur certains systemes) */
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

/*
 * -----------------------------------------------------------------------------
 * Fonction    : creer_ipc
 * Description : Cree toutes les IPC necessaires a la simulation
 *               - Memoire partagee pour l'etat global
 *               - Semaphores pour la synchronisation
 *               - Files de messages pour la communication
 * Parametres  : nb_vendeurs, nb_caissiers, nb_clients
 * Retour      : 0 si succes, -1 si erreur
 * -----------------------------------------------------------------------------
 */
int creer_ipc(int nb_vendeurs, int nb_caissiers, int nb_clients) {
    key_t key_shm, key_sem, key_msg_v, key_msg_c;
    int nb_semaphores;
    union semun arg;
    int fd;
    int i;
    
    /* Creer le fichier pour ftok si necessaire */
    fd = open(IPC_KEY_FILE, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("[ERREUR] Creation fichier IPC");
        return -1;
    }
    close(fd);
    
    /* Generer les cles IPC avec ftok */
    key_shm = ftok(IPC_KEY_FILE, SHM_KEY_ID);
    key_sem = ftok(IPC_KEY_FILE, SEM_KEY_ID);
    key_msg_v = ftok(IPC_KEY_FILE, MSG_VENDEUR_KEY_ID);
    key_msg_c = ftok(IPC_KEY_FILE, MSG_CAISSIER_KEY_ID);
    
    if (key_shm == -1 || key_sem == -1 || key_msg_v == -1 || key_msg_c == -1) {
        perror("[ERREUR] ftok");
        return -1;
    }
    
    /* ===== MEMOIRE PARTAGEE ===== */
    
    /* Creer le segment de memoire partagee */
    shm_id = shmget(key_shm, sizeof(magasin_shm_t), IPC_CREAT | IPC_EXCL | 0666);
    if (shm_id == -1) {
        if (errno == EEXIST) {
            /* Le segment existe deja, le supprimer et recreer */
            shm_id = shmget(key_shm, sizeof(magasin_shm_t), 0666);
            if (shm_id != -1) {
                shmctl(shm_id, IPC_RMID, NULL);
            }
            shm_id = shmget(key_shm, sizeof(magasin_shm_t), IPC_CREAT | 0666);
        }
        if (shm_id == -1) {
            perror("[ERREUR] shmget");
            return -1;
        }
    }
    
    /* Attacher la memoire partagee */
    shm = (magasin_shm_t *)shmat(shm_id, NULL, 0);
    if (shm == (void *)-1) {
        perror("[ERREUR] shmat");
        return -1;
    }
    
    /* Initialiser la memoire partagee */
    memset(shm, 0, sizeof(magasin_shm_t));
    shm->nb_vendeurs = nb_vendeurs;
    shm->nb_caissiers = nb_caissiers;
    shm->nb_clients = nb_clients;
    shm->simulation_active = 0;
    shm->clients_termines = 0;
    shm->pid_initial = getpid();
    
    /* ===== SEMAPHORES ===== */
    
    /* Calculer le nombre de semaphores necessaires */
    /* mutex_shm + mutex_log + semaphores_vendeurs + semaphores_caissiers */
    nb_semaphores = SEM_VENDEUR_BASE + nb_vendeurs + nb_caissiers;
    
    /* Creer l'ensemble de semaphores */
    sem_id = semget(key_sem, nb_semaphores, IPC_CREAT | IPC_EXCL | 0666);
    if (sem_id == -1) {
        if (errno == EEXIST) {
            /* Supprimer et recreer */
            sem_id = semget(key_sem, nb_semaphores, 0666);
            if (sem_id != -1) {
                semctl(sem_id, 0, IPC_RMID);
            }
            sem_id = semget(key_sem, nb_semaphores, IPC_CREAT | 0666);
        }
        if (sem_id == -1) {
            perror("[ERREUR] semget");
            return -1;
        }
    }
    
    /* Initialiser les valeurs des semaphores */
    unsigned short *values = malloc(nb_semaphores * sizeof(unsigned short));
    if (!values) {
        perror("[ERREUR] malloc");
        return -1;
    }
    
    values[SEM_MUTEX_SHM] = 1;  /* Mutex memoire partagee : disponible */
    values[SEM_MUTEX_LOG] = 1;  /* Mutex log : disponible */
    
    /* Semaphores vendeurs (initialises a 0) */
    for (i = 0; i < nb_vendeurs; i++) {
        values[SEM_VENDEUR_BASE + i] = 0;
    }
    
    /* Semaphores caissiers (initialises a 0) */
    for (i = 0; i < nb_caissiers; i++) {
        values[SEM_VENDEUR_BASE + nb_vendeurs + i] = 0;
    }
    
    arg.array = values;
    if (semctl(sem_id, 0, SETALL, arg) == -1) {
        perror("[ERREUR] semctl SETALL");
        free(values);
        return -1;
    }
    free(values);
    
    /* ===== FILES DE MESSAGES ===== */
    
    /* File de messages pour les vendeurs */
    msg_vendeur_id = msgget(key_msg_v, IPC_CREAT | IPC_EXCL | 0666);
    if (msg_vendeur_id == -1) {
        if (errno == EEXIST) {
            msg_vendeur_id = msgget(key_msg_v, 0666);
            if (msg_vendeur_id != -1) {
                msgctl(msg_vendeur_id, IPC_RMID, NULL);
            }
            msg_vendeur_id = msgget(key_msg_v, IPC_CREAT | 0666);
        }
        if (msg_vendeur_id == -1) {
            perror("[ERREUR] msgget vendeur");
            return -1;
        }
    }
    
    /* File de messages pour les caissiers */
    msg_caissier_id = msgget(key_msg_c, IPC_CREAT | IPC_EXCL | 0666);
    if (msg_caissier_id == -1) {
        if (errno == EEXIST) {
            msg_caissier_id = msgget(key_msg_c, 0666);
            if (msg_caissier_id != -1) {
                msgctl(msg_caissier_id, IPC_RMID, NULL);
            }
            msg_caissier_id = msgget(key_msg_c, IPC_CREAT | 0666);
        }
        if (msg_caissier_id == -1) {
            perror("[ERREUR] msgget caissier");
            return -1;
        }
    }
    
    return 0;
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : attacher_ipc
 * Description : Attache les IPC existantes (appele par les processus fils
 *               apres fork/exec)
 * Retour      : 0 si succes, -1 si erreur
 * -----------------------------------------------------------------------------
 */
int attacher_ipc(void) {
    key_t key_shm, key_sem, key_msg_v, key_msg_c;
    
    /* Generer les cles IPC */
    key_shm = ftok(IPC_KEY_FILE, SHM_KEY_ID);
    key_sem = ftok(IPC_KEY_FILE, SEM_KEY_ID);
    key_msg_v = ftok(IPC_KEY_FILE, MSG_VENDEUR_KEY_ID);
    key_msg_c = ftok(IPC_KEY_FILE, MSG_CAISSIER_KEY_ID);
    
    if (key_shm == -1 || key_sem == -1 || key_msg_v == -1 || key_msg_c == -1) {
        perror("[ERREUR] ftok dans attacher_ipc");
        return -1;
    }
    
    /* Recuperer la memoire partagee */
    shm_id = shmget(key_shm, sizeof(magasin_shm_t), 0666);
    if (shm_id == -1) {
        perror("[ERREUR] shmget dans attacher_ipc");
        return -1;
    }
    
    /* Attacher la memoire partagee */
    shm = (magasin_shm_t *)shmat(shm_id, NULL, 0);
    if (shm == (void *)-1) {
        perror("[ERREUR] shmat dans attacher_ipc");
        return -1;
    }
    
    /* Recuperer les semaphores */
    sem_id = semget(key_sem, 0, 0666);
    if (sem_id == -1) {
        perror("[ERREUR] semget dans attacher_ipc");
        return -1;
    }
    
    /* Recuperer les files de messages */
    msg_vendeur_id = msgget(key_msg_v, 0666);
    if (msg_vendeur_id == -1) {
        perror("[ERREUR] msgget vendeur dans attacher_ipc");
        return -1;
    }
    
    msg_caissier_id = msgget(key_msg_c, 0666);
    if (msg_caissier_id == -1) {
        perror("[ERREUR] msgget caissier dans attacher_ipc");
        return -1;
    }
    
    return 0;
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : detacher_ipc
 * Description : Detache la memoire partagee (sans la detruire)
 * -----------------------------------------------------------------------------
 */
void detacher_ipc(void) {
    if (shm != NULL && shm != (void *)-1) {
        shmdt(shm);
        shm = NULL;
    }
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : detruire_ipc
 * Description : Detruit toutes les IPC creees
 *               Appele uniquement par le processus initial a la fin
 * -----------------------------------------------------------------------------
 */
void detruire_ipc(void) {
    /* Detruire la memoire partagee */
    if (shm_id != -1) {
        shmctl(shm_id, IPC_RMID, NULL);
        shm_id = -1;
    }
    
    /* Detruire les semaphores */
    if (sem_id != -1) {
        semctl(sem_id, 0, IPC_RMID);
        sem_id = -1;
    }
    
    /* Detruire les files de messages */
    if (msg_vendeur_id != -1) {
        msgctl(msg_vendeur_id, IPC_RMID, NULL);
        msg_vendeur_id = -1;
    }
    
    if (msg_caissier_id != -1) {
        msgctl(msg_caissier_id, IPC_RMID, NULL);
        msg_caissier_id = -1;
    }
    
    /* Supprimer le fichier de cle */
    unlink(IPC_KEY_FILE);
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : sem_P
 * Description : Operation P (wait/proberen) sur un semaphore
 *               Decremente le semaphore de 1, bloque si <= 0
 * Parametre   : sem_index - index du semaphore dans l'ensemble
 * Retour      : 0 si succes, -1 si erreur
 * -----------------------------------------------------------------------------
 */
int sem_P(int sem_index) {
    struct sembuf op = {sem_index, -1, 0};
    
    if (semop(sem_id, &op, 1) == -1) {
        if (errno != EINTR) {
            perror("[ERREUR] semop P");
        }
        return -1;
    }
    return 0;
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : sem_V
 * Description : Operation V (signal/verhogen) sur un semaphore
 *               Incremente le semaphore de 1
 * Parametre   : sem_index - index du semaphore dans l'ensemble
 * Retour      : 0 si succes, -1 si erreur
 * -----------------------------------------------------------------------------
 */
int sem_V(int sem_index) {
    struct sembuf op = {sem_index, 1, 0};
    
    if (semop(sem_id, &op, 1) == -1) {
        perror("[ERREUR] semop V");
        return -1;
    }
    return 0;
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : sem_wait_zero
 * Description : Attend que le semaphore atteigne la valeur 0
 * Parametre   : sem_index - index du semaphore dans l'ensemble
 * Retour      : 0 si succes, -1 si erreur
 * -----------------------------------------------------------------------------
 */
int sem_wait_zero(int sem_index) {
    struct sembuf op = {sem_index, 0, 0};
    
    if (semop(sem_id, &op, 1) == -1) {
        if (errno != EINTR) {
            perror("[ERREUR] semop wait_zero");
        }
        return -1;
    }
    return 0;
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : trouver_vendeur_moins_charge
 * Description : Trouve le vendeur avec la file d'attente la plus courte
 * Retour      : Index du vendeur le moins charge
 * Note        : Doit etre appele avec le mutex SHM verrouille
 * -----------------------------------------------------------------------------
 */
int trouver_vendeur_moins_charge(void) {
    int idx = 0;
    int min_file = shm->vendeurs[0].file_attente;
    int i;
    
    for (i = 1; i < shm->nb_vendeurs; i++) {
        if (shm->vendeurs[i].file_attente < min_file) {
            min_file = shm->vendeurs[i].file_attente;
            idx = i;
        }
    }
    return idx;
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : trouver_caissier_moins_charge
 * Description : Trouve le caissier avec la file d'attente la plus courte
 * Retour      : Index du caissier le moins charge
 * Note        : Doit etre appele avec le mutex SHM verrouille
 * -----------------------------------------------------------------------------
 */
int trouver_caissier_moins_charge(void) {
    int idx = 0;
    int min_file = shm->caissiers[0].file_attente;
    int i;
    
    for (i = 1; i < shm->nb_caissiers; i++) {
        if (shm->caissiers[i].file_attente < min_file) {
            min_file = shm->caissiers[i].file_attente;
            idx = i;
        }
    }
    return idx;
}

/*
 * -----------------------------------------------------------------------------
 * Fonction    : trouver_vendeur_pour_rayon
 * Description : Trouve un vendeur competent pour un rayon donne
 * Parametre   : rayon - numero du rayon (0 a NB_RAYONS-1)
 * Retour      : Index du vendeur competent
 * Note        : Au moins un vendeur par rayon est garanti a l'initialisation
 * -----------------------------------------------------------------------------
 */
int trouver_vendeur_pour_rayon(int rayon) {
    int i;
    
    for (i = 0; i < shm->nb_vendeurs; i++) {
        if (shm->vendeurs[i].rayon == rayon) {
            return i;
        }
    }
    
    /* Ne devrait jamais arriver car initialisation garantit 1 vendeur/rayon */
    return 0;
}
