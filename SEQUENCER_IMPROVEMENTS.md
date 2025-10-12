# Améliorations du Module Sequencer

## 🎯 Fonctionnalité Ajoutée : Autofocus sur Changement de Filtre

### Vue d'ensemble

Le module Sequencer a été amélioré avec une **communication inter-modules** permettant de déclencher automatiquement un autofocus lorsque le filtre change pendant l'exécution d'une séquence. Cette fonctionnalité améliore la qualité des images en compensant le defocus chromatique introduit par les différents filtres.

### Workflow Complet

```
1. Utilisateur configure séquence avec différents filtres
   ↓
2. Active "Auto-focus on filter change" dans Parameters
   ↓
3. Lance la séquence (startsequence)
   ↓
4. Pour chaque ligne de la séquence :
   ├─ Si filtre == filtre précédent :
   │  └─ Shoot directement
   │
   └─ Si filtre ≠ filtre précédent :
      ↓
      5. Détection du changement de filtre
      ↓
      6. Change le filtre physique (FILTER_SLOT)
      ↓
      7. Émet événement "requestfocus" vers module focus
      ↓
      8. Module focus démarre autofocus (startCoarse)
      ↓
      9. Focus trouve meilleure position
      ↓
      10. Module focus émet événement "focusdone"
      ↓
      11. Sequencer reçoit confirmation
      ↓
      12. Sequencer reprend → Shoot()
      ↓
      13. Continue séquence normalement
```

### Nouvelles Propriétés (sequencer.json)

**Section "parameters"** ajoutée :

```json
{
  "parameters": {
    "devcat": "Parameters",
    "order": 0,
    "hasprofile": true,
    "label": "Parameters",
    "elements": {
      "autofocusonfilterchange": {
        "type": "bool",
        "label": "Auto-focus on filter change",
        "value": false,
        "hint": "Automatically trigger focus when filter changes during sequence"
      },
      "focusmodule": {
        "type": "string",
        "label": "Focus module instance",
        "value": "focus",
        "hint": "Name of the focus module INSTANCE to use (e.g. 'focus', 'myfocus1')"
      }
    }
  }
}
```

**Valeurs par défaut** :
- Auto-focus activé : `false` (opt-in pour éviter comportement inattendu)
- Nom de l'instance du module focus : `"focus"` (nom par défaut de l'instance focus)

**Note importante** : Le paramètre `focusmodule` référence le **nom de l'instance** du module focus, pas le type de module. Si vous avez plusieurs instances de modules focus (par exemple `"focus1"`, `"focus2"`), vous devez spécifier le nom exact de l'instance que vous voulez utiliser.

### Modifications du Code

#### 1. sequencer.h

**Nouvelles variables d'état** :
```cpp
QString previousFilter = "";      // Mémorise filtre précédent
bool mWaitingForFocus = false;    // Flag d'attente focus
```

**Nouvelles méthodes** :
```cpp
void requestFocus();              // Demande autofocus au module focus
void OnFocusDone(...);            // Callback quand focus terminé
```

#### 2. sequencer.cpp - StartLine()

**Détection du changement de filtre** :
```cpp
// Lecture du nouveau filtre
currentFilter = getEltInt("sequence", "filter")->getLov()[i];

// Détection changement
bool filterChanged = (!previousFilter.isEmpty() && previousFilter != currentFilter);
bool autofocusEnabled = getBool("parameters", "autofocusonfilterchange");

if (filterChanged && autofocusEnabled && (currentFrameType == "L" || currentFrameType == "F"))
{
    sendMessage("Filter changed from " + previousFilter + " to " + currentFilter);

    // Change le filtre
    sendModNewNumber(getString("devices", "filter"), "FILTER_SLOT", "FILTER_SLOT_VALUE", i);

    // Prépare les dossiers
    // ...

    // Demande focus ET RETOURNE (pas de Shoot ici)
    requestFocus();
    return;
}

// Mise à jour du filtre précédent
previousFilter = currentFilter;
```

**Logique clé** :
- Ne déclenche autofocus que pour frames **LIGHT** et **FLAT** (pas BIAS/DARK)
- Première ligne de séquence : `previousFilter` est vide → pas d'autofocus
- Retour anticipé : `Shoot()` ne sera appelé qu'après `OnFocusDone()`

