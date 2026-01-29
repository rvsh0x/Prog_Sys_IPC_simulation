# PROJET PROGRAMMATION SYSTEME : Simulation Magasin de Bricolage - UNIX / SYSTEM V IPC

## Auteurs
- **GHODBANE Rachid**
- **Mohamed-Amine Benkacem**

Université Jean Monnet, Saint-Étienne - L3 Informatique

---

## Description

Ce projet simule l'activité d'un magasin de bricolage avec :
- **Clients** : entrent dans le magasin, cherchent un rayon, discutent avec un vendeur, puis passent en caisse
- **Vendeurs** : experts d'un rayon, renseignent les clients, les redirigent vers le vendeurs qualifié, et concluent des ventes
- **Caissiers** : encaissent les paiements des clients
- **Monitoring** : affiche en temps réel l'état du magasin

## Rayons du magasin
1. Peinture
2. Menuiserie
3. Plomberie/Chauffage
4. Éclairage/Luminaires
5. Revêtement sols/murs
6. Jardin
7. Droguerie
8. Décoration
9. Outillage
10. Quincaillerie

---

## Compilation

```bash
make
```

Pour nettoyer :
```bash
make clean
```

Pour nettoyer les IPC orphelines :
```bash
make clean-ipc
```

---

## Utilisation

### Lancer la simulation

```bash
./main <nb_vendeurs> <nb_caissiers> <nb_clients>
```

Exemple :
```bash
./main 10 3 20
```

**Contraintes :**
- `nb_vendeurs >= 10` (au moins un vendeur par rayon)
- `nb_caissiers >= 1`
- `nb_clients >= 1`

### Lancer le monitoring (depuis un autre terminal)

```bash
./monitoring
```

---

## Architecture

### Fichiers sources

| Fichier | Description |
|---------|-------------|
| `main.c` | Processus initial : crée les IPC et lance tous les processus |
| `vendeur.c` | Processus vendeur : renseigne les clients et conclut des ventes |
| `caissier.c` | Processus caissier : encaisse les paiements |
| `client.c` | Processus client : parcourt le magasin |
| `monitoring.c` | Affichage temps réel de l'état du magasin |
| `ipc.c` / `ipc.h` | Gestion des IPC System V |
| `log.c` / `log.h` | Système de logging (terminal + fichier) |
| `utils.c` / `utils.h` | Fonctions utilitaires |
| `config.h` | Paramètres de configuration |

### IPC utilisées

- **Mémoire partagée** : état global du magasin (vendeurs, caissiers, achats)
- **Sémaphores** : synchronisation et exclusion mutuelle
- **Files de messages** : communication client↔vendeur et client↔caissier

### Signaux

- `SIGINT` : arrêt propre de la simulation
- `SIGUSR1` / `SIGUSR2` : synchronisation interne

---

## Configuration

Les paramètres sont modifiables dans `config.h` :

```c
#define TEMPS_DISCUSSION_MIN    1   // Temps min discussion (secondes)
#define TEMPS_DISCUSSION_MAX    3   // Temps max discussion
#define TEMPS_PAIEMENT_MIN      1   // Temps min paiement
#define TEMPS_PAIEMENT_MAX      2   // Temps max paiement
#define MONTANT_ACHAT_MIN       10  // Montant min achat (euros)
#define MONTANT_ACHAT_MAX       500 // Montant max achat
#define PROBA_VENTE_REUSSIE     65  // Probabilité vente (%)
```

---

## Fichier de log

Les actions sont enregistrées dans `magasin.log` avec le format :
```
[HH:MM:SS][ACTEUR] Description de l'action
```

---

## Déroulement d'une simulation

1. Le processus initial crée les IPC
2. Les vendeurs et caissiers sont lancés
3. Les clients entrent progressivement dans le magasin
4. Chaque client :
   - Choisit un rayon aléatoirement
   - Va vers le vendeur le moins chargé
   - Est redirigé si le vendeur n'est pas compétent
   - Discute avec un vendeur compétent
   - Décide d'acheter (65% de chances)
   - Si achat : passe en caisse et paie
   - Quitte le magasin
5. Quand tous les clients ont terminé, les vendeurs et caissiers sont arrêtés
6. Les IPC sont nettoyées

