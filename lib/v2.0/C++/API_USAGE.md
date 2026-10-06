# API pprzlink++ : simplification par les exemples

Première itération réalisée le 29 septembre 2026. Les clients de référence sont
un récepteur Ivy, un simulateur d'avion série, un enregistreur UDP et un émetteur
de commande UDP. Ils utilisent
uniquement les en-têtes publics de la bibliothèque. Leur répertoire peut être
copié dans un autre projet et compilé avec le SDK installé.

| Exemple | Parcours et limite qu'il permet d'observer |
| --- | --- |
| [SIUnits.cpp](pprzlink/examples/clients/SIUnits.cpp) | Consulter les unités XML/SI, arrondir une altitude vers l'entier XML, convertir les températures/tableaux et conserver l'encodage natif, sans I/O. |
| [IvyReceiver.cpp](pprzlink/examples/clients/IvyReceiver.cpp) | S'abonner, lire une altitude en mètres par `getFieldSI()`, arrêter la boucle. L'abonnement est possédé par une variable locale ; le délai utilise encore l'API native Ivy. |
| [SerialAircraft.cpp](pprzlink/examples/clients/SerialAircraft.cpp) | Ouvrir un port, publier une altitude en mètres par `setFieldSI()`, répondre à PING. L'application fournit et fait tourner son contexte Asio. |
| [UdpRecorder.cpp](pprzlink/examples/clients/UdpRecorder.cpp) | Recevoir trois messages avec leur adresse source et leur taille. Aucun bus Ivy n'est démarré ni lié au programme. |
| [UdpSettingSender.cpp](pprzlink/examples/clients/UdpSettingSender.cpp) | Remplir les deux champs d'une commande dans un seul `setField()`, puis envoyer son datagramme à un pair de test local. |
| [PtyRequester.cpp](pprzlink/examples/clients/PtyRequester.cpp) et [PtyResponder.cpp](pprzlink/examples/clients/PtyResponder.cpp) | Échanger une requête et une altitude entre deux processus sur des ports virtuels, avec le XML du guide. |
| [PtyAgent.cpp](pprzlink/examples/clients/PtyAgent.cpp) | Lancer le même agent aux deux extrémités : chaque processus publie et reçoit une altitude, sans rôle lié au port. |

Ces exemples sont de petits clients exécutables de référence, pas des services
complets de production. Les clients série/UDP/Ivy initiaux sont bornés à dix secondes.
Utiliser le XML fourni avec un pair de test qui charge les mêmes définitions.