#### 3. sequencer.cpp - requestFocus()

**Émission d'événement inter-module** :
```cpp
void Sequencer::requestFocus()
{
    QString focusModule = getString("parameters", "focusmodule");
    sendMessage("Filter changed - requesting autofocus from module: " + focusModule);

    // Structure QVariantMap pour simuler click sur bouton "autofocus"
    QVariantMap eventData;
    QVariantMap actionsMap;
    QVariantMap elementsMap;
    QVariantMap autofocusMap;

    autofocusMap["value"] = true;
    elementsMap["autofocus"] = autofocusMap;
    actionsMap["elements"] = elementsMap;
    eventData["actions"] = actionsMap;

    mWaitingForFocus = true;

    // Émet vers module focus
    emit moduleEvent("Flupdate", focusModule, "actions", eventData);
}
```

**Format de l'événement** :
- Type : `"Flupdate"` (Field update)
- Module cible : `"focus"` (ou valeur de `focusmodule`)
- Clé : `"actions"`
- Données : Simule un clic sur propriété `actions.autofocus`

#### 4. sequencer.cpp - OnFocusDone()

**Réception de la confirmation** :
```cpp
void Sequencer::OnFocusDone(const QString &eventType, const QString &eventModule,
                           const QString &eventKey, const QVariantMap &eventData)
{
    sendMessage("Autofocus completed - resuming sequence");
    mWaitingForFocus = false;

    // Continue la séquence
    Shoot();
}
```

**Détection dans OnMyExternalEvent()** :
```cpp
// En début de méthode, avant traitement événements propres au module
if (eventType == "focusdone" && mWaitingForFocus)
{
    OnFocusDone(eventType, eventModule, eventKey, eventData);
    return;
}
```

#### 5. focus.cpp - SMFocusDone()

**Émission de l'événement de complétion** :
```cpp
void Focus::SMFocusDone()
{
    // ... code existant ...

    // Émet événement "focusdone" pour notifier les autres modules
    QVariantMap eventData;
    QVariantMap resultsMap;
    QVariantMap elementsMap;
    QVariantMap hfrMap;
    QVariantMap posMap;

    hfrMap["value"] = _solver.HFRavg * ech;
    posMap["value"] = getFloat("results", "pos");
    elementsMap["hfr"] = hfrMap;
    elementsMap["pos"] = posMap;
    resultsMap["elements"] = elementsMap;
    eventData["results"] = resultsMap;

    emit moduleEvent("focusdone", getModuleName(), "results", eventData);
}
```

**Données transmises** :
- HFR final (qualité focus)
- Position focuser finale

### Architecture de Communication

#### ⚠️ PRÉREQUIS CRITIQUE : Connexions Directes Entre Modules

**IMPORTANT** : Pour que la communication inter-modules fonctionne, vous DEVEZ décommenter les lignes 185-186 dans `OST/src/controller.cpp` :

```cpp
// Dans la fonction Controller::loadModule(), après la ligne 184
connect(othermodule, &Basemodule::moduleEvent, mod, &Basemodule::OnExternalEvent);
connect(mod, &Basemodule::moduleEvent, othermodule, &Basemodule::OnExternalEvent);
```

**Pourquoi ces lignes sont nécessaires** :
- Par défaut, ces lignes étaient **commentées** dans le code original
- Sans elles, les modules ne peuvent communiquer qu'à travers le Controller
- Avec elles, les modules peuvent s'envoyer des événements directement

**Impact** :
```
AVANT (lignes commentées):           APRÈS (lignes décommentées):
SEQUENCER → CONTROLLER → FOCUS       SEQUENCER ←→ FOCUS
(événement perdu)                    (communication directe)
```

#### Flux des Événements

```
SEQUENCER                                      FOCUS
    |                                           |
    |--emit moduleEvent()---------------------->|
    |  ("requestautofocus", "focus", "", {})    |
    |                                           |
    |                           OnMyExternalEvent
    |                           → startCoarse() |
    |                           → State machine |
    |                                  ...      |
    |                           → SMFocusDone() |
    |                                           |
    |<--------emit moduleEvent()----------------|
    |         ("focusdone", "sequencer", ...)   |
    |                                           |
    | OnMyExternalEvent                         |
    | → OnFocusDone()                           |
    | → Shoot()                                 |
```

