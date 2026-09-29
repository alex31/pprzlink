# API pprzlink++ : simplification par les exemples

Première itération réalisée le 29 septembre 2026. Les clients de référence sont
un récepteur Ivy, un simulateur d'avion série et un enregistreur UDP. Ils utilisent
uniquement les en-têtes publics de la bibliothèque. Leur répertoire peut être
copié dans un autre projet et compilé avec le SDK installé.

| Exemple | Parcours et limite qu'il permet d'observer |
| --- | --- |
| [IvyReceiver.cpp](pprzlink/examples/clients/IvyReceiver.cpp) | S'abonner, lire une altitude comme `double`, arrêter la boucle. L'abonnement est possédé par une variable locale ; le délai utilise encore l'API native Ivy. |
| [SerialAircraft.cpp](pprzlink/examples/clients/SerialAircraft.cpp) | Ouvrir un port, publier une altitude, répondre à PING. L'application fournit et fait tourner son contexte Asio. |
| [UdpRecorder.cpp](pprzlink/examples/clients/UdpRecorder.cpp) | Recevoir trois messages avec leur adresse source et leur taille. Aucun bus Ivy n'est démarré ni lié au programme. |

Ces exemples sont petits et bornés à dix secondes. Ils servent de clients
exécutables de référence, pas de services complets de production.
Utiliser le XML fourni avec un pair de test qui charge les mêmes définitions.

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

```cpp
auto subscription = link.subscribeMessage("CLIENT_ALTITUDE",
    [](std::string sender, pprzlink::Message message) {
        const double altitude = message.getFieldAs<double>("altitude");
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
| `pprzlink::core` | XML, messages, codecs, framing PPRZ/XBee et interface abstraite Device. TinyXML2 et en-têtes Boost ; aucune dépendance Ivy. |
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
```

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

`-DCLIENTS_WITH_IVY=OFF` construit les deux clients série/UDP sans rechercher
Ivy. Le standard C++23 et les dépendances transitives proviennent des cibles
installées ; le projet client ne les redéclare pas.

## Validation de cette itération

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
