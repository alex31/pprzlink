# Apprendre à utiliser pprzlink++ pas à pas

Ce guide vous accompagne dans l'écriture d'une petite application C++ qui
manipule des messages PPRZLINK, puis les échange avec une autre application.
Vous commencerez sans réseau ni matériel. Vous ajouterez ensuite UDP, une
liaison série, le bus Ivy et, enfin, une radio XBee.

Il suppose que vous savez compiler un programme C++ et connaissez les
variables, les fonctions et les classes. Les notions particulières à la
bibliothèque sont introduites au moment où elles deviennent utiles.

Les exemples suivent **l'API C++ présente dans cette branche**, dans
`lib/v2.0/C++`, avec le protocole PPRZLINK v2. Ils utilisent un petit catalogue
XML propre au guide. Ce catalogue sert à expérimenter entre vos programmes ;
pour communiquer avec Paparazzi, vous utiliserez ensuite les définitions de
votre configuration réelle.

Le parcours est le suivant :

1. [Préparer un projet](#1-préparer-un-projet).
2. [Créer et lire un premier message](#2-créer-et-lire-un-premier-message).
3. [Remplir plusieurs champs et choisir les bons types](#3-remplir-plusieurs-champs-et-choisir-les-bons-types).
   Les [conversions SI explicites](#convertir-explicitement-les-unités) permettent aussi de travailler en mètres, radians et kelvins.
4. [Manipuler des tableaux et du texte](#4-manipuler-des-tableaux-et-du-texte).
5. [Encoder puis décoder sans matériel](#5-encoder-puis-décoder-sans-matériel).
6. [Échanger des messages en UDP](#6-échanger-des-messages-en-udp).
7. [Passer à une liaison série](#7-passer-à-une-liaison-série).
8. [Recevoir et publier sur Ivy](#8-recevoir-et-publier-sur-ivy).
9. [Faire une requête et attendre sa réponse](#9-faire-une-requête-et-attendre-sa-réponse).
10. [Utiliser une radio XBee](#10-utiliser-une-radio-xbee).
11. [Construire un outil générique ou un adaptateur](#11-construire-un-outil-générique-ou-un-adaptateur).
12. [Organiser une application durable et diagnostiquer les erreurs](#12-organiser-une-application-durable-et-diagnostiquer-les-erreurs).

## 1. Préparer un projet

### Construire la bibliothèque

La bibliothèque demande C++23, CMake 3.20 ou plus, TinyXML2 et Boost.
Les conversions SI utilisent une table interne. La plateforme minimale prise en charge par cette branche est Ubuntu 24.04 avec
GCC 13. Sous Ubuntu 24.04, les dépendances du début de ce guide s'installent
avec :

```sh
sudo apt install git g++-13 cmake libtinyxml2-dev libboost-dev
```

Ivy sera ajouté à l'étape 8. Commencez avec les commandes suivantes **à la
racine du dépôt pprzlink** :

```sh
export PPRZLINK_CPP="$PWD/lib/v2.0/C++"
export PPRZLINK_ATELIER=/tmp/pprzlink-guide

cmake -S "$PPRZLINK_CPP" -B "$PPRZLINK_ATELIER/build-lib" \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPPRZLINK_WITH_IVY=OFF \
  -DPPRZLINK_BUILD_LINK=OFF \
  -DBUILD_TESTING=OFF
cmake --build "$PPRZLINK_ATELIER/build-lib" -j4
cmake --install "$PPRZLINK_ATELIER/build-lib" \
  --prefix "$PPRZLINK_ATELIER/sdk"

mkdir -p "$PPRZLINK_ATELIER/projet"
cd "$PPRZLINK_ATELIER/projet"
```

Le répertoire `sdk` contient les en-têtes, bibliothèques et fichiers CMake
installés. Les conversions d'unités sont compilées directement dans la
bibliothèque ; aucune installation supplémentaire n'est nécessaire.
Le répertoire `projet` accueillera votre programme. `/tmp` convient à cet
atelier ; choisissez un autre emplacement si vous souhaitez le conserver.

**À partir de maintenant, les commandes sont exécutées dans `projet`.**
Gardez ce terminal ouvert pour conserver les deux variables d'environnement.

### Décrire les messages de l'atelier

Créez `messages.xml` dans `projet` :

```xml
<?xml version="1.0"?>
<protocol>
  <msg_class name="guide" id="1">
    <message name="GUIDE_ALTITUDE" id="1">
      <field name="altitude" type="float" unit="m"/>
    </message>
    <message name="GUIDE_SETTING" id="2">
      <field name="ac_id" type="uint8"/>
      <field name="value" type="float"/>
    </message>
    <message name="GUIDE_SAMPLES" id="3">
      <field name="axes" type="float[3]"/>
      <field name="samples" type="uint16[]"/>
      <field name="label" type="char[4]"/>
      <field name="text" type="string"/>
    </message>
    <message name="GUIDE_ALTITUDE_REQ" id="4"/>
  </msg_class>
</protocol>
```

Pour le moment, regardez seulement `GUIDE_ALTITUDE` : il contient un champ
appelé `altitude`, de type `float`. Les autres messages serviront plus tard.

Le XML est chargé **à l'exécution**. Il n'est pas nécessaire de générer une
classe C++ par message. Un objet `Message` pourra représenter n'importe
laquelle de ces définitions.

Les identifiants de classe et de message permettent au décodeur binaire de
retrouver la définition. Les deux correspondants doivent donc utiliser des
définitions compatibles, avec le même ordre et les mêmes types de champs.
La trame ne transporte pas le XML.

### Créer le projet CMake

Créez `CMakeLists.txt` à côté du XML :

```cmake
cmake_minimum_required(VERSION 3.20)
project(atelier_pprzlink LANGUAGES CXX)

find_package(pprzlink++ CONFIG REQUIRED COMPONENTS core)

add_executable(atelier main.cpp)
target_link_libraries(atelier PRIVATE pprzlink::core)
```

Le composant `core` suffit pour manipuler des messages et leurs encodages.
La cible CMake transmet au programme les chemins d'en-têtes, les dépendances
et le standard C++23.

## 2. Créer et lire un premier message

Créez maintenant `main.cpp` :

```cpp
#include <pprzlink/MessageDictionary.h>
#include <pprzlink/Message.h>
#include <exception>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "Usage: atelier messages.xml\n";
        return 2;
    }

    try {
        const pprzlink::MessageDictionary dictionary(argv[1]);
        const auto &definition = dictionary.getDefinition("GUIDE_ALTITUDE");
        pprzlink::Message altitude(definition);

        altitude.setField("altitude", 123.5f);
        const float metres = altitude.getField<float>("altitude");

        std::cout << "Altitude : " << metres << " m\n";
        std::cout << altitude.toString() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Erreur : " << error.what() << '\n';
        return 1;
    }
}
```

Compilez et lancez :

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_PREFIX_PATH="$PPRZLINK_ATELIER/sdk"
cmake --build build -j4
./build/atelier messages.xml
```

La première ligne affiche :

```text
Altitude : 123.5 m
```

La seconde affiche le nom du message et la valeur de ses champs. C'est un
affichage de diagnostic, utile pendant le développement.

Quatre opérations viennent d'avoir lieu :

1. `MessageDictionary` a lu le fichier XML et possède les définitions.
2. `getDefinition()` a retrouvé la structure de `GUIDE_ALTITUDE`.
3. `Message` a créé un exemplaire de ce message, initialement sans valeur.
4. `setField()` a renseigné le champ, puis `getField<float>()` l'a relu.

La **définition** décrit ce qu'un message contient. Le **message** porte les
valeurs d'un exemplaire particulier. Vous pouvez créer dix messages à partir
de la même définition, chacun avec une altitude différente.

Un champ n'est pas automatiquement initialisé à zéro. Si vous lisez
`altitude` avant `setField()`, la bibliothèque lève `field_has_no_value`.
Si vous écrivez `"altitdue"`, elle lève `no_such_field` : les noms sont ceux
du XML, vérifiés à l'exécution.

**À essayer :** remplacez `123.5f` par `250.0f`, recompilez et relancez.
Ajoutez ensuite un second `setField("altitude", 300.0f)` avant la lecture :
il remplace la valeur précédente.

## 3. Remplir plusieurs champs et choisir les bons types

Les petits extraits des étapes 3 à 5 s'ajoutent dans le bloc `try` du programme
précédent, avant `return 0`. Ajoutez les en-têtes indiqués en tête de fichier.

### Préparer une commande complète

Nous voulons maintenant préparer un réglage pour l'avion 42. Ajoutez :

```cpp
pprzlink::Message command(dictionary.getDefinition("GUIDE_SETTING"));
command.setSenderId(0);
command.setReceiverId(42);
command.setComponentId(0);

command.setField("ac_id", 42);
command.setField("value", 12.5);

std::cout << command.toString() << '\n';
```

Les trois premiers appels renseignent **l'en-tête PPRZLINK**. Les deux suivants
renseignent **les champs du XML**. Dans cet atelier, nous choisissons 0 pour
l'émetteur sol et 42 pour l'avion. Ces appels n'établissent aucune connexion.

En particulier, `setReceiverId(42)` ne remplit pas `ac_id`.
Inversement, `setField("ac_id", 42)` ne modifie pas le destinataire de l'en-tête.
Certaines applications ont besoin de ces deux informations, éventuellement
avec des valeurs différentes.

Vous pouvez remplacer les deux écritures de champs par un seul appel :

```cpp
command.setField("ac_id", 42, "value", 12.5);
```

Les arguments vont par couples **nom, valeur**. Cet appel remplit le message
entier, mais ne l'envoie pas encore. Plus tard, un seul `sendMessage(command)`
transmettra l'ensemble de ses champs.

`setField()` renvoie une référence constante au message, ce qui autorise
aussi `transport.sendMessage(command.setField(...))`. Conservez pour commencer
deux instructions distinctes : le remplissage, puis l'envoi. Le chaînage
`command.setField(...).setField(...)` ne convient pas, car le premier résultat
est constant. L'ancienne méthode `addField()` remplit également un champ ;
les exemples de ce guide utilisent `setField()`.

### Comprendre les conversions

Le XML impose le type stocké. Dans `GUIDE_SETTING`, `ac_id` est un `uint8`,
même si le littéral C++ `42` est un `int`. De même, `12.5` est converti en
`float` à l'écriture de `value`.

Ajoutez l'en-tête `<cstdint>`, puis essayez :

```cpp
const auto aircraft = command.getField<std::uint8_t>("ac_id");
const float exactValue = command.getField<float>("value");
const double convertedValue = command.getFieldAs<double>("value");

std::cout << "Avion " << static_cast<unsigned>(aircraft)
          << " : " << exactValue << " / " << convertedValue << '\n';
```

`getField<T>()` demande le type exact. `getFieldAs<T>()` demande explicitement
une conversion numérique contrôlée. Ainsi, `getField<double>("value")` lèverait
`field_type_mismatch`, puisque ce champ est un `float`.
Le `static_cast<unsigned>` sert seulement à afficher `uint8_t` comme un nombre
avec `std::cout`, au lieu de l'interpréter comme un caractère.

Voici les correspondances utiles au début :

| Type du XML | Lecture C++ exacte |
| --- | --- |
| `uint8`, `uint16`, `uint32`, `uint64` | `std::uint8_t`, `std::uint16_t`, `std::uint32_t`, `std::uint64_t` |
| `int8`, `int16`, `int32`, `int64` | `std::int8_t`, `std::int16_t`, `std::int32_t`, `std::int64_t` |
| `float`, `double` | `float`, `double` |
| `char` | `char` |
| `string` | `std::string` |

Les écritures `setField("ac_id", 300)` et `setField("ac_id", 42.5)` sont
refusées : la première déborde d'un octet, la seconde perdrait une partie
fractionnaire. Les conversions vers un flottant peuvent en revanche arrondir.
`getFieldAs()` ne convertit pas les tableaux et ne transforme pas un texte
comme `"12.5"` en nombre.

### Convertir explicitement les unités

`getField()` et `setField()` manipulent les valeurs dans les unités du XML.
`getFieldAs<double>()` change leur type numérique, pas leur unité. Pour fournir
ou récupérer une grandeur SI, appelez les nouvelles méthodes :

```cpp
altitude.setFieldSI("altitude", 123.5); // Mètres SI -> float XML, ici déjà en mètres.
double altitude_si_m = altitude.getFieldSI("altitude");
std::cout << altitude_si_m << " m\n";
```

Ces appels utilisent un `double` pour la valeur SI. L'unité SI est toujours
cohérente : mètres, secondes, mètres/seconde, radians pour les angles (latitude
et longitude incluses), kelvins pour les températures absolues. Les pourcentages
sont des rapports sans unité : une entrée SI de `0.5` représente `50 %`.
Le setter choisit le type de stockage déclaré par le champ XML.

Les métadonnées sont consultables avant même de remplir un message :

```cpp
const auto& info = altitude.getFieldDefinition("altitude");
std::cout << "XML : " << info.getUnit() << "; alternative : " << info.getAltUnit() << '\n';
if (auto coefficient = info.getAltUnitCoef()) {
    std::cout << "Coefficient XML explicite : " << *coefficient << '\n';
}
std::cout << "SI : " << info.getSIUnit()
          << "; conversion disponible : " << std::boolalpha << info.canConvertSI() << '\n';
```

La définition peut aussi être récupérée par indice. Les métadonnées restent
empruntées au message ; sa destruction ou son remplacement invalide leurs références.

Pour essayer une conversion sans modifier de message, ajoutez `<cstdint>` et :

```cpp
const pprzlink::MessageField distance("distance", "int32", {}, "mm");
int32_t millimetres = distance.fromSI<int32_t>(1.23456); // 1235 mm.
double distance_si_m = distance.toSI(millimetres);      // 1.235 m.
std::cout << millimetres << " mm = " << distance_si_m << " m\n";
```

`fromSI<T>()` exige le type numérique exact du XML, comme `getField<T>()`.
Les destinations entières sont toujours arrondies à l'entier le plus proche,
avec les demi-valeurs en s'éloignant de zéro. Le getter SI restitue la valeur
réellement stockée après cet arrondi. Une valeur hors plage lève
`field_conversion_error`, sans saturation. Une écriture qui échoue conserve
la valeur précédente. Il n'y a pas de mode strict à sélectionner.

Les flottants peuvent arrondir vers `float` et conserver des NaN de mesure.
Les écritures vers des entiers refusent les valeurs non finies. Le `double` SI
ne permet pas une lecture exacte de tous les grands entiers 64 bits ; utilisez
le getter natif lorsque l'exactitude de ces entiers est nécessaire.

Pour un tableau dont tous les éléments portent la même unité, l'entrée est un
`std::span<const double>` (également construit depuis `std::array` ou `std::vector`)
et `getFieldArraySI()` renvoie directement un `std::vector<double>` :

```cpp
// Avec un champ XML : <field name="position" type="int16[3]" unit="cm"/>
std::array<double, 3> position_m{1.0, -2.0, 3.0};
message.setFieldSI("position", position_m); // Stocke 100, -200, 300 en int16.
const auto position_si = message.getFieldArraySI("position");
// Restitue 1.0, -2.0, 3.0 m, dans un vector<double> de trois éléments.
```

Ajoutez `<array>` pour cet extrait. Le vecteur retourné prend la taille du
tableau stocké dans le message : vous n'avez pas à répéter le type ni la
longueur du XML. Les lectures acceptent aussi un indice XML. Pour un scalaire,
`getFieldSI("altitude")` renvoie un `double`.

Les surcharges avec paramètre de sortie restent disponibles :

```cpp
std::vector<double> output;
message.getFieldSI("position", output);
```

Ajoutez `<vector>` pour cet extrait. Si un élément échoue, ni le champ entier
ni le tableau de sortie d'une lecture ne sont remplacés. Le champ
`axes` du catalogue initial n'a pas d'unité ; ajoutez une unité correspondant
à vos mesures dans le XML avant d'utiliser une lecture SI.

L'exemple autonome [SIUnits.cpp](pprzlink/examples/clients/SIUnits.cpp) fournit
ce XML et vérifie aussi l'encodage puis le décodage natif. Il se lance sans
matériel ni réseau :

```sh
"$PPRZLINK_ATELIER/build-lib/si_units"
```

Vous devez notamment voir `1235 mm = 1.235 m` et `20 Celsius = 293.15 K`.

### Modifier les conventions Paparazzi

La table de conversion est dans [UnitAliases.cpp](pprzlink/UnitAliases.cpp).
Une règle décrit directement la pente et le décalage vers l'unité SI :

```cpp
{"1e7deg", "rad", std::numbers::pi / (180.0 * 1e7)},
{"deg_celsius", "K", 1.0, 273.15},
{"C", "K", 1.0, 273.15, "ESC", "temperature"},
{"adc", ""}, // Libellé reconnu ; conversion SI interdite sans calibration.
```

La conversion est `SI = nombre_XML * multiplicateur + offset`. Ajoutez une
règle, puis recompilez pprzlink. Les sélecteurs message/champ sont facultatifs ;
la règle la plus spécifique gagne, puis la première en cas d'égalité.
Les conversions préparées sont immuables et partagées entre copies de définitions.

Les préfixes SI usuels permettent d'anticiper des unités comme `km`, `µA`, `MHz`
ou `mm/s^2` à partir des bases enregistrées. Les surfaces et volumes utilisent
le facteur au carré/cube (`cm²`, `mm3`). Le résolveur accepte un seul préfixe et
les formes composées connues ; il ne devine pas une expression algébrique ni
un nouvel encodage à virgule fixe.

Si le XML fournit `alt_unit` et `alt_unit_coef`, ce coefficient explicite
multiplie la pente de la règle alternative une seule fois ; l'offset n'est
pas multiplié. Cette description suffit pour certains champs à virgule fixe
sans attribut `unit`. Un coefficient sans unité ne suffit pas.

Toute unité inconnue dans `unit` ou `alt_unit` provoque `bad_message_file`
dès le chargement du catalogue, avant le démarrage des communications. Le
message d'erreur indique le fichier, message, champ, libellé et la table à
compléter. Il faut ajouter la règle ou corriger le XML avant de relancer.
Les dimensions incohérentes sont aussi refusées au chargement.

Un libellé connu mais sans conversion SI reste utilisable par les getters
natifs ; une demande SI lève `field_unit_error`. Cela concerne notamment les
commandes, capteurs sans calibration et `GROUND_REF.pos`, qui mélange degrés
et mètres selon le repère. Les transformations de repères, références
 d'altitude et epochs restent à la charge de l'application.

### Adopter l'API SI dans un programme existant

Vous pouvez remplacer localement une lecture par `getFieldSI()` ou une
écriture par `setFieldSI()` lorsque la grandeur est fournie en SI et que ses
métadonnées permettent la conversion. Les transports série, UDP et Ivy
continuent à transmettre les mêmes types et valeurs XML. Recompilez la
bibliothèque et les programmes clients : la disposition des classes C++ a
changé, ce qui interdit de réutiliser des binaires construits avec les anciens en-têtes.

Les clients série, PTY et Ivy fournis utilisent maintenant `getFieldSI()` et
`setFieldSI()` pour leurs altitudes. Le XML fourni possède `unit="m"`, mais
le même code fonctionne avec une altitude XML entière en millimètres : les
programmes continuent à fournir et afficher des mètres. Les réglages génériques `GUIDE_SETTING.value` et
`CLIENT_SETTING.value` n'ont pas d'unité physique connue : gardez leur accès
natif. Le routage de `link++` relaie lui aussi les valeurs XML ; les identifiants
`ac_id`, masques, commandes et compteurs n'exigent aucune conversion SI.
Les traitements applicatifs de mesures peuvent appeler les méthodes SI sur
les messages reçus, sans modifier les codecs ni le relais.

### Observer une erreur sans arrêter le programme

Ajoutez l'en-tête `<pprzlink/exceptions/pprzlink_exception.h>`, puis :

```cpp
try {
    command.setField("ac_id", 300);
} catch (const pprzlink::field_conversion_error &error) {
    std::cout << "Valeur refusée : " << error.what() << '\n';
}
std::cout << "ac_id est toujours " << command.getFieldAs<int>("ac_id") << '\n';
```

Une écriture invalide laisse la valeur précédente de **ce champ** intacte.
L'appel à plusieurs couples procède de gauche à droite : si le deuxième couple
échoue, le premier reste appliqué. Ce n'est pas une transaction.

Pour préparer une modification qui doit réussir entièrement avant de remplacer
le message initial, travaillez sur une copie :

```cpp
auto candidate = command;
candidate.setField("ac_id", 43, "value", 15.0);
command = candidate; // Exécuté seulement si les deux écritures ont réussi.
```

**À essayer :** demandez `getFieldAs<int>("value")` lorsque `value` vaut `12.5`.
L'erreur confirme que la bibliothèque ne tronque pas silencieusement ce nombre.

## 4. Manipuler des tableaux et du texte

Ajoutez les en-têtes `<array>`, `<span>`, `<vector>`, `<string>` et `<string_view>`. Le message
`GUIDE_SAMPLES` permet d'apprendre quatre formes de champs :

```cpp
pprzlink::Message samples(dictionary.getDefinition("GUIDE_SAMPLES"));
samples.setField(
    "axes", std::array<float, 3>{1.0f, 2.0f, 3.0f},
    "samples", std::vector<std::uint16_t>{100, 200, 300},
    "label", std::string("TEST"),
    "text", std::string("Mesure de départ"));

const auto axes = samples.getField<std::span<const float>>("axes");
const auto values = samples.getField<std::span<const std::uint16_t>>("samples");
const auto label = samples.getField<std::string_view>("label");
const auto description = samples.getField<std::string_view>("text");

std::cout << "Axe X : " << axes[0] << ", " << values.size() << " mesures\n";
std::cout << label << " : " << description << '\n';
```

`float[3]` exige exactement trois éléments. `uint16[]` autorise une taille
variable ; un `std::vector<std::uint16_t>{}` représente un tableau vide.
Les types des éléments doivent correspondre au XML lors de la lecture.

`getField<std::span<const T>>()` emprunte sans copie le tableau stocké dans le
message et renvoie sa longueur. Les getters acceptent les spans à taille
dynamique, même pour un champ XML de taille fixe comme `float[3]`. Une copie
reste disponible avec `getField<std::array<T, N>>()` ou
`getField<std::vector<T>>()`. Les vues restent valides jusqu'au remplacement du
champ, à la réaffectation ou à la destruction du message. Les lectures par index
et avec paramètre de sortie acceptent également les spans. Les éléments sont
constants : `std::span<T>` permettrait de les modifier, même si la vue est
déclarée `const auto`, et n'est donc pas accepté par ces getters.

`char[4]` représente quatre caractères, stockés comme un tableau. L'écriture
depuis `std::string` est pratique. Pour obtenir une copie du tableau, utilisez
`getField<std::vector<char>>()`. `string` est un champ texte distinct, dont
`getField<std::string>()` fournit une copie.

`getField<std::string_view>()` permet de lire sans copier les caractères des
champs `string`, `char[]` et `char[N]`. La vue emprunte le stockage du message :
elle reste valide jusqu'au remplacement de ce champ, à la réaffectation ou à
la destruction du message. Elle ne doit pas être obtenue depuis un message
temporaire. La lecture par index et les surcharges avec paramètre de sortie
acceptent aussi `std::string_view`. La taille est conservée intégralement,
y compris les éventuels octets nuls ; aucun terminateur n'est nécessaire.

Pour `char[4]`, la chaîne `"TEST"` fournit exactement quatre caractères : aucun
terminateur nul n'est ajouté. `"OK"` est trop court et `"HELLO"` trop long.
Les longueurs sont des nombres d'octets ; un caractère accentué en UTF-8 peut
occuper plusieurs octets.

Le format binaire encode la longueur des tableaux variables et des chaînes
sur un octet, soit au plus 255 éléments ou octets. La taille totale du message
doit aussi tenir dans le transport choisi : une trame PPRZ complète est limitée
à 255 octets. Un tableau autorisé isolément peut donc être trop grand pour
le message complet. Les tableaux de `string` ne sont pas pris en charge par
le codec binaire.

**À essayer :** remplacez les trois axes par deux éléments. Vous obtenez une
erreur de longueur à l'écriture, avant toute communication.

## 5. Encoder puis décoder sans matériel

Nous avons jusqu'ici manipulé des objets C++. Pour les transporter, il faut
en faire des octets. Un **codec** réalise cette conversion ; un **transport**
ajoutera les entrées/sorties à l'étape suivante.

Ajoutez `<pprzlink/PprzFrameCodec.h>`, `<span>` et `<stdexcept>`, puis cet extrait
au programme de l'étape 2 :

```cpp
altitude.setSenderId(42);
altitude.setReceiverId(0);

const auto frame = pprzlink::encodePprzFrame(altitude);
std::cout << "Trame complète : " << frame.size() << " octets\n";

pprzlink::PprzFrameDecoder decoder(dictionary);
const std::span<const std::uint8_t> bytes(frame);

decoder.pushBytes(bytes.first(3));
if (decoder.tryReceive()) {
    throw std::runtime_error("Une trame partielle ne doit pas être reçue");
}

decoder.pushBytes(bytes.subspan(3));
auto received = decoder.tryReceive();
if (!received) {
    throw std::runtime_error("Le message complet était attendu");
}

std::cout << "Altitude décodée : "
          << received->message.getFieldSI("altitude") << " m\n";
std::cout << "Taille reçue : " << received->frameSize << " octets\n";
```

Vous devez retrouver `123.5` et une taille de **12 octets** : quatre octets
de champ `float`, quatre d'en-tête PPRZLINK v2 et quatre d'enveloppe PPRZ.
`altitude.getByteSize()` ne compte que les champs ; `frame.size()` compte
la trame complète.

L'arrivée en deux morceaux reproduit une situation courante sur un port série.
Le décodeur garde les octets incomplets jusqu'à ce qu'il puisse reconstruire
un message entier.

`tryReceive()` renvoie un `std::optional<ReceivedMessage>` :

- sans valeur, aucun message complet n'a été produit pendant cet appel ;
- avec une valeur, `message` contient tous les champs décodés et `frameSize`
  indique la taille de la trame correspondante.

Un résultat vide n'est donc ni une fin de fichier ni une preuve de panne.
Ce décodeur est utilisé sans I/O. Si plusieurs trames sont disponibles,
rappelez-le pour obtenir les messages suivants. Les transports des étapes
suivantes distribueront les messages par abonnement, avec `bind()`.

### Essayer aussi le format texte

Pour préparer l'étape Ivy, ajoutez `<pprzlink/IvyMessageCodec.h>` :

```cpp
const auto line = pprzlink::ivy_codec::serializeMessage(altitude);
std::cout << line << '\n';

auto fromText = pprzlink::ivy_codec::parseMessageBody(
    dictionary.getDefinition("GUIDE_ALTITUDE"),
    "42", "GUIDE_ALTITUDE 123.5");
std::cout << fromText.getFieldSI("altitude") << '\n';
```

La ligne sérialisée commence par `42 GUIDE_ALTITUDE`, suivie de l'altitude.
Pour une altitude de `123.6`, elle vaut `42 GUIDE_ALTITUDE 123.6` : les nombres
transmis par Ivy ne sont pas complétés à six décimales. Le codec utilise
`std::to_chars` sur le type stocké pour produire la représentation courte qui
préserve exactement la valeur d'un `float` ou `double` fini à l'aller-retour.
La notation scientifique peut apparaître pour des valeurs très petites ou
très grandes ; les valeurs minuscules et le signe du zéro sont conservés.
Ce format ne dépend ni de la locale ni de la précision de `std::cout`.

`parseMessageBody()` reçoit séparément l'expéditeur et le corps du message,
qui commence par le nom. Ces fonctions ne nécessitent pas Ivy installé : elles
appartiennent à `core` et ne font aucune communication.

`toString()` est destiné au diagnostic ; `serializeMessage()` produit le
format texte d'échange. Utilisez le codec, plutôt qu'une concaténation manuelle,
pour gérer les champs texte et les tableaux.

## 6. Échanger des messages en UDP

Nous allons maintenant envoyer l'altitude à un second socket du même programme.
Cette expérience utilise le réseau local de la machine et ne demande aucun
matériel. Le système choisira deux ports libres.

Dans `CMakeLists.txt`, remplacez les lignes qui sélectionnent et lient `core`
par :

```cmake
find_package(pprzlink++ CONFIG REQUIRED COMPONENTS io)
target_link_libraries(atelier PRIVATE pprzlink::io)
```

Conservez `add_executable(atelier main.cpp)` avant `target_link_libraries()`.
Le composant `io` inclut les fonctions de `core` et ajoute notamment UDP et
la série.

Remplacez maintenant **tout `main.cpp`** par :

```cpp
#include <pprzlink/UdpTransport.h>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <boost/asio/steady_timer.hpp>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "Usage: atelier messages.xml\n";
        return 2;
    }
    try {
        using namespace std::chrono_literals;
        const pprzlink::MessageDictionary dictionary(argv[1]);
        boost::asio::io_context context;

        pprzlink::UdpTransport receiver(context, dictionary,
            {.local = {"127.0.0.1", 0}});
        pprzlink::UdpTransport sender(context, dictionary,
            {.local = {"127.0.0.1", 0}});

        pprzlink::Message altitude(dictionary.getDefinition("GUIDE_ALTITUDE"));
        altitude.setSenderId(42);
        altitude.setReceiverId(0);
        altitude.setFieldSI("altitude", 123.5);

        bool received = false;
        boost::asio::steady_timer timeout(context, 2s);
        timeout.async_wait([&](const boost::system::error_code &error) {
            if (!error) receiver.stop();
        });
        receiver.bind("GUIDE_ALTITUDE",
            [&](const pprzlink::Message &message, const pprzlink::ReceiveInfo &info) {
                const auto &peer = info.udpPeer.value();
                std::cout << "Reçu de " << peer.address << ':' << peer.port
                          << " : " << message.toString() << '\n';
                received = true;
                receiver.stop();
                timeout.cancel();
            });
        receiver.start();
        const auto destination = receiver.localEndpoint();
        const auto sent = sender.sendMessage(altitude, destination);
        std::cout << "Envoyé : " << sent << " octets vers le port "
                  << destination.port << '\n';
        context.run();
        if (!received) throw std::runtime_error("Aucun message UDP reçu dans le délai prévu");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

Reconfigurez, compilez et lancez :

```sh
cmake -S . -B build
cmake --build build -j4
./build/atelier messages.xml
```

Vous voyez l'envoi de 12 octets, puis la réception de `GUIDE_ALTITUDE` avec
`altitude=123.5` selon le format d'affichage du codec. Les ports changent
d'une exécution à l'autre.

Trois détails expliquent ce programme :

1. Le port local `0` demande au système un port disponible. `localEndpoint()`
   permet de connaître le port réellement attribué.
2. `sendMessage()` demande une destination IP/port explicite. Le destinataire
   PPRZLINK, ici 0, est une information distincte de cette destination réseau.
3. `bind()` conserve la lambda à appeler lorsque le message arrive. `start()`
   active la lecture et `context.run()` exécute les callbacks Asio sur le thread
   appelant. Aucun thread supplémentaire n'interroge les canaux.

Le timer empêche d'attendre indéfiniment. Asio attend les données ou l'échéance ;
aucune boucle avec `sleep_for()` n'est nécessaire. `stop()` annule les lectures
de ce récepteur ; l'annulation du timer permet à `context.run()` de terminer.

### Passer à deux programmes

Gardez le récepteur dans un programme et l'émetteur dans un autre. Faites
écouter le récepteur sur `{"127.0.0.1", 4242}`, puis utilisez cette même
destination à l'envoi. Pour écouter sur les interfaces réseau de la machine,
utilisez l'adresse locale `"0.0.0.0"` ; l'émetteur devra viser l'adresse IP
réelle du récepteur.

Si votre protocole répond au port d'origine, `info.udpPeer` donne la
destination de la réponse. D'autres applications, dont certaines configurations
Paparazzi, utilisent un port montant distinct : c'est une décision de votre
application, pas une déduction faite par la bibliothèque.

Le callback emprunte le `Message` et le `ReceiveInfo` pendant son invocation.
Copiez ces objets pour les conserver. Les vues `span`/`string_view` empruntées
ne doivent pas survivre au message qui les contient.
Le décodeur UDP abandonne les trames incomplètes à la fin d'un datagramme ;
il ne mélange pas les morceaux de deux datagrammes.

**À essayer :** envoyez trois altitudes et comptez les invocations de la lambda
avant d'arrêter le récepteur au troisième message. Vous pouvez aussi envoyer `GUIDE_SETTING` après avoir rempli
ses deux champs : le mécanisme de transport reste identique.

### Les trois modes de l'atelier

L'exemple [WorkshopUdp.cpp](pprzlink/examples/clients/WorkshopUdp.cpp) propose
`both`, `emitter` et `receiver`. Pour l'utiliser comme `main.cpp` :

```sh
cp "$PPRZLINK_CPP/pprzlink/examples/clients/WorkshopUdp.cpp" main.cpp
cmake --build build -j4
./build/atelier messages.xml both
```

Dans deux terminaux, lancez le mode `receiver`, puis `emitter`. Le récepteur
écoute sur 127.0.0.1:4242 pendant 30 secondes, ou jusqu'à Ctrl+C.

### Choisir les messages et leurs émetteurs

Le callback simple reçoit seulement le message. Pour écouter tous les types,
utilisez `receiver.bind(pprzlink::ALL, lambda)`. Le récepteur conserve le binding ;
il n'est pas nécessaire de garder l'identifiant retourné.

```cpp
receiver.bind("GUIDE_ALTITUDE", {.senderId = 42},
    [](const pprzlink::Message &message) {
        std::cout << message.getFieldSI("altitude") << " m\n";
    });
```

`senderId` sélectionne l'émetteur PPRZLINK. Pour sélectionner l'origine réseau,
utilisez `udpPeer` (IP et port), `udpAddress` (IP seule) ou `udpPort` (port seul).
Ces critères sont distincts et peuvent être combinés. `receiverId`, `className`,
`classId`, `componentId` et un prédicat `where` complètent la sélection.
Voir le [contrat des filtres](API_USAGE.md#réception-réactive--abonnements-et-filtres).

## 7. Passer à une liaison série

Sur une liaison série, deux objets coopèrent : le **Device** lit et écrit
des octets ; le **Transport** transforme ces octets en messages PPRZLINK.
Ici, nous utiliserons `BoostSerialPortDevice` et `PprzTransport`.

Le projet reste lié à `pprzlink::io`. Cet exemple demande un port série
accessible et un correspondant utilisant le même XML, le protocole PPRZ v2
et les mêmes paramètres série. Remplacez tout `main.cpp` par :

```cpp
#include <pprzlink/BoostSerialPortDevice.h>
#include <pprzlink/PprzTransport.h>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "Usage: atelier messages.xml port-serie\n";
        return 2;
    }
    try {
        using namespace std::chrono_literals;
        const pprzlink::MessageDictionary dictionary(argv[1]);
        boost::asio::io_context context;

        using Serial = pprzlink::BoostSerialPortDevice;
        auto device = std::make_unique<Serial>(context, argv[2]);
        device->setBaudrate(Serial::Baudrate(57600));
        device->setDataBits(Serial::DataBits(8));
        device->setParity(Serial::Parity(Serial::Parity::none));
        device->setStopBits(Serial::StopBits(Serial::StopBits::one));
        device->setFlowcontrol(Serial::Flowcontrol(Serial::Flowcontrol::none));

        pprzlink::PprzTransport transport(std::move(device), dictionary);

        pprzlink::Message altitude(dictionary.getDefinition("GUIDE_ALTITUDE"));
        altitude.setSenderId(42);
        altitude.setReceiverId(0);
        altitude.setFieldSI("altitude", 123.5);
        transport.sendMessage(altitude);

        bool received = false;
        boost::asio::steady_timer timeout(context, 10s);
        timeout.async_wait([&](const boost::system::error_code &error) {
            if (!error) transport.stop();
        });
        transport.bind(pprzlink::ALL, [&](const pprzlink::Message &message) {
            std::cout << message.toString() << '\n';
            received = true;
            transport.stop();
            timeout.cancel();
        });
        transport.start();
        context.run();
        if (!received) throw std::runtime_error("Aucun message série reçu en 10 secondes");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

Compilez comme précédemment, puis adaptez le nom du périphérique :

```sh
./build/atelier messages.xml /dev/ttyUSB0
```

Le programme envoie une altitude, puis attend un message du correspondant.
Il ne reçoit pas automatiquement une copie de son propre envoi.

`std::move(device)` transfère au transport la propriété du périphérique :
le transport le détruira à sa propre destruction. La variable `device` est
alors vide ; configurez donc le port avant ce transfert.

Comme UDP, la réception série utilise Boost.Asio. `context.run()` traite les
lectures et les abonnements ; les callbacks applicatifs sont appelés après la
libération du mutex d'I/O du périphérique. Une lambda peut envoyer une réponse,
se désabonner ou arrêter son récepteur. `stop()` conserve les bindings pour un
éventuel `start()` ultérieur, sans arrêter un contexte partagé avec d'autres canaux.

L'ordre des déclarations est intentionnel : le transport est détruit avant
le contexte et le dictionnaire qu'il utilise. Nous reviendrons à ces durées
de vie à l'étape 12.

### Réagir au bon message

Un transport peut recevoir plusieurs types de messages. Dans une application
qui poursuit sa réception, remplacez l'abonnement `ALL` de l'exemple par :

```cpp
transport.bind("GUIDE_SETTING", {.receiverId = 42},
    [](const pprzlink::Message &message) {
        const auto aircraft = message.getFieldAs<int>("ac_id");
        const auto value = message.getField<float>("value");
        std::cout << "Réglage pour " << aircraft << " : " << value << '\n';
    });
```

La bibliothèque ne décide pas à votre place si un message reçu doit être
appliqué à votre avion. Vérifiez le type de message et les informations
d'adressage attendues par votre application.

Pour un exemple série complet qui émet une altitude puis répond à un PING,
consultez [SerialAircraft.cpp](pprzlink/examples/clients/SerialAircraft.cpp),
avec son propre fichier `client_messages.xml`.

### Essayer deux processus sur des pseudo-terminaux

Sur Linux, `link++ -socat start` crée deux ports virtuels reliés et rend la main.
Il sert ici à gérer la liaison : les deux programmes de l'atelier utilisent
directement la bibliothèque, sans lancer l'agent de télémétrie ni le bus Ivy.
Le programme `socat` doit être installé et accessible sur PATH.

L'exemple [PtyAgent.cpp](pprzlink/examples/clients/PtyAgent.cpp) est symétrique :
le **même programme** tourne dans les deux processus. Chaque instance publie
`GUIDE_ALTITUDE` une fois par seconde et affiche les altitudes qu'elle reçoit.
Elle fait tourner son contexte Asio et utilise uniquement les en-têtes publics
de `pprzlink::io`. La copie du catalogue de l'atelier s'appelle
[guide_messages.xml](pprzlink/examples/clients/guide_messages.xml).

Les noms **Port A** et **Port B** sont des étiquettes. Les deux extrémités
lisent et écrivent ; aucune n'est réservée au sol, à l'avion, au demandeur ou
au répondeur. Vous pouvez permuter les ports sans changer les programmes.

Depuis la **racine du dépôt Paparazzi**, compilez les exemples une fois dans
un répertoire distinct de celui du `make` habituel :

```sh
cmake -S sw/ext/pprzlink/lib/v2.0/C++ -B var/build/pprzlink-pty \
  -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Debug \
  -DPPRZLINK_BUILD_EXAMPLES=ON -DPPRZLINK_BUILD_LINK=ON -DBUILD_TESTING=OFF
cmake --build var/build/pprzlink-pty \
  --target link++ pty_agent -j4
./var/build/pprzlink-pty/apps/link/link++ -socat start
```

Le démarrage affiche **Port A** et **Port B**, par exemple `/dev/pts/12` et
`/dev/pts/13`. Dans un premier terminal, ouvrez l'un des ports avec
l'identifiant 1 :

```sh
./var/build/pprzlink-pty/pty_agent \
  sw/ext/pprzlink/lib/v2.0/C++/pprzlink/examples/clients/guide_messages.xml \
  /dev/pts/12 1
```

Dans un second terminal, ouvrez l'autre port avec l'identifiant 2 :

```sh
./var/build/pprzlink-pty/pty_agent \
  sw/ext/pprzlink/lib/v2.0/C++/pprzlink/examples/clients/guide_messages.xml \
  /dev/pts/13 2
```

Les identifiants doivent être différents ; ils appartiennent aux messages,
pas aux ports. L'agent 1 publie `124.5 m` et reçoit `125.5 m` de l'agent 2.
L'agent 2 publie `125.5 m` et reçoit `124.5 m` de l'agent 1. Les messages sont
envoyés avec le destinataire de diffusion 255 ; chaque instance ignore ses
propres messages et les messages adressés à un autre identifiant.

```text
Agent 1 TX GUIDE_ALTITUDE 124.5 m
Agent 1 RX from 2: GUIDE_ALTITUDE 125.5 m
```

Les processus continuent jusqu'à **Ctrl+C**. Les adresses `/dev/pts/N` sont
temporaires : utilisez les valeurs réellement affichées par `start`.

À la fin, arrêtez les deux agents avec Ctrl+C, puis la liaison :

```sh
./var/build/pprzlink-pty/apps/link/link++ -socat stop
```

Vous pouvez remplacer `guide_messages.xml` par le `messages.xml` créé à
l'étape 1 : les définitions sont identiques. Cet exemple se compile
également avec le SDK installé, en copiant le répertoire `clients` comme
décrit dans [API_USAGE.md](API_USAGE.md). Il se lie uniquement à
`pprzlink::io`, sans Ivy.

Pour travailler spécifiquement la requête et la réponse, les exemples
[PtyRequester.cpp](pprzlink/examples/clients/PtyRequester.cpp) et
[PtyResponder.cpp](pprzlink/examples/clients/PtyResponder.cpp) restent
disponibles. Compilez les cibles `pty_requester` et `pty_responder`, puis
lancez chaque programme avec `messages.xml` et l'un des ports ; ils échangent
`GUIDE_ALTITUDE_REQ` et `GUIDE_ALTITUDE` et terminent après la réponse.
Leurs rôles applicatifs n'imposent pas non plus le choix de Port A ou Port B.

## 8. Recevoir et publier sur Ivy

Ivy est un bus de messages texte. Avec `IvyLink`, vous vous abonnez aux
messages qui vous intéressent : une fonction de rappel, ou *callback*, est
appelée lorsqu'un message correspondant arrive.

### Ajouter le composant Ivy

Cette branche nécessite Ivy 3.18 ou plus et son interface native C++
`ivy-cpp`. Le [README](README.md#build) précise l'installation Ivy utilisée
pour les validations et la configuration d'un préfixe privé.

Une fois ces dépendances installées, vérifiez leur découverte et reconstruisez
le SDK :

```sh
pkg-config --modversion ivy-cpp ivy-c

cmake -S "$PPRZLINK_CPP" -B "$PPRZLINK_ATELIER/build-lib" \
  -DPPRZLINK_WITH_IVY=ON
cmake --build "$PPRZLINK_ATELIER/build-lib" -j4
cmake --install "$PPRZLINK_ATELIER/build-lib" \
  --prefix "$PPRZLINK_ATELIER/sdk"
```

Dans le `CMakeLists.txt` du projet, remplacez le composant `io` par `ivy` et
la cible `pprzlink::io` par `pprzlink::ivy`, puis reconfigurez avec
`cmake -S . -B build`.

### Recevoir une altitude par abonnement

Remplacez `main.cpp` par ce programme complet :

```cpp
#include <pprzlink/IvyLink.h>
#include <chrono>
#include <exception>
#include <iostream>
#include <system_error>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "Usage: atelier messages.xml\n";
        return 2;
    }
    try {
        const pprzlink::MessageDictionary dictionary(argv[1]);
        bool received = false;
        pprzlink::IvyLink link(dictionary, "guide-recepteur",
                               "127.255.255.255:2010");

auto subscription = link.subscribeMessage("GUIDE_ALTITUDE",
            [&](std::string sender, pprzlink::Message message) {
                std::cout << "Altitude de " << sender << " : "
                          << message.getFieldSI("altitude") << " m\n";
                received = true;
                link.stop();
            });

        auto timeout = link.getBus().bind_event(
            [&](std::chrono::milliseconds) { link.stop(); },
            ivy::after(std::chrono::seconds(10)));
        if (!timeout) {
            throw std::system_error(timeout.error(), "Temporisation Ivy");
        }

        std::cout << "Attente sur Ivy pendant 10 secondes..." << std::endl;
        link.run();
        if (!received) {
            std::cerr << "Aucune altitude reçue\n";
        }
        return received ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

Le callback reçoit **un message complet**, avec les champs déjà décodés.
Son argument `sender` est le nom textuel de l'expéditeur sur Ivy.

Conservez `subscription` pendant toute la durée souhaitée de l'abonnement.
Cet objet se désabonne à sa destruction. Si vous ignorez le résultat de
`subscribeMessage()`, l'abonnement temporaire disparaît immédiatement.

Dans ce programme, `link.run()` exécute la boucle Ivy sur le thread appelant.
Le callback appelle `stop()`, puis `run()` rend la main. La temporisation
arrête également la boucle si aucun message n'arrive. Les deux chemins
permettent aux objets locaux d'être détruits proprement.

### Publier depuis un second programme

Créez `emetteur_ivy.cpp` à côté de `main.cpp` :

```cpp
#include <pprzlink/IvyLink.h>
#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <thread>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "Usage: emetteur_ivy messages.xml\n";
        return 2;
    }
    try {
        const pprzlink::MessageDictionary dictionary(argv[1]);
        pprzlink::Message altitude(dictionary.getDefinition("GUIDE_ALTITUDE"));
        altitude.setSenderId(42);
        altitude.setFieldSI("altitude", 123.5);

        pprzlink::IvyLink link(dictionary, "guide-emetteur",
                               "127.255.255.255:2010", true);
        std::cout << "Démarrez le récepteur, puis appuyez sur Entrée.\n";
        std::cin.get();

        // Attendre que l'abonnement du récepteur soit visible sur le bus.
        bool ready = false;
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::seconds(5);
        while (!ready && std::chrono::steady_clock::now() < deadline) {
            const auto peer = link.getBus().find_application("guide-recepteur");
            if (peer && *peer) {
                const auto bindings = link.getBus().application_regexps(**peer);
                ready = bindings && !bindings->empty();
            }
            if (!ready) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        if (!ready) {
            throw std::runtime_error("Abonnement du récepteur introuvable");
        }
        link.sendMessage(altitude);

        const auto status = link.getBus().take_callback_error();
        if (!status) throw std::system_error(status.error(), "Callback Ivy");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

Ajoutez ces deux lignes à `CMakeLists.txt`, puis recompilez :

```cmake
add_executable(emetteur_ivy emetteur_ivy.cpp)
target_link_libraries(emetteur_ivy PRIVATE pprzlink::ivy)
```

Lancez d'abord `./build/emetteur_ivy messages.xml`. Dans un second terminal
placé dans `projet`, lancez `./build/atelier messages.xml`, puis appuyez sur
Entrée dans le premier terminal avant la fin des dix secondes d'attente.
Le récepteur doit afficher `Altitude de 42 : 123.5 m`.

Le paramètre `true` du constructeur de l'émetteur démarre la boucle Ivy sur
un thread appartenant au lien. Cela lui permet de découvrir le récepteur
pendant que le thread principal attend au clavier. Dans ce mode, n'appelez
pas `run()` : la boucle fonctionne déjà. Le destructeur arrête et rejoint
ce thread.

Ivy diffuse les publications aux abonnés présents ; l'envoi ne conserve pas
un message pour un récepteur qui s'abonnerait plus tard. C'est pourquoi cet
exemple attend de voir l'abonnement avant de publier. Pour un échange
automatique sans clavier, voyez [IvyRoundTrip.cpp](pprzlink/examples/IvyRoundTrip.cpp).

Le nom d'application `guide-emetteur` identifie le participant Ivy.
`setSenderId(42)` identifie l'expéditeur dans le message PPRZLINK : ce sont
deux noms différents. Sur Ivy, vous pouvez aussi écrire
`setSenderId(std::string("station-sol"))`. Un envoi binaire exigera un
identifiant numérique entre 0 et 255, éventuellement sous forme de texte
numérique.

Enfin, le format texte Ivy n'encode pas les destinataire et composant de
l'en-tête binaire. `setReceiverId()` ne sélectionne donc pas un destinataire
sur le bus Ivy. Le contenu XML et les règles de l'application déterminent
comment traiter une commande.

## 9. Faire une requête et attendre sa réponse

Une publication annonce une information. Une requête demande une réponse
associée à une demande précise. `IvyLink` fournit cette association pour les
messages dont le nom se termine par `_REQ`.

Notre XML contient déjà `GUIDE_ALTITUDE_REQ`. La réponse attendue est
`GUIDE_ALTITUDE`, c'est-à-dire le même nom sans le suffixe `_REQ`. Ce
mécanisme est propre à l'adaptateur Ivy présenté ici ; un couple PING/PONG
sur série demande sa propre logique applicative.

### Transformer le récepteur en répondeur

Reprenez le `main.cpp` Ivy. Remplacez uniquement l'abonnement à
`GUIDE_ALTITUDE` par :

```cpp
auto subscription = link.subscribeRequestAnswerer(
    dictionary.getDefinition("GUIDE_ALTITUDE_REQ"),
    [&](std::string sender, pprzlink::Message request) {
        (void)request; // Cette requête n'a aucun champ.
        std::cout << "Demande reçue de " << sender << '\n';
        pprzlink::Message answer(dictionary.getDefinition("GUIDE_ALTITUDE"));
        answer.setSenderId(42);
        answer.setFieldSI("altitude", 123.5);
        received = true;
        return answer;
    });
```

Le callback **retourne** le message de réponse. `IvyLink` l'envoie avec
l'identifiant de corrélation approprié ; vous n'appelez pas `sendMessage()`
pour cette réponse. Ce répondeur reste actif jusqu'à la temporisation du
programme, et peut traiter plusieurs demandes pendant cet intervalle.

### Transformer l'émetteur en demandeur

Dans `emetteur_ivy.cpp`, ajoutez `<future>` et `<memory>`. Remplacez la
construction du message `altitude`, avant celle du lien, par :

```cpp
auto result = std::make_shared<std::promise<double>>();
auto future = result->get_future();
pprzlink::Message request(dictionary.getDefinition("GUIDE_ALTITUDE_REQ"));
request.setSenderId(std::string("station-sol"));
```

Gardez le lien Ivy, l'attente au clavier et la découverte de l'abonnement.
À la place de `link.sendMessage(altitude)`, écrivez :

```cpp
const long pending = link.sendRequest(request,
    [result](std::string, pprzlink::Message answer) {
        try {
            result->set_value(answer.getFieldSI("altitude"));
        } catch (...) {
            result->set_exception(std::current_exception());
        }
    });

if (future.wait_for(std::chrono::seconds(3)) != std::future_status::ready) {
    link.UnbindMessage(pending);
    throw std::runtime_error("La requête est restée sans réponse");
}
std::cout << "Altitude demandée : " << future.get() << " m\n";
```

Recompilez les deux programmes et utilisez le même ordre de lancement qu'à
l'étape précédente. Le demandeur doit afficher `Altitude demandée : 123.5 m`.

`sendRequest()` installe l'attente puis envoie la demande. À la première
réponse valide, cette attente est automatiquement supprimée. La bibliothèque
ne fixe pas de délai maximal : ici, le programme attend trois secondes puis
annule l'abonnement avec `UnbindMessage()` si nécessaire.

Le callback s'exécute sur le thread Ivy de l'émetteur. La paire
`std::promise` / `std::future` transmet le résultat au thread principal.
La capture par `shared_ptr` maintient la promesse en vie même si un callback
avait déjà commencé au moment de l'annulation. Se désabonner n'interrompt
pas une fonction de rappel déjà en cours.

**À essayer :** ne lancez pas le répondeur. Vous verrez d'abord échouer
l'attente de son abonnement. Pour observer le délai de réponse lui-même,
faites démarrer un participant `guide-recepteur` abonné à `GUIDE_ALTITUDE`
comme à l'étape 8 : il est découvert, mais ne répond pas aux requêtes.

## 10. Utiliser une radio XBee

Revenez au projet série de l'étape 7 et à la cible `pprzlink::io`.
Le `Device` reste un port série ; c'est le transport qui change pour produire
les trames de l'API XBee. L'API prise en charge utilise **AP=1**, sans
échappement.

L'exemple suivant concerne une radio 802.15.4. Ajoutez
`<pprzlink/XbeeTransport.h>` et `<optional>`, puis remplacez la construction
du `PprzTransport` par :

```cpp
pprzlink::XbeeTransport transport(std::move(device), dictionary);

pprzlink::XbeeConfiguration configuration;
configuration.localAddress = 0x100;
configuration.targetBaudrate = std::nullopt;
transport.startInitialization(configuration);

// Remplacez aussi l’envoi direct de l’étape 7 par :
// transport.onReady([&] { transport.sendMessage(altitude); });
// Conservez les bind(), start() et context.run() de cette étape.
```

Dans cette configuration, le modem doit déjà communiquer à la vitesse
configurée sur le port série, 57600 bauds dans notre programme.
`targetBaudrate = std::nullopt` conserve cette vitesse. L'initialisation
configure l'adresse locale et le mode API ; le canal reste inchangé lorsque
`configuration.channel` n'est pas renseigné.

`start()` active le dialogue AT ; l'arrivée des octets et les échéances Asio
vérifient les réponses et font avancer l'initialisation. Placez l'envoi dans
`transport.onReady([&] { transport.sendMessage(altitude); });`, après la création
du message `altitude`. Les délais et temps de garde ne demandent aucune interrogation
périodique. Une erreur est signalée par `onError()` ou remontée dans la boucle. Attendez la fin de cette phase avant d'envoyer
des messages, et laissez le transport seul utiliser le périphérique pendant
l'initialisation.

Si vous utilisez simplement `startInitialization()` sans configuration, le
comportement est différent : l'initialiseur détecte la vitesse, vise 57600
bauds et peut enregistrer les réglages courants avec `ATWR` lors d'un
changement de vitesse. Choisissez ce comportement intentionnellement.
Si le modem est déjà configuré, vous pouvez aussi omettre l'initialisation ;
dans ce cas `isReady()` suppose cette préparation, sans interroger le modem.

### Choisir la destination radio

L'envoi habituel utilise le destinataire PPRZLINK comme adresse radio.
Vous pouvez aussi indiquer une adresse radio distincte :

```cpp
altitude.setReceiverId(0);
transport.sendMessageTo16(altitude, 0x0100);
```

Ici, 0 est le destinataire logique dans l'en-tête du message et `0x0100`
l'adresse de la radio distante. Adaptez celle-ci à votre installation.
`sendMessageTo64()` permet un adressage radio sur 64 bits.

La variante `XbeeTransport::Api::Series868`, passée en troisième argument au
constructeur, sélectionne les trames étendues. Elle ne configure pas à elle
seule un modem 868 MHz. Les essais logiciels de cette branche couvrent ces
trames ; la validation sur matériel 868 reste à effectuer, comme indiqué
dans le [rapport de validation](LINK_CPP_VALIDATION.md).

### Distinguer écriture série et livraison radio

La valeur renvoyée par `sendMessage()` est un nombre d'octets écrits vers
le modem. Pour connaître le résultat radio, installez un callback **avant**
l'envoi. Ajoutez `<variant>` :

```cpp
transport.setStatusCallback([](const pprzlink::XbeeTransport::RadioStatus &event) {
    if (const auto *status =
            std::get_if<pprzlink::XbeeTransport::TransmitStatus>(&event)) {
        std::cout << "Trame radio " << static_cast<unsigned>(status->frameId)
                  << ", statut " << static_cast<unsigned>(status->status) << '\n';
    }
});

transport.sendMessage(altitude);
const auto frameId = transport.getLastFrameId();
std::cout << "Identifiant radio envoyé : " << static_cast<unsigned>(frameId) << '\n';
```

Activez la réception avec `start()` et faites tourner Asio avec `context.run()`.
Le traitement de la réception déclenche aussi les callbacks de statut, même
si aucun message applicatif n'est rendu. Un statut de transmission égal à
zéro indique le succès radio ; il ne confirme pas que l'application distante
a exécuté une commande.

Pour suivre plusieurs envois, associez chaque statut à son `frameId`.
Les identifiants automatiques parcourent 1 à 255. Une application avancée
peut les gérer elle-même avec `sendMessageWithId()` : elle doit conserver
les identifiants en attente et éviter de les réutiliser prématurément. La
valeur 0 désactive la réponse de statut. Le transport ne gère pas les
réessais applicatifs ; définir un délai et décider d'une retransmission
relèvent du programme utilisateur.

Les messages radio reçus portent aussi des métadonnées :

```cpp
transport.bind(pprzlink::ALL,
    [](const pprzlink::Message &message, const pprzlink::ReceiveInfo &info) {
        std::cout << message.toString() << '\n';
        if (info.xbee) {
            const auto &radio = *info.xbee;
            std::cout << "Adresse radio source : " << radio.sourceAddress << '\n';
            if (radio.hasRssi) std::cout << "RSSI : " << -static_cast<int>(radio.rssi) << " dBm\n";
        }
    });
```

L'adresse radio source et l'identifiant d'expéditeur PPRZLINK décrivent deux
niveaux d'adressage. Le RSSI n'est pas présent dans toutes les variantes,
d'où le contrôle de `hasRssi`. La charge utile RF acceptée par ce transport
est limitée à 100 octets, en-tête PPRZLINK compris.

## 11. Construire un outil générique ou un adaptateur

### Afficher un message dont on ne connaît pas les champs à l'avance

Un enregistreur ou une interface de diagnostic peut parcourir la définition
au lieu de coder le nom de chaque champ. Revenez au programme sans I/O de
l'étape 2, ajoutez `<pprzlink/TextCodec.h>`, puis :

```cpp
const auto &schema = altitude.getDefinition();
for (std::size_t i = 0; i < schema.getNbFields(); ++i) {
    const auto &field = schema.getField(i);
    std::cout << field.getName() << " (" << field.getType().toString() << ") = ";
    pprzlink::writeDebugField(std::cout, altitude.getRawValue(i));
    std::cout << '\n';
}
```

Les indices suivent l'ordre du XML. Ce traitement fonctionne aussi avec un
message reçu d'un transport, quel que soit son nom, dès lors que tous ses
champs ont une valeur.

Pour découvrir les messages d'une classe, vous pouvez aussi écrire :

```cpp
for (const auto &schema : dictionary.getMsgsForClass("guide")) {
    std::cout << schema.getName() << " : " << schema.getNbFields() << " champs\n";
}
```

Si votre traitement doit examiner le type C++ de chaque valeur,
`message.getField(nom)` sans paramètre de template fournit une référence
constante au `std::variant` de stockage, utilisable avec `std::visit`.
Cette référence appartient au message : ne la conservez pas après sa
destruction ou le remplacement du champ.

Les métadonnées chargées comprennent les noms, types, identifiants, le
format d'affichage, le mode de routage `link`, les unités et leurs coefficients
explicites. `field.getUnit()` fournit l'unité XML et `field.getSIUnit()` celle
de l'accès SI. Les descriptions textuelles ne sont pas exposées. Seuls
`getFieldSI()` et `setFieldSI()` demandent une conversion d'unité ; les appels
natifs et les codecs continuent à utiliser les valeurs XML.

### Brancher un autre flux d'octets

Supposons que votre programme possède déjà un flux TCP ou un canal de
simulation. Deux approches sont possibles :

- injecter les octets dans `PprzFrameDecoder`, comme à l'étape 5, et utiliser
  `encodePprzFrame()` à l'envoi ;
- écrire une classe dérivée de `Device`, puis la confier à `PprzTransport`.

La première approche laisse toute la gestion des I/O à votre programme.
La seconde permet de réutiliser le transport existant. Pour un simple
bouclage de démonstration, la bibliothèque fournit déjà `MemoryDevice`.
Ajoutez `<pprzlink/MemoryDevice.h>`, `<pprzlink/PprzTransport.h>` et `<memory>`,
puis placez dans le bloc `try`, après le remplissage de `altitude` :

```cpp
boost::asio::io_context context;
pprzlink::PprzTransport loopback(
    std::make_unique<pprzlink::MemoryDevice>(context), dictionary);
loopback.bind("GUIDE_ALTITUDE", [&](const pprzlink::Message &message) {
    std::cout << "Bouclage : " << message.toString() << '\n';
    loopback.stop();
});
loopback.start();
loopback.sendMessage(altitude);
context.run();
```

Vous retrouvez votre message parce que les écritures en mémoire deviennent
de l'entrée, annoncée sur le contexte Asio. Il suffit de `pprzlink::core`,
sans socket ni port série. L'application n'a pas à gérer les fragments :
la lambda reçoit uniquement un message complet. Pour un simulateur avancé,
`MemoryDevice::feed()` peut injecter des octets dans le flux.

Un adaptateur réel implémente `getExecutor()`, `startReception()`,
`stopReception()` et `setReceiveCallback()`. Ses notifications sont appelées
hors des verrous d'I/O et les buffers survivent aux lectures annulées.
`readAll()` consomme les octets disponibles ; `writeBuffer()` écrit le tampon
entier ou lève une exception. `MemoryDevice` permet d'apprendre la réception
sans avoir à écrire cette mécanique.

Pour les fichiers ou FIFO sous Unix, la bibliothèque fournit déjà
`PosixFileDevice`, dans le composant `io`. Pour changer la vitesse UART pendant
une initialisation XBee, un adaptateur doit implémenter l'interface plus
spécifique `SerialDevice` et sa méthode `resetBaudrate()`.

`PprzTransport` et `XbeeTransport` dérivent tous deux de `Transport` : un
programme peut choisir l'un ou l'autre avec un `std::unique_ptr<Transport>`.
`UdpTransport` et `IvyLink` ont leurs propres opérations d’envoi, mais les
quatre modes partagent `Receiver`, `bind()`, `start()` et `stop()`.

## 12. Organiser une application durable et diagnostiquer les erreurs

### Respecter la durée de vie des objets

Le modèle utilisé dans les exemples est simple : créez le dictionnaire,
puis le contexte d'I/O si nécessaire, puis les objets de communication.
La destruction se fera dans l'ordre inverse.

| Objet | Ce qu'il conserve | Conséquence pratique |
| --- | --- | --- |
| `Message` | Sa propre définition et ses valeurs | Il peut être copié et conservé après la réception. |
| `PprzFrameDecoder`, transports et `IvyLink` | Une référence au dictionnaire | Le dictionnaire doit rester vivant pendant leur utilisation. |
| `PprzTransport`, `XbeeTransport` | La propriété exclusive du `Device` | Configurez le périphérique avant `std::move`. |
| Périphérique série et transport UDP | Le contexte Asio fourni | Détruisez-les avant le contexte. |
| Abonnement Ivy | Le callback et ses captures | Gardez l'abonnement actif, et ses données capturées valides. |

Pour les transports binaires, effectuez les appels à un même transport
depuis un seul thread, ou sérialisez-les explicitement. Le fait que le
périphérique série protège ses propres opérations ne rend pas le transport
entier utilisable simultanément sans coordination.

Avec Ivy en mode threadé, les callbacks exécutent sur le thread de la boucle
Ivy. Utilisez une file, un mutex ou une promesse pour communiquer avec le
reste de votre programme. Déclarez l'état capturé avant le lien pour qu'il
lui survive. Arrêtez les autres appelants avant de détruire le lien ; ne
détruisez jamais le lien depuis l'un de ses callbacks. `stop()` peut en
revanche y être appelé.

En mode `run()`, une erreur de callback est contrôlée lorsque la boucle rend
la main. En mode threadé, inspectez aussi `getBus().take_callback_error()`
pour détecter ces erreurs ; ce résultat suit le contrat d'erreur natif Ivy.

### Séparer le traitement métier de la réception

Après les premiers essais, extrayez le traitement dans une fonction. Celle-ci
reçoit déjà un message complet et peut être essayée avec un message construit
en mémoire :

```cpp
void traiterAltitude(const pprzlink::Message &message)
{
    if (message.getDefinition().getName() != "GUIDE_ALTITUDE") return;
    const double metres = message.getFieldSI("altitude");
    std::cout << "Altitude traitée : " << metres << " m\n";
}
```

Appelez-la avec `received->message` dans une boucle UDP/série ou avec
`message` dans un callback Ivy. Le calcul métier n'a ainsi pas besoin de
connaître le transport. Placez cette fonction avant `main()` si vous
l'ajoutez à l'un des programmes du guide.

### Comprendre ce que signale une erreur

Les exemples terminent le programme lorsqu'une exception remonte jusqu'à
`main()`. Dans un service continu, vous choisirez quelles erreurs de contenu
peuvent être journalisées avant de poursuivre et quelles erreurs d'I/O
nécessitent une reconnexion ou un arrêt.

| Symptôme | Première vérification |
| --- | --- |
| Fichier XML introuvable | Le chemin est interprété depuis le répertoire de lancement du programme. |
| `no_such_message` ou `no_such_class` | Le XML chargé contient-il les noms et identifiants attendus ? |
| `no_such_field` | Le nom du champ correspond-il exactement à celui du XML ? |
| `field_has_no_value` | Tous les champs ont-ils été remplis avant lecture ou sérialisation ? |
| `field_type_mismatch` | Le type de `getField<T>()` correspond-il au XML ? Faut-il `getFieldAs<T>()` ? |
| `field_conversion_error` | La valeur tient-elle dans le type XML après conversion et, pour un setter SI entier, arrondi ? Le setter natif refuse les fractions. |
| `field_unit_error` | `canConvertSI()` est-il vrai ? Vérifier les unités/coefficient XML et la correspondance dans `UnitAliases.cpp`. |
| `std::length_error` | Taille de tableau fixe incorrecte, tableau trop long ou message trop grand ? |
| Aucun callback de réception série | Le contexte Asio tourne-t-il ? Le correspondant émet-il, avec le bon débit et le bon protocole ? |
| Rien ne déclenche le callback Ivy | Même domaine Ivy, abonnement encore vivant, boucle active et publication après découverte ? |
| Requête Ivy sans réponse | Le répondeur est-il abonné à la définition `_REQ` et retourne-t-il le bon type de réponse ? |
| XBee écrit des octets mais rien n'arrive | Destination radio correcte, modem en AP=1, statuts TX traités ? |

Toutes les erreurs ne passent pas par `pprzlink_exception` : les erreurs de
conversion, de type et de longueur ont d'autres bases standard. Le
`catch (const std::exception &)` des exemples couvre ces familles ; dans
votre application, utilisez des captures plus précises lorsque le traitement
diffère.

Le décodeur PPRZ élimine les octets parasites et les trames dont la longueur
ou le checksum est invalide. Un contenu invalide dans une trame correctement
délimitée peut lever une exception après consommation de cette trame. Un
appel ultérieur peut alors poursuivre la lecture.

Pour observer ce qui se passe, utilisez les statistiques du décodeur ou du
transport. Cet extrait suppose un objet `transport` binaire existant :

```cpp
const auto &stats = transport.getStatistics();
std::cout << "Messages reçus : " << stats.receivedMessages
          << ", erreurs de checksum : " << stats.checksumErrors
          << ", erreurs de décodage : " << stats.decodingErrors << '\n';
```

Des checksums corrects ne garantissent pas que deux XML donnent le même sens
aux octets : un ordre de champs différent peut produire des valeurs erronées
sans modifier la longueur de trame. Vérifiez toujours la compatibilité des
définitions aux deux extrémités.

### Relier l'atelier à une application Paparazzi

Pour utiliser ces acquis dans votre projet, remplacez `messages.xml` par le
catalogue de votre configuration et choisissez les noms de messages qui y
existent. Le catalogue source du dépôt se trouve dans
[message_definitions/v1.0/messages.xml](../../../message_definitions/v1.0/messages.xml) ;
le `v1.0` de ce chemin désigne le catalogue et ne sélectionne pas le protocole
de transport utilisé par la bibliothèque C++.

Les règles de routage vers les avions, le suivi de présence, les délais de
réponse et les réessais appartiennent à l'application. L'agent
[link++](apps/link) montre une application complète qui relie les transports
au bus Ivy et applique ces règles.

Pour poursuivre avec des exemples déjà intégrés au dépôt :

- [IvyRoundTrip.cpp](pprzlink/examples/IvyRoundTrip.cpp) échange une altitude
  entre deux liens Ivy et attend la découverte de l'abonnement.
- [SerialAircraft.cpp](pprzlink/examples/clients/SerialAircraft.cpp) émet une
  altitude et répond à un PING sur une liaison série.
- [UdpRecorder.cpp](pprzlink/examples/clients/UdpRecorder.cpp) conserve l'origine
  réseau de chaque message reçu.
- [UdpSettingSender.cpp](pprzlink/examples/clients/UdpSettingSender.cpp) prépare
  plusieurs champs puis envoie une commande en UDP.

Ces exemples utilisent leurs propres XML. Leurs commandes de compilation et
d'exécution sont détaillées dans [API_USAGE.md](API_USAGE.md).
[architecture.md](architecture.md) permet ensuite d'approfondir les relations
entre les classes et les choix de conception.