**Points clés** :
1. **Communication directe** : Les modules sont cross-connectés (lignes 185-186 de controller.cpp)
2. **Événement personnalisé** : `"requestautofocus"` (pas `"Flupdate"`) pour éviter problèmes de setValue()
3. **Événement de complétion** : `"focusdone"` pour notifier le sequencer
4. **Protection contre boucles** : Chaque module vérifie `if (getModuleName() == eventModule)`

**Détails d'implémentation** :

**sequencer.cpp:414** - Émission événement :
```cpp
void Sequencer::requestFocus()
{
    QString focusModule = getString("parameters", "focusmodule");
    mWaitingForFocus = true;
    emit moduleEvent("requestautofocus", focusModule, "", QVariantMap());
}
```

**focus.cpp:62** - Réception événement :
```cpp
void Focus::OnMyExternalEvent(...)
{
    if (eventType == "requestautofocus" && getModuleName() == eventModule)
    {
        sendMessage("Autofocus requested by another module - starting");
        getProperty("actions")->setState(OST::Busy);
        startCoarse();
        return;
    }
}
```

**focus.cpp:525** - Émission événement de complétion :
```cpp
void Focus::SMFocusDone()
{
    // ... focus terminé
    emit moduleEvent("focusdone", getModuleName(), "results", eventData);
}
```

### Conditions de Déclenchement

L'autofocus est déclenché SI ET SEULEMENT SI :

✅ **Toutes ces conditions sont vraies** :
1. `autofocusonfilterchange == true` (paramètre activé)
2. `previousFilter != ""` (pas la première ligne)
3. `currentFilter != previousFilter` (filtre réellement changé)
4. `currentFrameType == "L" OR "F"` (Light ou Flat)

❌ **Pas d'autofocus si** :
- Paramètre désactivé
- Première ligne de la séquence
- Même filtre que ligne précédente
- Frame type = BIAS ou DARK

### Gestion des Erreurs

#### Abort Séquence
```cpp
if (keyelt == "abortsequence")
{
    emit Abort();
    isSequenceRunning = false;
    mWaitingForFocus = false;  // ← Reset du flag
}
```

#### Focus Échoue
Si le focus échoue (timeout, aucune étoile, etc.) :
- Le module focus n'émet PAS `focusdone`
- Le sequencer reste en état `mWaitingForFocus = true`
- **Solution** : L'utilisateur doit abort la séquence manuellement
- **Amélioration future** : Ajouter timeout dans sequencer

#### Module Focus Inexistant
Si `focusmodule` pointe vers un module inexistant :
- L'événement est émis mais personne n'écoute
- Même comportement qu'un focus qui échoue
- **Solution** : Vérifier nom du module dans configuration

### Messages Utilisateur

**Détection changement** :
```
Filter changed from Ha to OIII
Filter changed - requesting autofocus from module: focus
```

**Focus terminé** :
```
Focus done                              (depuis focus module)
Autofocus completed - resuming sequence (depuis sequencer)
```

**Séquence normale** :
```
Sequence completed
```

## 📊 Impact sur les Performances

### Sans Autofocus (comportement par défaut)
- Temps par ligne : **t_filter_change + t_exposure**
- Typique : 5-10s (change filtre) + temps exposition

### Avec Autofocus Activé
- Temps par ligne (changement filtre) : **t_filter + t_focus + t_exposure**
- Typique : 5-10s + **60-120s** (focus) + temps exposition

### Exemple de Séquence

**Configuration** :
```
Line 1: Ha  - 10x 300s
Line 2: OIII - 10x 300s
Line 3: SII  - 10x 300s
```

**Sans autofocus** :
- Total : 30 × 300s = **2h 30min**

**Avec autofocus** :
- Ha  : 10 × 300s = 50min (pas de focus ligne 1)
- OIII : 2min (focus) + 10 × 300s = 52min
- SII  : 2min (focus) + 10 × 300s = 52min
- Total : **2h 34min** (4min overhead)

**Bénéfices** :
- Focus optimal pour chaque filtre
- Compense défocus chromatique
- Images plus nettes → meilleur SNR

## 🎛️ Configuration Recommandée

