# Module Template OST

## Vue d'ensemble

Ce dossier contient un squelette complet (`template/`) pour créer de nouveaux modules OST. Il inclut tous les patterns et bonnes pratiques nécessaires.

## Structure du module template

```
template/
├── template.h           # Header avec déclaration de classe
├── template.cpp         # Implémentation
├── template.json        # Définition des propriétés
├── template.qrc         # Ressources Qt
└── template.fr.md       # Documentation (optionnel)
```

## Créer un nouveau module

### 1. Copier le template

```bash
cd OST-modules/src/modules
cp -r template monmodule
cd monmodule
```

### 2. Renommer les fichiers

```bash
mv template.h monmodule.h
mv template.cpp monmodule.cpp
mv template.json monmodule.json
mv template.qrc monmodule.qrc
mv template.fr.md monmodule.fr.md
```

### 3. Remplacer dans tous les fichiers

Dans **monmodule.h** :
- Remplacer `TEMPLATE_H` par `MONMODULE_H`
- Remplacer `class Template` par `class Monmodule`
- Remplacer `Template*` par `Monmodule*`

Dans **monmodule.cpp** :
- Remplacer `Template` par `Monmodule` (toutes occurrences)
- Remplacer `Q_INIT_RESOURCE(template)` par `Q_INIT_RESOURCE(monmodule)`
- Remplacer `":template.json"` par `":monmodule.json"`

Dans **monmodule.qrc** :
- Remplacer `template.json` par `monmodule.json`

Dans **monmodule.json** :
- Adapter les propriétés à vos besoins

### 4. Ajouter au CMakeLists.txt

Dans `OST-modules/CMakeLists.txt`, ajouter avant `install(TARGETS` :

```cmake
# Mon module
add_library(ostmonmodule SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/monmodule/monmodule.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/monmodule/monmodule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/monmodule/monmodule.qrc
)
target_link_libraries(ostmonmodule PRIVATE
    ${OST_LIBRARY_INDI}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Widgets
    Threads::Threads
    z
)
target_compile_definitions(ostmonmodule PRIVATE MONMODULE_MODULE)
```

Et ajouter `ostmonmodule` dans la liste `install(TARGETS`.

### 5. Compiler

```bash
cd OST-modules
mkdir -p build
cd build
cmake ..
make
sudo make install
```

## Fonctionnalités du template

### Gestion INDI de base

```cpp
// Connecter au serveur INDI
connectIndi();

// Connecter un périphérique
connectDevice(getString("devices", "camera"));

// Envoyer une commande
sendModNewNumber(device, property, element, value);

// Lire une valeur
double value;
getModNumber(device, property, element, value);
```

### Gestion des propriétés

```cpp
// Lire une valeur de propriété
int iterations = getInt("parameters", "iterations");
double threshold = getFloat("parameters", "threshold");
QString device = getString("devices", "camera");

// Modifier une propriété
getEltInt("results", "iteration")->setValue(42, true);  // true = emit event

// Désactiver/activer une propriété
getProperty("parameters")->disable();
getProperty("parameters")->enable();
```

### Barre de progression

```cpp
// Mettre à jour la progression
getEltPrg("progress", "global")->setPrgValue(50, true);  // 50%
getEltPrg("progress", "global")->setDynLabel("Processing...", true);
```

### Grille de résultats

```cpp
// Ajouter une ligne à la grille
getEltInt("results", "iteration")->setValue(i, false);
getEltFloat("results", "value")->setValue(val, false);
getEltString("results", "timestamp")->setValue(QDateTime::currentDateTime().toString(), false);
getProperty("results")->push();  // Ajoute la ligne

// Effacer la grille
getProperty("results")->clearGrid();
```

### Gestion d'images

```cpp
// Demander une capture
requestCapture(cameraDevice, exposure, gain, offset);

// Traiter l'image dans newBLOB()
void Monmodule::newBLOB(INDI::PropertyBlob blob)
{
    mImage = new fileio();
    mImage->loadBlob(blob, 64);  // 64 = qualité debayer

    // Sauver JPEG
    QImage img = mImage->getRawQImage();
    img.save(getWebroot() + "/preview.jpeg", "JPG", 90);

    // Mettre à jour l'élément image
    OST::ImgData data = mImage->ImgStats();
    data.mUrlJpeg = "preview.jpeg";
    getEltImg("image", "image")->setValue(data, true);
}
```

### Messages

```cpp
sendMessage("Information");      // Message normal
sendWarning("Attention");        // Avertissement
sendError("Erreur critique");    // Erreur
```

### Profils

Les profils sont automatiquement gérés si `"hasprofile": true` dans le JSON. L'utilisateur peut :
- Sauver l'état actuel des paramètres
- Charger un profil sauvegardé
- Lister les profils disponibles

## Définir le rôle du module

Choisir un ou plusieurs rôles selon la fonction :

```cpp
// Dans le constructeur :
defineMeAsFocuser();    // Module d'autofocus
defineMeAsGuider();     // Module de guidage
defineMeAsSequencer();  // Module de séquences
defineMeAsNavigator();  // Module de goto/navigation
defineMeAsImager();     // Module d'imagerie générique
```

Chaque rôle ajoute automatiquement :
- Les boutons d'action appropriés (start, abort, etc.)
- Les propriétés nécessaires (exposure, gain, offset pour imager)
- Une image preview

## Périphériques INDI disponibles

```cpp
// Demander un périphérique par type :
giveMeADevice("camera", "Camera", INDI::BaseDevice::CCD_INTERFACE);
giveMeADevice("telescope", "Telescope", INDI::BaseDevice::TELESCOPE_INTERFACE);
giveMeADevice("focuser", "Focuser", INDI::BaseDevice::FOCUSER_INTERFACE);
giveMeADevice("filter", "Filter Wheel", INDI::BaseDevice::FILTER_INTERFACE);
giveMeADevice("weather", "Weather Station", INDI::BaseDevice::WEATHER_INTERFACE);
```

## Bonnes pratiques

1. **Toujours vérifier la connexion INDI** avant d'envoyer des commandes
2. **Gérer les erreurs** avec try/catch et retours booléens
3. **Désactiver les propriétés** pendant les opérations longues
4. **Mettre à jour la progression** pour feedback utilisateur
5. **Nettoyer la mémoire** (delete) des objets alloués
6. **Documenter** le code et créer un .fr.md avec les instructions

## Debugging

```cpp
// Messages de debug (visibles dans les logs du serveur)
sendMessage("Debug: value = " + QString::number(value));

// Afficher l'état d'une propriété
qDebug() << "Property state:" << getProperty("actions")->getState();
```

## Support

Pour des exemples concrets, consultez les modules existants :
- `focus/` : Exemple complet avec state machine SCXML
- `guider/` : Guidage temps réel
- `sequencer/` : Gestion de séquences complexes
- `navigator/` : Communication avec télescope

Voir aussi la documentation principale : https://documentation.ostserver.fr/
