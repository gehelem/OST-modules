# Améliorations du Module Navigator

## 🎯 Fonctionnalité Ajoutée : Boucle de Recentrage Itératif

### Vue d'ensemble

Le module Navigator a été amélioré avec une **boucle de correction automatique** qui recentre précisément la cible après le goto initial. Cette amélioration transforme un goto approximatif en un centrage de précision.

### Workflow Complet

```
1. Utilisateur sélectionne une cible (ex: M31)
   ↓
2. Clic "gototarget"
   ↓
3. Slew initial vers les coordonnées calculées
   ↓
4. Acquisition image + plate-solving
   ↓
5. Calcul offset (RA/DEC résolues vs cible)
   ↓
6. Si offset > tolérance ET iterations < max:
   → Correction offset
   → Slew vers position corrigée
   → Retour à étape 4
   ↓
7. Si offset ≤ tolérance:
   ✅ SUCCÈS: "Centering successful after N iteration(s)"
   ↓
8. Si iterations ≥ max ET offset > tolérance:
   ❌ ERREUR: "Centering failed after N iterations"
```

### Nouvelles Propriétés (navigator.json)

**Section "centeringparams"** ajoutée :

```json
{
  "maxiterations": {
    "type": "int",
    "value": 5,
    "min": 1,
    "max": 10,
    "slider": 2,
    "hint": "Maximum number of centering iterations"
  },
  "tolerance": {
    "type": "float",
    "value": 30.0,
    "min": 5.0,
    "max": 300.0,
    "slider": 2,
    "format": "%.1f",
    "hint": "Centering tolerance in arcseconds"
  }
}
```

**Valeurs par défaut** :
- Max iterations : 5
- Tolérance : 30 secondes d'arc

### Nouvelles Méthodes (navigator.cpp)

#### 1. `checkCentering(solvedRA, solvedDEC, targetRA, targetDEC)`

Vérifie si le centrage est satisfaisant.

**Calculs** :
```cpp
// Conversion RA en degrés
deltaRA = (solvedRA - targetRA) * 15.0

// Correction sphérique (cos(DEC))
deltaRA *= cos(targetDEC_radians)

// Distance angulaire totale
distance = sqrt(deltaRA² + deltaDEC²) * 3600  // en arcsec
```

**Retour** : `true` si `distance ≤ tolerance`

**Log exemple** :
```
Offset: RA=12.3" DEC=-8.5" Total=14.9"
```

#### 2. `correctOffset(solvedRA, solvedDEC)`

Applique la correction à la monture.

**Principe** :
```cpp
correction_RA = target_RA - solved_RA
correction_DEC = target_DEC - solved_DEC

nouvelle_position = position_actuelle + correction
```

**Log exemple** :
```
Applying correction: RA 5.1234h → 5.1256h, DEC 42.12° → 42.15°
```

La méthode envoie la nouvelle position au mount via `EQUATORIAL_EOD_COORD`.

#### 3. `OnSucessSolve()` (refactorisée)

Gère la boucle de recentrage :

```cpp
if (solve failed) {
    → Error, abort
}

iteration++

if (centered within tolerance) {
    → Success, stop
}

if (iteration >= max) {
    → Error "centering failed", stop
}

→ Apply correction, continue loop
```

### Variables d'État Ajoutées

```cpp
int mMaxIterations;          // Chargé depuis JSON
int mCurrentIteration;       // Compteur 0..max
double mToleranceArcsec;     // Tolérance en arcsec
double mTargetRA;            // RA cible (mémorisée)
double mTargetDEC;           // DEC cible (mémorisée)
```

### Messages Utilisateur

**Démarrage** :
```
Starting goto with centering: max 5 iterations, tolerance 30.0"
```

**Chaque itération** :
```
Centering iteration 2/5 - applying correction
Offset: RA=45.2" DEC=-22.1" Total=50.3"
Applying correction: RA 5.1234h → 5.1267h, DEC 42.12° → 42.18°
```

**Succès** :
```
Centering successful after 3 iteration(s) - within tolerance
```

**Échec** :
```
Centering failed after 5 iterations - tolerance not reached
```

**Solve échoue** :
```
Image NOT solved - centering aborted
```