### Usage Standard (Tri-color LRGB)
```json
{
  "autofocusonfilterchange": true,
  "focusmodule": "focus"
}
```
**Justification** : Défocus significatif entre filtres RGB

### Narrowband (Ha/OIII/SII)
```json
{
  "autofocusonfilterchange": true,
  "focusmodule": "focus"
}
```
**Justification** : Défocus important entre longueurs d'onde étroites

### Flats avec Plusieurs Filtres
```json
{
  "autofocusonfilterchange": true,
  "focusmodule": "focus"
}
```
**Justification** : Focus précis nécessaire pour bon flat

### Mono (pas de filtres) ou Séquence 1 Filtre
```json
{
  "autofocusonfilterchange": false
}
```
**Justification** : Pas de changement de filtre → inutile

### BIAS/DARK Uniquement
```json
{
  "autofocusonfilterchange": false
}
```
**Justification** : Code skip automatiquement BIAS/DARK mais économise vérifications

## 🚀 Utilisation

### Configuration

#### Instance Unique (cas standard)
1. **Ouvrir module Sequencer** dans frontend OST
2. **Onglet Parameters** (nouveau) :
   - Cocher "Auto-focus on filter change"
   - Vérifier "Focus module instance" = `"focus"` (valeur par défaut)
3. **Sauver profil** (optionnel)

#### Instances Multiples (configuration avancée)
Si vous avez plusieurs instances de modules focus (par exemple, un focus pour chaque setup optique) :

1. **Identifier le nom de l'instance focus** :
   - Dans OST, allez dans la configuration
   - Notez le nom exact de l'instance focus que vous voulez utiliser
   - Exemples : `"focus1"`, `"wide_field_focus"`, `"narrow_band_focus"`

2. **Configurer le sequencer** :
   - Ouvrir module Sequencer
   - Onglet Parameters
   - Entrer le nom EXACT de l'instance dans "Focus module instance"
   - Exemple : Si votre instance s'appelle `"focus_longue_focale"`, entrez exactement ce nom

3. **Vérifier la communication** :
   - Lancer une séquence de test avec changement de filtre
   - Observer les logs : `"Filter changed - requesting autofocus from module: focus_longue_focale"`
   - Si erreur "module introuvable", vérifiez l'orthographe du nom

### Création de Séquence
4. **Onglet Sequence** :
   - Ajouter lignes avec différents filtres
   - Exemple :
     ```
     L - Ha   - 300s - 10x
     L - OIII - 300s - 10x
     L - SII  - 300s - 10x
     ```

### Exécution
5. **Start Sequence** :
   - Ligne 1 (Ha) : Pas de focus (première ligne)
   - Ligne 2 (OIII) : **Focus automatique déclenché**
   - Ligne 3 (SII) : **Focus automatique déclenché**

### Monitoring
6. **Observer logs** :
   ```
   Filter changed from Ha to OIII
   Filter changed - requesting autofocus from module: focus
   [Focus module logs...]
   Focus done
   Autofocus completed - resuming sequence
   ```

7. **Vérifier image** :
   - Première image après changement filtre = meilleur focus

## 🐛 Limitations Connues

### 1. Pas de Timeout
**Problème** : Si focus échoue, sequencer attend indéfiniment
**Workaround** : Abort manuel de la séquence
**Solution future** : Timer de 5 minutes → skip focus si timeout

### 2. Pas de Retry
**Problème** : Si focus échoue, pas de nouvelle tentative
**Solution future** : Paramètre `maxfocusretries`

### 3. Pas de Validation Nom d'Instance
**Problème** : Si `focusmodule` référence une instance inexistante, l'événement est envoyé mais personne ne répond
**Scénario** :
- Utilisateur entre `"focus2"` mais l'instance s'appelle `"focus_2"` (avec underscore)
- Événement émis vers module inexistant
- Sequencer attend indéfiniment (comportement identique à un focus qui échoue)

**Workaround actuel** :
- Vérifier orthographe exacte du nom d'instance dans configuration OST
- Observer les logs pour confirmation : `"Filter changed - requesting autofocus from module: XXX"`
- Si aucune réponse du focus, abort manuel et corriger le nom

**Solution future** :
- Valider existence de l'instance avant d'émettre l'événement
- Message d'erreur clair : `"Focus module 'focus2' not found - available: focus, focus_widefield"`
- Liste déroulante dynamique des instances focus disponibles

