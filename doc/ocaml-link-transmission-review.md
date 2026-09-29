# Transmission par `link` : arguments Ivy et tailles binaires

## Base et périmètre

Analyse du 29 septembre 2026, sur PPRZLink
`1b086b68a691378d151daa5c8616eb56653e5900`, commit référencé par
Paparazzi `0bbcf766088ca06c27b697e7b679377473ce6ebd`.

Les corrections, ce rapport et les tests sont dans la branche
`FIX_Codex_Assited/link` de **PPRZLink**. Le dépôt parent Paparazzi reste sur
`master`. Aucune modification de la branche `FEATURE/ivycpp_native` n'est incluse.

Le programme `sw/ground_segment/tmtc/link.ml` de Paparazzi délègue ses
conversions à `PprzLink` : `values_of_payload` puis `message_send` en réception,
`message_bind` puis `payload_of_values` en émission. La version sélectionnée
par défaut est la bibliothèque v2.0. Le patch touche uniquement
[`lib/v2.0/ocaml/pprzLink.ml`](../lib/v2.0/ocaml/pprzLink.ml).

Il faut distinguer **le nombre de champs Ivy**, **le nombre d'éléments d'un
tableau** et **le nombre d'octets écrits**. Un tableau représente un seul champ
Ivy, même quand il contient zéro élément. Aucun défaut général de comptage des
arguments de la liaison OCaml/C n'a été établi ici.

## 1. Champs vides : rejet ou disparition d'un argument Ivy

### Lecture du texte

`values_of_string` accepte des champs entourés de `"..."` ou de `|...|`.
Son analyseur exige cependant un `Str.Text` entre les délimiteurs. Pour `""`
ou `||`, `Str.full_split` ne produit pas ce texte intermédiaire : le champ
vide déclenche `incorrect array delimiter`.

Exemple réel, avec les quatre champs de `MISSION_UPDATE` :

```text
MISSION_UPDATE 42 7 -1 ""
```

Le champ `params` est un `float[]` vide, représentable en binaire par un octet
de longueur égal à zéro. Avant correction, le texte est rejeté et cette
commande n'est pas correctement transmise par le chemin habituel de `link`.
La correction accepte deux délimiteurs identiques consécutifs comme **un
argument vide**, que le type du champ interprète ensuite.

### Écriture du texte

`formatted_string_of_value` et `string_of_value` accèdent à `a.(0)` pour
distinguer un tableau de caractères d'un tableau numérique. Sur `Array [||]`,
cet accès lève `Invalid_argument("index out of bounds")`.

Ainsi, une charge utile binaire valide de `PAYLOAD` avec `values=[]`, ou de
`INFO_MSG` avec `msg=[]`, est décodée mais sa publication Ivy échoue. Dans
`link`, l'exception est interceptée par `use_tele_message` : le message n'est
pas publié et `update_status`, situé après la publication, n'est pas exécuté
pour ce message.

Le patch produit `""` pour un tableau vide. Cela évite l'exception et préserve
la présence du champ lors d'une relecture par la bibliothèque OCaml corrigée.

De plus, une valeur `String ""` était rendue sans aucun caractère. Un message
de test à trois champs devenait `TEXT 1  2` ; la découpe sur `[ \t]+` ne
retrouvait que deux arguments. Une chaîne contenant une tabulation sans espace
avait le problème inverse : `TEXT 1 a<TAB>b 2` donnait quatre arguments.
Le patch entoure aussi les chaînes vides et celles contenant une tabulation
de guillemets, comme il le faisait déjà pour celles contenant un espace.

La validation stricte du nombre de champs reste en place : un champ réellement
absent ou surnuméraire est toujours rejeté. Un tableau vide doit être explicite
(`""` ou `||`) ; une simple succession d'espaces n'est pas réinterprétée.

## 2. `uint32` : huit octets écrits pour quatre annoncés

Les `uint32` sont représentés par le constructeur OCaml `Int64`, afin de
pouvoir représenter toute leur plage non signée. `sprint_value` les envoyait
néanmoins à `sprint_int64`, dont la primitive C `c_sprint_int64` écrit huit
octets, puis annonçait une taille de quatre octets.