## 🔧 Bugs Corrigés

### 1. Chemin hardcodé (CRITIQUE)
**Avant** :
```cpp
stellarSolver.setProperty("LogFileName", "/home/gilles/projets/OST/solver.log");
```

**Après** :
```cpp
stellarSolver.setProperty("LogFileName", getWebroot() + "/navigator_solver.log");
```

### 2. Code dupliqué
**Avant** :
```cpp
sendModNewNumber(..., "SIM_TIME_FACTOR", 0.01);  // Ligne 230
sendModNewNumber(..., "SIM_TIME_FACTOR", 0.01);  // Ligne 232 (doublon)
```

**Après** : Supprimé

### 3. Simulation sans condition
**Avant** :
```cpp
if (camera == "CCD Simulator") { ... }
sendModNewNumber(...);  // Envoyé même si pas simulateur
```

**Après** : Correction uniquement dans le if

## 📊 Performances Estimées

### Scénarios Typiques

**Goto précis (offset < 30") :**
- 1 itération
- Temps : ~15-30 secondes (slew + image + solve)

**Goto moyen (offset 30-120") :**
- 2-3 itérations
- Temps : ~45-90 secondes

**Goto imprécis (offset > 300") :**
- 4-5 itérations ou échec
- Temps : ~2-3 minutes

### Facteurs d'Influence

- **Qualité du modèle de pointage** : ±
- **Précision polaire** : ±
- **Seeing** : Affecte le solve
- **Magnitude cible** : Objets faibles = solve plus difficile

## 🎛️ Configuration Recommandée

### Usage Standard (DSO)
```
Max iterations : 5
Tolerance : 30" (0.5 arcmin)
```

### Imagerie Haute Résolution
```
Max iterations : 7
Tolerance : 10" (pour petit capteur)
```

### Goto Rapide (survey)
```
Max iterations : 3
Tolerance : 60" (1 arcmin)
```

### Monture Non Calibrée
```
Max iterations : 10
Tolerance : 60"
```

## 🚀 Utilisation

1. **Ouvrir module Navigator** dans le frontend OST
2. **Onglet Parameters** : Ajuster "Max iterations" et "Tolerance"
3. **Sauver profil** (optionnel)
4. **Rechercher cible** : Entrer nom → clic forward
5. **Sélectionner** : Clic sur ligne résultat
6. **Goto** : Clic "gototarget"
7. **Observer logs** : Messages de progression
8. **Vérifier image** : Overlay RA/DEC résolues

## 🐛 Limitations Connues

1. **Pas de spiral search** : Si 1ère image ne solve pas, échec immédiat
2. **Pas de seuil adaptatif** : Tolérance fixe pour toutes cibles
3. **Pas d'historique** : Pas de statistiques de précision
4. **Géométrie sphérique simplifiée** : OK jusqu'à pôle ±80°

## 🔮 Évolutions Futures

### Court terme
- [ ] Barre de progression visuelle (0-100%)
- [ ] Graphe offset vs iteration
- [ ] Alerte sonore si échec

### Moyen terme
- [ ] Spiral search si solve échoue
- [ ] Tolérance adaptative selon magnitude
- [ ] Historique goto (succès/échecs/offsets)

### Long terme
- [ ] Modèle de pointage intégré (TPoint-like)
- [ ] Export corrections pour INDI
- [ ] Intégration séquenceur (mosaïques)

## 📝 Notes Développeur

### Threading
La boucle est **séquentielle** :
- Correction → Attente slew (callback) → Shoot → Attente BLOB → Solve → Décision

Pas de risque de race condition car tout est événementiel (Qt signals/slots).

### Précision Calculs
- **RA** : Correction sphérique cos(DEC) appliquée
- **DEC** : Distance orthodromique simplifiée (OK si < 10°)
- **Unités** : RA en heures, DEC en degrés, offsets en arcsec

### Abort Propre
L'abort (`abortnavigator`) :
1. Stoppe le solver
2. Remet `mState = "idle"`
3. Réinitialise `mCurrentIteration = 0`

Pas de risque de répétition infinie.

---

**Version** : 0.2
**Date** : 2025-01-11
**Auteur** : Claude Code + Gilles