### 4. Focus Lancé pour Chaque Ligne
**Problème** : Si séquence a 10 lignes avec même filtre, puis change :
```
L - Ha - 300s - 10x    ← 10 lignes
L - OIII - 300s - 1x   ← Focus déclenché
```
Focus déclenché qu'une fois (OK), mais si température change pendant 10× Ha, pas de refocus

**Solution future** : Paramètre `refocusevery` (toutes les N images)

### 5. Pas de Refocus si Température Change
**Problème** : Focus peut dériver avec température même sans changement filtre
**Solution future** : Intégration avec capteur température focuser

## 🔮 Évolutions Futures

### Court terme
- [ ] Timeout pour autofocus (5 min)
- [ ] Validation existence instance focus avant envoi événement
- [ ] Message erreur si instance focus introuvable
- [ ] Indicateur visuel "Waiting for focus" dans UI
- [ ] Liste déroulante dynamique des instances focus disponibles

### Moyen terme
- [ ] Retry automatique si focus échoue (max 3×)
- [ ] Refocus périodique (toutes les N images)
- [ ] Support multi-modules focus (choisir selon config)
- [ ] Statistiques : temps focus moyen, nb succès/échecs

### Long terme
- [ ] Prédiction offset focus par filtre (modèle ML)
- [ ] Compensation température automatique
- [ ] Focus asynchrone (overlapping avec download image)
- [ ] Focus pendant slew (si goto avant séquence)

## 📝 Notes Développeur

### Threading et Synchronisation
- Communication **événementielle** (Qt signals/slots)
- Pas de threads bloquants
- `mWaitingForFocus` : Simple flag booléen (pas de mutex nécessaire car single-threaded Qt event loop)

**Mécanisme de blocage** :
Le flag `mWaitingForFocus` empêche le sequencer de shooter prématurément :

```cpp
// Dans updateProperty() et newProperty() :
if (FILTER_SLOT == IPS_OK && !mWaitingForFocus) {
    Shoot();  // N'est PAS appelé si on attend le focus
}
```

**Workflow détaillé** :
1. `StartLine()` détecte changement filtre → `mWaitingForFocus = true`
2. `requestFocus()` émet événement vers focus
3. Filtre wheel change physiquement → `FILTER_SLOT` passe à `IPS_OK`
4. `updateProperty()` détecte `IPS_OK` MAIS vérifie `mWaitingForFocus`
5. Comme `mWaitingForFocus == true`, `Shoot()` n'est PAS appelé
6. Focus termine → émet `focusdone`
7. `OnFocusDone()` : `mWaitingForFocus = false` puis appelle `Shoot()`
8. Séquence reprend normalement

### Format Événements
Convention OST pour `moduleEvent()` :
```cpp
emit moduleEvent(
    QString eventType,    // "Flupdate", "focusdone", etc.
    QString eventModule,  // Module CIBLE de l'événement
    QString eventKey,     // Clé de propriété (optionnel)
    QVariantMap eventData // Données structurées
);
```

**Types courants** :
- `"Flupdate"` : Mise à jour champ (Field update)
- `"Flcreate"` : Création ligne grille
- `"Fldelete"` : Suppression ligne grille
- `"focusdone"` : Custom event (focus terminé)

### Extensibilité
Ce pattern peut être réutilisé pour :
- **Sequencer → Mount** : Goto avant chaque ligne
- **Sequencer → Camera** : Ajuster gain selon filtre
- **Sequencer → Rotator** : Rotation selon cible
- **Focus → Temperature** : Compensation thermique

### Ordre d'Exécution
```cpp
1. StartLine()               // Ligne 287
   ├─ Détection changement   // Ligne 308
   ├─ Change filtre INDI     // Ligne 317
   ├─ Setup folders          // Ligne 320-337
   └─ requestFocus()         // Ligne 340
       └─ return             // Ligne 341 (IMPORTANT)

2. Focus fait son travail (async via state machine)

3. Focus::SMFocusDone()      // Ligne 557
   └─ emit moduleEvent()     // Ligne 580

4. Sequencer::OnMyExternalEvent()  // Ligne 30
   └─ OnFocusDone()                // Ligne 36-40
       └─ Shoot()                  // Ligne 395

5. Exposition démarre...
```