Les deux exemples PTY utilisent `guide_messages.xml` et attendent jusqu'à
60 secondes, pour permettre leur lancement manuel dans deux terminaux.
`pty_agent` utilise le même XML et continue jusqu'à Ctrl+C. Lancez deux
instances avec des identifiants différents, sur **Port A** et **Port B**
indifféremment ; les deux processus envoient et reçoivent.
`link++ -socat start` prépare leurs ports ; `link++ -socat stop` les supprime.
Le [guide d'utilisation](guide_d_utilisation.md#essayer-deux-processus-sur-des-pseudo-terminaux)
donne les commandes de compilation et de lancement. Ces clients se lient
uniquement à `pprzlink::io` et peuvent également utiliser le SDK sans Ivy.

## Contrats retenus

1. Les écritures numériques vérifient la plage et refusent une partie
   fractionnaire lors d'une conversion vers un entier. Une erreur ne remplace
   pas la valeur déjà présente. Les lectures `getField<T>()` restent strictes ;
   `getFieldAs<T>()` demande explicitement une conversion numérique scalaire contrôlée.
2. `tryReceive()` retourne un message et ses métadonnées par valeur, ou
   `std::nullopt` si aucun message complet n'est disponible. Les erreurs restent
   des exceptions ; les anciennes méthodes de réception restent disponibles.
3. `subscribeMessage()` et `subscribeSender()` retournent directement
   `ivy::Subscription`. L'appelant conserve le jeton ; sa destruction désabonne.
   Les anciennes fonctions à identifiant numérique gardent leur contrat.
4. UDP devient une API publique. Les composants CMake `core`, `io` et `ivy`
   permettent de choisir les dépendances ; les cibles historiques restent
   disponibles. La construction sans Ivy et les clients qui la consomment sont testés.

Les exemples et les tests de leurs échanges sont la référence pour faire
évoluer ces contrats. Les conventions de routage, PING périodiques et rapports
de station sol restent dans `link++`.

## Champs : unités SI explicites

Les méthodes existantes manipulent les unités XML. `getFieldAs<double>()`
convertit le type numérique, sans convertir l'unité. L'accès SI est un appel
explicite et utilise systématiquement des `double` :

```cpp
pprzlink::Message gps(dictionary.getDefinition("GPS"));
gps.setFieldSI("alt", 123.456);   // Mètres -> int32 en millimètres.
gps.setFieldSI("speed", 12.34);   // m/s -> uint16 en cm/s.
double altitude_m = gps.getFieldSI("alt");
int32_t altitude_mm = gps.getField<int32_t>("alt"); // 123456

const auto& field = gps.getFieldDefinition("alt");
std::cout << field.getUnit() << " -> " << field.getSIUnit(); // mm -> m
int32_t native = field.fromSI<int32_t>(1.23456); // 1235
double si = field.toSI(native);                 // 1.235 m
```

`getFieldDefinition()` est disponible avant même que la valeur soit définie,
par nom ou indice. `getUnit()` et `getAltUnit()` renvoient les libellés XML
originaux, ou une chaîne vide pour un attribut absent. `getAltUnitCoef()`
renvoie un `std::optional<double>` contenant uniquement le coefficient écrit
dans le XML. `getSIUnit()` indique l'unité effectivement utilisée par l'API SI ;
une chaîne vide et `canConvertSI() == false` signalent une conversion indisponible.

| Grandeur | Convention de l'API SI |
| --- | --- |
| Longueur, durée, vitesse | m, s, m/s |
| Angles, latitude et longitude incluses | rad |
| Température absolue | K ; 293.15 K devient 20 °C dans un champ Celsius |
| Pourcentage | Rapport sans unité ; 0.5 devient 50 % |

Un setter SI choisit le type du XML. Une destination entière est arrondie à
l'entier le plus proche, avec les demi-valeurs en s'éloignant de zéro, puis
contrôlée en plage. Il n'y a ni mode strict ni saturation. Le getter SI restitue
la valeur stockée après cet arrondi. `fromSI<T>()`/`toSI(T)` convertissent un
scalaire ou un élément de tableau sans modifier de message ; `T` doit être
exactement le type de base XML. Les flottants peuvent arrondir vers `float` ;
les grands entiers 64 bits peuvent perdre de la précision dans le `double` SI.

Pour un tableau homogène :

```cpp
std::array<double, 3> position_m{1.0, 2.0, 3.0};
message.setFieldSI("position", position_m); // Champ numérique XML de trois éléments.
const auto position_si = message.getFieldArraySI("position");
```

`getFieldSI()` renvoie un `double` pour un scalaire ; `getFieldArraySI()` renvoie
un `std::vector<double>` pour un tableau. Sa longueur correspond au tableau
stocké dans le message, fixe ou variable selon le XML. Les lectures acceptent
aussi un indice XML. Les paramètres de sortie `double&` et
`std::vector<double>&` de `getFieldSI()` restent disponibles.

Les surcharges scalaire et tableau conservent la forme et les contrôles de
taille XML. Une conversion qui échoue ne remplace ni le champ, ni la destination
d'une lecture. `setFieldSI()` renvoie `const Message&`, comme `setField()` ;
on peut écrire `transport.sendMessage(message.setFieldSI("altitude", 123.5))`
lorsque le message est complet.

LLNL/units est utilisé en interne, sans types LLNL dans les signatures publiques.
Les règles éditables sont dans [UnitAliases.cpp](pprzlink/UnitAliases.cpp) :

```cpp
{"1e7deg", "deg", 1e-7},                  // XML -> LLNL : multiplier par 10^-7.
{"deg_celsius", "degC"},                 // Alias simple, facteur implicite 1.
{"C", "degC", 1.0, "ESC", "temperature"}, // Seulement ce message/champ.
{"adc", ""},                             // Conversion interdite sans calibration.
```

Les règles les plus spécifiques ont priorité ; à spécificité égale, la première
ligne gagne. Le facteur s'applique au nombre avant la conversion LLNL, notamment
avant le décalage Celsius/kelvin. Si le XML fournit `alt_unit` et un coefficient
explicite, ce coefficient prend priorité sur le facteur de la règle, sans être
appliqué deux fois. Un coefficient sans unité reste exposé mais ne suffit pas
à autoriser une conversion. Recompilez la bibliothèque après modification des règles.

Une conversion indisponible lève `field_unit_error` avec le nom du message,
du champ et l'unité XML. Les dépassements numériques lèvent
`field_conversion_error`. Les NaN de mesures flottantes se propagent ; ils ne
sont pas acceptés vers des entiers. Les repères, références d'altitude et epochs
ne sont pas transformés. `GROUND_REF.pos`, dont les unités varient selon `frame`,
reste à traiter dans le code applicatif.

La construction de pprzlink compile LLNL/units depuis son sous-module épinglé
et l'installation du SDK inclut cette dépendance. Les clients et `link++`
utilisent ce même SDK, sans installation séparée de LLNL.
Leurs appels existants gardent leurs unités XML ; les lectures/écritures SI sont
adoptables localement pour les champs dont `canConvertSI()` est vrai. Les
identifiants et commandes sans unité physique passent par l'API native. Le relais
de `link++` conserve les valeurs XML et les rapports de compatibilité OCaml.
La nouvelle disposition des classes exige de recompiler les clients, et pas
seulement de remplacer leur bibliothèque partagée.

Les exemples série, PTY et Ivy ont adopté l'accès SI pour leurs altitudes.
Les tests lancent aussi les mêmes clients série/Ivy avec une altitude XML
`int32` en millimètres : l'entrée et l'affichage restent en mètres.
L'enregistreur UDP conserve les valeurs XML pour ses sorties de diagnostic ;
le réglage générique conserve son accès natif car son unité est indéterminée.
Dans `link++`, les accès aux champs sont utilisés pour le routage et le relais.
Les rapports `LINK_REPORT` gardent leur calcul/formatage compatible OCaml en
double, plutôt que d'introduire une conversion vers les floats de leur schéma.

## Champs : conversions et erreurs explicites

Avec les définitions de `client_messages.xml` :

```cpp
pprzlink::Message command(dictionary.getDefinition("CLIENT_SETTING"));
command.setSenderId(0);
command.setReceiverId(42); // Destinataire dans l'en-tête binaire.
command.setField("ac_id", 42);
command.setField("value", 123.5); // Double converti vers le float du XML.

auto exact = command.getField<float>("value");
auto converted = command.getFieldAs<double>("value");

try {
    command.setField("ac_id", 300);
} catch (const pprzlink::field_conversion_error &error) {
    // Le diagnostic indique le champ, la valeur et les types.
    // ac_id vaut toujours 42 : aucune troncature silencieuse vers 44.
}
```

`addField()` reste disponible et applique les mêmes contrôles que `setField()`.
`setField()` accepte également plusieurs couples nom/valeur dans un seul appel :

```cpp
command.setField("ac_id", 42, "value", 123.5);
```

Les deux surcharges renvoient `const Message&` : une référence au même message,
sans copie. Cela permet de renseigner les champs directement dans l'appel
d'envoi :

```cpp
link.sendMessage(command.setField("value", 12.5f));
link.sendMessage(command.setField("ac_id", 42, "value", 12.5f));
```

Le message doit avoir tous ses champs renseignés pour être envoyé. Si
`setField()` lève une exception, `sendMessage()` n'est pas appelé. La référence
retournée est empruntée au message et ne prolonge pas sa durée de vie.
Les appels qui ignorent le retour continuent de fonctionner ; `addField()`
conserve son retour `void`.

L'appel doit contenir un nombre pair d'arguments, avec au moins un couple.
Un nombre impair est refusé à la compilation. Chaque valeur peut avoir son
propre type, y compris une chaîne ou un tableau, avec les mêmes contrôles XML
que l'appel à deux arguments. Les programmes utilisant cet appel conservent
leur comportement.

Pour la partie variadique, les diagnostics de compilation citent les contraintes
nommées `EvenNumberOfFieldArguments` si une valeur manque et
`FieldNamesAreStrings` si une clé n'est pas convertible en `std::string`.
La première clé est directement un paramètre `const std::string&` : son mauvais
type provoque une erreur de conversion. Les erreurs renvoient à la ligne de
l'appel, y compris lorsque la mauvaise clé est en fin de liste.

| Appel incorrect | Diagnostic attendu |
| --- | --- |
| `message.setField("index", 7, "ac_id")` | `EvenNumberOfFieldArguments` non satisfaite : la valeur de `ac_id` manque. |
| `message.setField("index", 7, 42, 12.5)` | `FieldNamesAreStrings` non satisfaite : le troisième argument est une clé entière. |
| `message.setField("index", 7, "ac_id", 42, true, 12.5)` | Même contrainte : le cinquième argument est une clé booléenne. |

Les noms réels des champs et leurs types XML sont connus à l'exécution. Une
clé mal orthographiée, une chaîne fournie pour un champ numérique ou une valeur
hors plage restent donc des erreurs d'exécution, même si l'appel est correct
du point de vue des types C++.

Les couples sont appliqués de gauche à droite. Si une validation échoue, les
couples précédents restent appliqués ; le champ refusé et les suivants ne sont
pas modifiés. Un nom répété prend la dernière valeur. Ces appels préparent
seulement le message local : l'envoi reste un appel séparé à `sendMessage()`.

Les éléments de tableaux sont contrôlés eux aussi. Les écritures hors plage,
les flottants non finis vers un entier et les valeurs fractionnaires vers un
entier lèvent `field_conversion_error`, dérivée de `std::out_of_range`.
Les setters d'identifiants d'en-tête acceptent également des nombres contrôlés :
`setSenderId(42)` fonctionne directement ; `setSenderId(300)` et
`setReceiverId(-1)` échouent sans modifier l'identifiant précédent. La limite
protocolaire de quatre bits du composant reste vérifiée lors de l'encodage.
Le champ `ac_id` et le destinataire d'en-tête restent deux informations distinctes :
certains messages décrivent un autre avion. La bibliothèque ne déduit pas les
règles de routage à partir du nom d'un champ.

Une destination flottante peut arrondir, notamment lors d'une conversion
`double` → `float` ou d'un grand entier vers un flottant. La conversion contrôlée
ne promet donc pas une représentation sans perte. Les valeurs flottantes de
même type, y compris les NaN, restent prises en charge par le protocole binaire.

`getField<T>()` conserve son contrat de lecture exacte. Une incompatibilité
lève maintenant `field_type_mismatch`, dérivée de `std::bad_variant_access`,
avec le nom du champ, son type XML et le type demandé. Les anciens blocs `catch`
restent valables. `getFieldAs<T>()` ne parse pas les chaînes et ne convertit pas
encore les tableaux ; leurs getters conservent les types d'éléments du XML.

Pour lire du texte sans copier ses octets, utilisez une `std::string_view`
(en-tête `<string_view>`) sur un champ `string`, `char[]` ou `char[N]` :

```cpp
const auto label = samples.getField<std::string_view>("label");
```

La vue emprunte le stockage du message jusqu'au remplacement du champ, à la
réaffectation ou à la destruction du message ; n'utilisez pas un message
temporaire. Les lectures par index et avec paramètre de sortie acceptent
également ce type. La vue conserve la longueur complète et les octets nuls,
sans exiger de terminateur. Les autres types de champs lèvent
`field_type_mismatch`.

Pour les tableaux, `std::span<const T>` (en-tête `<span>`) emprunte les éléments
sans copie et prend la longueur du tableau stocké :

```cpp
const auto axes = samples.getField<std::span<const float>>("axes");
```

Le type `T` doit correspondre au type XML des éléments. Les getters acceptent
les spans à taille dynamique. Les mêmes règles de durée de vie que
pour `string_view` s'appliquent, ainsi que la lecture par index ou avec paramètre
de sortie. Utilisez des éléments `const` : `const auto` seul ne rend pas les
éléments d'un `std::span<T>` constants, et les spans modifiables ne sont pas
acceptés par ces getters.

## Réception : un message et ses métadonnées

```cpp
if (auto received = transport.tryReceive()) {
    const auto &message = received->message;
    const auto bytes = received->frameSize;
    if (received->xbee) {
        const auto radioAddress = received->xbee->sourceAddress;
        // Utiliser rssi seulement si hasRssi est vrai.
    }
    if (received->udpPeer) {
        // Adresse et port de ce datagramme précis, conservés par valeur.
    }
}
```

`PprzTransport`, `XbeeTransport`, `PprzFrameDecoder` et `UdpTransport` proposent
cette opération. Elle effectue une tentative de lecture/décodage ; `nullopt`
signifie qu'aucun message complet n'a été produit pendant cet appel. Un transport
peut également traiter des statuts radio ou avancer l'initialisation XBee.
Les erreurs de contenu et d'I/O restent des exceptions. Une trame invalide est
consommée selon le contrat du décodeur ; l'appel suivant peut poursuivre.

Les anciennes méthodes `hasMessage()`, `getMessage()` et les getters de dernières
métadonnées restent disponibles. Les nouvelles valeurs retournées ne changent
pas après une réception ultérieure.

`UdpTransport::sendMessage(message, destination)` demande une destination
explicite. Le destinataire PPRZLINK dans le message et le pair réseau sont deux
informations distinctes. `localEndpoint()` expose le port attribué si le port
local demandé était zéro. Les trames incomplètes sont abandonnées à la frontière
du datagramme pour éviter de mélanger deux émetteurs.

## Ivy : utiliser la propriété déjà fournie par Ivy C++

`IvyLink::sendMessage()` transmet le texte produit par `serializeMessage()`.
Les champs numériques utilisent `std::to_chars`, indépendamment de la locale
et des réglages d'un flux. Les `float` et `double` finis sont transmis dans leur
représentation courte conservant exactement leur valeur lors du décodage dans
le même type : `123.6`, pas `123.600000`. La notation scientifique, les valeurs
minuscules et le zéro négatif sont pris en charge pour les scalaires et tableaux.
Le codec écrit directement depuis un tampon local sans flux temporaire par
champ ; le parseur évite les copies pour les nombres sans séparateurs `_`.
Le codec `serializeLegacyMessage()` garde son contrat distinct de formatage
OCaml/XML, notamment les précisions explicitement demandées dans le XML.

```cpp
auto subscription = link.subscribeMessage("CLIENT_ALTITUDE",
    [](std::string sender, pprzlink::Message message) {
        const double altitude = message.getFieldSI("altitude"); // Mètres SI.
        // Traiter altitude et sender.
    });
```

Le type retourné est directement `ivy::Subscription`. Il est déplaçable,
non copiable, désabonné à sa destruction. `subscribeSender()` fournit le même
contrat pour un expéditeur ; `subscribeRequestAnswerer()` pour un répondeur.
L'ancienne API à identifiants numériques délègue aux mêmes mécanismes.

Conserver le jeton aussi longtemps que l'abonnement est souhaité. Un callback
déjà commencé peut finir après le désabonnement : son état capturé doit rester
vivant. Les callbacks exécutent sur la boucle Ivy ; l'exemple utilise la boucle
sur le thread appelant. Une interface graphique doit encore transférer les
mises à jour sur son propre thread. Aucun nouveau système d'abonnement n'est
introduit au-dessus de celui d'Ivy.

## Dépendances choisies par le client

| Composant CMake | Contenu |
| --- | --- |
| `pprzlink::core` | XML, messages, conversions SI LLNL/units, codecs, framing PPRZ/XBee et interface abstraite Device. TinyXML2 et en-têtes Boost ; aucune dépendance Ivy. |
| `pprzlink::io` | Série Boost.Asio, fichiers POSIX et UDP ; dépend de `core` et des threads. |
| `pprzlink::ivy` | IvyLink ; dépend de `core`, Ivy C++ et des threads. |

Les composants sont des archives statiques construites avec PIC. Les cibles
historiques `pprzlink++` et `pprzlink++_static` restent respectivement partagée
et statique. Ne pas mélanger les cibles agrégées et les composants dans un même
client ; choisir l'une des deux interfaces de construction.

```cmake
find_package(pprzlink++ CONFIG REQUIRED COMPONENTS io)
target_link_libraries(my_recorder PRIVATE pprzlink::io)
```

Cet appel ne cherche pas Ivy, même si le SDK a été construit avec son composant
Ivy. Sans `COMPONENTS`, `find_package` garde le comportement historique et
charge les dépendances des bibliothèques agrégées. Demander le composant `ivy`
à un SDK construit sans Ivy provoque une erreur de configuration explicite.

## Construire et essayer les exemples

Depuis `lib/v2.0/C++`, après préparation des dépendances décrites dans le README :

```sh
cmake -S . -B build-gcc13 -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-gcc13 -j4
ctest --test-dir build-gcc13 --output-on-failure

./build-gcc13/ivy_receiver pprzlink/examples/clients/client_messages.xml 127.255.255.255:2010
./build-gcc13/serial_aircraft pprzlink/examples/clients/client_messages.xml /dev/ttyUSB0
./build-gcc13/udp_recorder pprzlink/examples/clients/client_messages.xml 4242
./build-gcc13/udp_setting_sender pprzlink/examples/clients/client_messages.xml 4242
```

`udp_setting_sender` envoie une seule commande `CLIENT_SETTING` pour l'avion
42, avec `value=12.5`, vers `127.0.0.1` au port indiqué. Un pair de test doit
écouter ce port pour la recevoir. Ce message appartient au XML d'exemple.

Construction sans Ivy, incluant les exemples série/UDP et leurs tests :

```sh
cmake -S . -B build-no-ivy -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 \
  -DPPRZLINK_WITH_IVY=OFF -DPPRZLINK_BUILD_LINK=OFF
cmake --build build-no-ivy -j4
ctest --test-dir build-no-ivy --output-on-failure
```

Le Makefile historique accepte également `WITH_IVY=0`. Ses bibliothèques
agrégées excluent alors l'adaptateur Ivy.

Pour vérifier un usage extérieur au dépôt :

```sh
cmake --install build-gcc13 --prefix "$PWD/sdk"
cp -R pprzlink/examples/clients /tmp/pprzlink-client-examples
cmake -S /tmp/pprzlink-client-examples -B /tmp/pprzlink-client-build \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 -DCMAKE_PREFIX_PATH="$PWD/sdk"
cmake --build /tmp/pprzlink-client-build -j4
```

`-DCLIENTS_WITH_IVY=OFF` construit les trois clients série/UDP sans rechercher
Ivy. Le standard C++23 et les dépendances transitives proviennent des cibles
installées ; le projet client ne les redéclare pas.

## Validation du remplissage multiple

La validation du 29 septembre, après ajout de la surcharge de `setField()` et
du client `UdpSettingSender`, couvre la compilation avec GCC 13.3 et les
**19/19 tests avec Ivy** et **15/15 tests sans Ivy**. Le scénario comparatif
de `link++` avec OCaml est inclus.
Les tests vérifient le refus d'un nombre impair d'arguments, les conversions,
l'ordre d'application et l'égalité des trames avec les appels individuels.

Les trois clients série/UDP ont également été copiés hors du dépôt, compilés
avec `CLIENTS_WITH_IVY=OFF` contre le SDK complet installé, puis exécutés.
Le datagramme de `CLIENT_SETTING` est comparé aux octets attendus. Cette
vérification utilise `/tmp/pprzlink-api/clients-variadic/` ; aucun répertoire
du dépôt n'est utilisé comme chemin d'inclusion par ces clients.

Le 30 septembre, les diagnostics ont été renforcés avec des contraintes
nommées : **20/20 tests avec Ivy passent sous GCC 13.3**. Le nouveau test
`field_arguments_compile` compile un cas valide puis cinq appels incorrects ;
il vérifie le refus, la contrainte ou conversion indiquée, et la référence à
la ligne de l'appel fautif. Les clés erronées au début, au milieu et en fin de
liste sont couvertes.

## Validation de la première itération

- Ubuntu 24.04, GCC 13.3 : **19/19 tests avec Ivy**, dont les nouveaux contrôles
  de conversion, réception, UDP et les trois clients exécutés.
- Construction sans Ivy : **15/15 tests**, sans préfixe Ivy dans l'environnement
  d'exécution et avec la découverte pkg-config désactivée à la configuration.
- Les **17 scénarios comparatifs de link++/OCaml** passent après adoption de
  `tryReceive()` et de `UdpTransport` par l'agent.
- Les trois clients sont copiés hors du dépôt, compilés et exécutés contre le
  SDK installé. Les clients série/UDP sont aussi compilés et exécutés sans Ivy,
  depuis un SDK complet et depuis un SDK sans Ivy.
- Un consommateur `core` seul compile et s'exécute avec la découverte d'Ivy et
  de Threads désactivée. `ldd` confirme l'absence d'Ivy dans les clients série/UDP.
- Les deux anciens consommateurs CMake, statique et partagé, passent toujours.
- ASan/UBSan avec détection des fuites : **19/19 tests réussis**, sans diagnostic.
  La bibliothèque, l'agent et les clients sont instrumentés ; Ivy et les
  dépendances système ne sont pas réinstrumentés dans cette session.
- Le Makefile construit les deux variantes et le changement `WITH_IVY=1` → `0`
  reconstruit bien les archives/bibliothèques sans conserver `IvyLink.o` ni de
  dépendance dynamique à Ivy.
- La CI reprend la construction des clients installés et les tests sans Ivy.
  Ces commandes sont validées localement ; la nouvelle CI n'a pas été lancée
  sur GitHub pendant cette session.

Traces locales : `/tmp/pprzlink-api/` (`gcc13`, `no-ivy`, `sdk`, `sdk-no-ivy`,
`clients-build`, `clients-no-ivy-build`, `no-ivy-sdk-clients`, `core-client`,
`legacy-consumer` et leurs journaux). Les changements d'ABI nécessitent une
recompilation des applications ; les signatures historiques sont conservées,
mais les écritures numériques invalides sont désormais rejetées.

## Limites observées pour les prochaines itérations

| Observation dans les exemples | Amélioration à examiner ensuite |
| --- | --- |
| Le récepteur Ivy utilise `getBus()` pour son délai. | Délais et annulation des requêtes au niveau PPRZLINK ; `sendRequest()` conserve encore son interface historique. |
| Le simulateur fait tourner Asio ; l'enregistreur interroge UDP périodiquement. | Réception asynchrone intégrable dans une boucle existante, sans créer de thread implicite. |
| Les callbacks Ivy peuvent s'exécuter sur un thread distinct. | Exemple d'intégration GUI avec transfert explicite des événements et durée de vie des objets. |
| La lecture convertie couvre les scalaires. | Évaluer les conversions explicites de tableaux à partir d'un vrai client qui en a besoin. |
| Le récepteur écrit l'unité « m » dans son code. | Exposer davantage de métadonnées XML, comme les unités et les valeurs d'énumération, pour un afficheur générique. |

Ces points ne sont pas masqués par une nouvelle abstraction générale. Chaque
extension future devra raccourcir ou clarifier un client concret, avec ses tests.