Reproduction sur un tampon de douze octets initialisés à `aa`, en écrivant
`0x12345678` à l'indice 1 :

```text
Avant : aa 78 56 34 12 00 00 00 00 aa aa aa
Après : aa 78 56 34 12 aa aa aa aa aa aa aa
Taille annoncée dans les deux cas : 4
```

Les quatre octets après le champ sont écrasés. Le même défaut se produit sur
le dernier élément d'un tableau de `uint32`. Près de la fin du tampon, cette
écriture peut dépasser sa capacité.

**Cela ne signifie pas que chaque trame contenant un `uint32` est corrompue.**
Sur la machine little-endian testée, les quatre octets utiles sont corrects ;
les champs suivants réécrivent généralement le surplus, et `payload_of_values`
coupe le tampon à la longueur annoncée. Le test du message réel
`WINDTURBINE_STATUS` passe donc déjà avant correction. Le défaut démontré est
l'écriture au-delà du champ, avec un risque de dépassement du tampon ; aucun
crash en fonctionnement n'est revendiqué par ces tests.

La correction utilise `sprint_int32` avec `Int64.to_int32`, en conservant les
32 bits de la valeur, y compris `0x80000000` et `0xffffffff`. Les `int64` et
`uint64` continuent à utiliser l'écriture sur huit octets. Aucune modification
du C n'est nécessaire : l'erreur est dans le choix de la primitive.

## 3. `int8` : rejet incorrect de la valeur `-128`

La condition `x < -0x7f` exclut `-128`, pourtant représentable sur un octet
signé. Une commande telle que :

```text
BOOZ_NAV_STICK 42 -128 127 -1 0
```

est acceptée par l'analyseur Ivy mais rejetée lors de l'encodage binaire.
La correction remplace la borne inférieure par `-0x80`.
Les valeurs hors plage `-129` et `128` restent rejetées. Ce défaut porte sur
la plage du type, pas sur le nombre d'arguments.

## Patch et validation

Le changement de production tient à **sept lignes ajoutées et trois retirées
dans un seul fichier OCaml**, sans changement d'API ni de format binaire.
Les tests sont dans [`tests/ocaml`](../tests/ocaml).

Depuis la racine de PPRZLink, avec Python 3, OCaml et un compilateur C :

```sh
python3 tests/ocaml/run_regression.py --revision 1b086b68a691378d151daa5c8616eb56653e5900
python3 tests/ocaml/run_regression.py
```

Résultats obtenus avec OCaml 4.14.1 :

| Version | Résultat |
| --- | --- |
| Base `1b086b6` | 11/24 réussis, 13 échecs reproduisant les défauts |
| Arbre corrigé | 24/24 réussis |

Les tests compilent les véritables fichiers `pprzLink.ml`, `convert.c`,
`protocol.ml`, `pprz_transport.ml` et `debugPL.ml` dans un répertoire temporaire.
Ils vérifient notamment les aller-retours de commandes réelles, les octets
binaires attendus, les longueurs, les sommes de contrôle, les octets témoins
après un champ, les chaînes vides et les cas valides déjà fonctionnels.

L'environnement ne dispose pas d'`ocamlfind`, d'Ivy OCaml ni de `xml-light`.
Le banc charge les vrais catalogues XML via Python et fournit un adaptateur
d'arbre XML ; il capture `Ivy.send`. Les fonctions réseau Ivy ne sont pas
simulées comme fonctionnelles : leur appel échoue explicitement. Le module
est compilé sans son `.mli` pour tester également ses primitives internes.
Ce sont des tests du codec OCaml/C, **pas un essai du programme `link` complet
sur bus Ivy, modem ou UDP**.

La v1.0 contient des défauts similaires mais reste hors de ce patch ciblé sur
le `link` actuel. La compatibilité des tableaux numériques vides avec les
autres consommateurs Ivy (C, C++, Python) reste à vérifier : le test démontre
la conservation du champ et sa relecture par OCaml. Les producteurs qui
omettent entièrement ce champ vide doivent l'encoder explicitement.

Aucun changement n'est apporté au routage, aux temporisations, aux retries,
aux statistiques ou aux fonctions C++.