### Abort Propre
```cpp
// Si abort pendant focus:
1. User clique "abortsequence"
2. OnMyExternalEvent() reçoit event
3. isSequenceRunning = false
4. mWaitingForFocus = false  ← Reset
5. Focus continue (pas d'abort propagé)
6. Quand focus termine, OnFocusDone() vérifie isSequenceRunning
7. Si false, ignore et ne shoot pas
```

**Amélioration future** : Propager abort au focus module

---

## 💡 Exemples Pratiques

### Exemple 1 : Setup Standard (Instance Unique)

**Configuration OST** :
- 1 instance sequencer : `"sequencer"`
- 1 instance focus : `"focus"`

**Configuration sequencer** :
```
parameters.autofocusonfilterchange = true
parameters.focusmodule = "focus"
```

**Résultat** : Fonctionne directement avec les valeurs par défaut

---

### Exemple 2 : Plusieurs Setups Optiques

**Configuration OST** :
- 1 instance sequencer large champ : `"seq_widefield"`
- 1 instance sequencer longue focale : `"seq_longfocal"`
- 1 instance focus large champ : `"focus_widefield"`
- 1 instance focus longue focale : `"focus_longfocal"`

**Configuration seq_widefield** :
```
parameters.autofocusonfilterchange = true
parameters.focusmodule = "focus_widefield"
```

**Configuration seq_longfocal** :
```
parameters.autofocusonfilterchange = true
parameters.focusmodule = "focus_longfocal"`
```

**Résultat** : Chaque sequencer communique avec son focus dédié

---

### Exemple 3 : Pas de Focus Automatique

**Configuration OST** :
- 1 instance sequencer : `"sequencer"`
- 1 instance focus : `"focus"`

**Configuration sequencer** :
```
parameters.autofocusonfilterchange = false
```

**Résultat** : Focus manuel uniquement, pas d'autofocus sur changement filtre

---

## 📚 Références

### Fichiers Modifiés
- `OST-modules/src/modules/sequencer/sequencer.h`
- `OST-modules/src/modules/sequencer/sequencer.cpp`
- `OST-modules/src/modules/sequencer/sequencer.json`
- `OST-modules/src/modules/focus/focus.cpp`
- **`OST/src/controller.cpp`** ⚠️ **CRITIQUE** : Lignes 185-186 décommentées pour activer communication inter-modules

### Commits Associés
- `[sequencer] Add auto-focus on filter change feature`
- `[focus] Emit focusdone event for inter-module communication`
- `[controller] Enable direct module-to-module communication` ⚠️

### Installation et Compilation

**ÉTAPES OBLIGATOIRES** :

1. **Modifier controller.cpp** (dans OST, pas OST-modules) :
   ```bash
   cd OST
   nano src/controller.cpp
   # Aller aux lignes 185-186 et décommenter :
   connect(othermodule, &Basemodule::moduleEvent, mod, &Basemodule::OnExternalEvent);
   connect(mod, &Basemodule::moduleEvent, othermodule, &Basemodule::OnExternalEvent);
   ```

2. **Compiler et installer OST** :
   ```bash
   cd OST/build
   cmake ..
   make
   sudo make install
   ```

3. **Compiler et installer OST-modules** :
   ```bash
   cd OST-modules/build
   cmake ..
   make
   sudo make install
   ```

4. **Vérifier** :
   - Lancer ostserver
   - Charger sequencer et focus
   - Observer logs : `"Autofocus requested by another module - starting"`

**Si vous oubliez l'étape 1** :
- Le sequencer émettra l'événement `"requestautofocus"`
- Le focus ne le recevra JAMAIS
- Aucun autofocus ne démarrera
- Aucun message d'erreur (silencieux)

### Documentation Connexe
- `CLAUDE.md` : Architecture OST globale (section "Module Communication" mise à jour)
- `NAVIGATOR_IMPROVEMENTS.md` : Autre exemple de communication modules
- `README_TEMPLATE.md` : Guide création nouveaux modules

---

**Version** : 0.2
**Date** : 2025-01-11
**Auteur** : Claude Code + Gilles
**Modules Impactés** : sequencer (v0.2), focus (v0.2)
