# NORTHSTAR — PARENA et l'héritage Ada/GNAT (version française)

**Statut :** vision à long terme, rien n'est implémenté. Rédigé le 2026-08-24, à la demande du
fondateur, en temps réel, fil complet : « make sure our compiler has all of the features of ADA
GNAT » → précisé : « the ones that serve our mission of memory management » → « and allow for
different levels of memory and compiler and instruction hacks » → « and on some level the
compiler should have a flag to turn that off » → « and pull any other useful ADA GNAT prior art »
→ « and then prioritize all the features of ADA GNAT and what i discussed and beyond into a
northstar » → confirmé de nouveau : « be sure to include the full ADA GNAT surface » → « write
that document in french » → raison donnée : « the biggest ada firm is in france » (AdaCore, éditeur
de GNAT, a son siège à Paris) → « we need to recruit to parena ». Ce document sert donc aussi de
pièce de recrutement destinée à des talents Ada/GNAT francophones — d'où la traduction complète,
et non un simple résumé.

**Ce document existe en parallèle de `NORTHSTAR_ADA_GNAT_MEMORY.md` (anglais)**, la version de
référence technique interne de l'équipe compilateur. Celui-ci en est une traduction complète,
augmentée pour couvrir toute la surface de GNAT (pas seulement la gestion mémoire), comme demandé.

**Cadrage, dit d'emblée :** « tout GNAT » n'est pas une tâche réaliste — GNAT est l'un des
compilateurs Ada les plus complets qui existent (généricité complète, tâches concurrentes,
contrats/SPARK, gestion d'exceptions, des décennies d'ingénierie). Ce document reste une liste
priorisée de fonctionnalités réelles de GNAT/Ada, avec leur pertinence directe pour PARENA — pas
un projet de portage intégral.

## Priorité 1 — l'invariant de retour (déjà noté comme manquant dans region.c)

`src/region.h` documente déjà cette lacune : `Region(Value) >= Region(Caller)`. Les **niveaux
d'accessibilité** d'Ada sont le précédent réel, vieux de plusieurs décennies, le plus proche —
exactement l'invariant que le système de rangs de région de PARENA (`:region/scratch` /
`:region/buffer`) exprime déjà, mais généralisé à une profondeur d'imbrication arbitraire. GNAT
rejette, à la compilation et sans coût à l'exécution, une fonction qui retournerait une valeur
d'accès pointant vers un objet local de portée plus courte que celle attendue par l'appelant —
c'est exactement l'implémentation réelle de l'invariant de retour que `region.c` ne vérifie pas
encore. **Portée concrète** : étendre le `walk()` existant de `region.c` pour vérifier l'expression
de retour d'un `defn` par rapport à son annotation `@ Region` déclarée — la même logique
d'invariant d'affectation, appliquée à une position syntaxique de plus. L'étape suivante la moins
coûteuse et la plus sûre.

## Priorité 2 — vérification du déplacement/propriété (l'autre lacune nommée de region.c)

Les symboles `!var` / `(move var)` / `&var` / `&mut var` de PARENA existent déjà dans la table de
syntaxe de NORTHSTAR.md, mais ne sont vérifiés nulle part — aujourd'hui, une simple convention de
nommage, pas un invariant appliqué. Précédent réel : **l'annexe Pointer/propriété de SPARK** (le
sous-ensemble Ada formellement vérifiable que GNAT accompagne d'un prouveur) — une analyse de flux
sur le même AST déjà parcouru par le compilateur, marquant une liaison comme « déplacée » dès
qu'elle est consommée par un symbole `!`, et signalant toute référence ultérieure à cette même
liaison. Une passe véritablement nouvelle, pas une extension de `walk()` — une étude de conception
réelle est nécessaire avant de l'écrire.

## Priorité 3 — un vrai drapeau de désactivation des vérifications (demande directe du fondateur)

**« the compiler should have a flag to turn that off. »** Correspondance directe avec le
`pragma Suppress` / `-gnatp` réel de GNAT : désactiver sélectivement (ou globalement) les
vérifications à la compilation une fois un programme jugé fiable, sans toucher au code source.
Pour PARENA, cela signifie un `--suppress=region,move` (ou un drapeau unique façon `-gnatp`) sur
`parena build` qui court-circuite `region_analyze()` (et le futur vérificateur de déplacement) —
utile en particulier pour le palier « montre-calculatrice Casio » ci-dessous, où même le code
généré par un vérificateur présent-mais-inoffensif peut encore coûter quelque chose à produire
pour une cible minuscule. Modification petite et mécanique : un drapeau CLI transmis à
`cmd_build`, protégeant l'appel existant à `region_analyze()`.

## Priorité 4 — le déterminisme comme restriction à la compilation (fil du RNG)

**« we can turn rng off at compile time » / « speed up games that might not always need real
rng » — réponse déjà donnée directement : le vrai gain est le déterminisme, pas seulement la
vitesse** — le multijoueur en lockstep (le vrai code réseau de REDGARDEN/SHANKPIT) a besoin d'un
RNG strictement identique sur chaque client ; le rejeu et le débogage ont besoin d'exécutions
reproductibles. Précédent réel de GNAT : **`pragma Restrictions`** — le mécanisme d'Ada pour
éliminer des catégories entières de comportement à l'exécution, dès la compilation, de façon
VÉRIFIÉE (une violation est une erreur de compilation, pas un simple oubli silencieux) — plus fort
que le simple drapeau de suppression de la Priorité 3. Un `(restrict :no-true-random)` PARENA (au
niveau module ou programme) : (a) ferait de tout appel à une source d'entropie réelle une erreur de
compilation, (b) permettrait au code de la bibliothèque standard de basculer vers un chemin PRNG
à graine plus rapide quand la restriction est active. Le même mécanisme se généralise au-delà du
RNG à d'autres catégories de restrictions (`:no-recursion`, `:no-dynamic-alloc` pour le palier
cible minuscule ci-dessous — les restrictions réelles `No_Recursion`/`No_Allocators` d'Ada en sont
le précédent direct).

## Priorité 5 — paliers pour cibles minuscules (« montre-calculatrice Casio »)

Précédent réel de GNAT : le **profil Ravenscar** / le **runtime Zero Footprint** — des
configurations de runtime restreintes et minimales que GNAT propose réellement pour des cibles
avioniques et autres systèmes embarqués/bare-metal extrêmement contraints (pas d'allocation
dynamique, pas de récursion non bornée, un support runtime fixe et minuscule). S'articule
directement avec le mécanisme de restrictions de la Priorité 4 (`:no-dynamic-alloc` +
`:no-recursion` + `:no-true-random` ensemble approchent un profil façon Ravenscar) plutôt que
d'être une fonctionnalité séparée — travail réel, ultérieur, une fois les restrictions elles-mêmes
existantes, non détaillé davantage ici.

## Priorité 6 — hacks d'instructions et contrôle bas niveau (le fil de mission d'origine de PARENA)

Se rattache à l'objectif déjà énoncé dans NORTHSTAR.md (« instruction hacks... bring into the real
world of real architectures like arm and x86 »). Outils réels de GNAT, sans équivalent PARENA
aujourd'hui :

- **Clauses de représentation** (`for T'Size use N;`, `for T'Address use ...;`, disposition de bits
  au niveau des enregistrements) — contrôle explicite et vérifié de la disposition mémoire exacte
  d'un type. Le `defstruct` de PARENA n'a aujourd'hui aucune annotation de contrôle de disposition.
- **`System.Machine_Code`** — assembleur en ligne réel et typé (listes d'arguments/de registres
  déclarées, pas une simple chaîne de caractères brute). Le `#target {:c (inline-c "...")}` actuel
  de PARENA est l'analogue existant le plus proche (réel, déjà largement utilisé au cours de cette
  session) mais reste au niveau C, non typé et non vérifié par conception (« VS0 has no way to
  check it » — commentaire propre d'`emit.c`). Étape suivante concrète si cela devient prioritaire :
  une clé sœur `#target {:asm (inline-asm "...")}`, suivant exactement le même schéma de confiance
  verbatim que `find_target_c_src` établit déjà pour `:c` — un ajout petit et mécanique, pas un
  nouveau sous-système.
- **`pragma Machine_Attribute`** — attributs spécifiques au CPU cible par sous-programme
  (convention d'appel, gestion des interruptions). Aucun équivalent PARENA.
- **`Interfaces` / types entiers à largeur fixe** — le système de types de PARENA n'a aujourd'hui
  que `I32` ; aucune famille de types entiers à largeur fixe. Se rattache à une lacune réelle et
  plus large déjà rencontrée directement cette session, et contournée localement plutôt que
  corrigée en profondeur : la gestion des littéraux numériques d'`emit.c` ne distingue pas du tout
  entier et flottant (chaque littéral est un `double`) — voir le CHANGELOG PARENA du 2026-08-24
  (le contournement `#target` `zero-i32` de `regex/pcre.prn`). Une vraie famille d'entiers à
  largeur fixe dépend d'abord de la correction de cette lacune plus fondamentale.

## Priorité 7 — espaces de noms de modules (trouvé en direct cette session, précédent GNAT réel)

Sans lien direct avec ce fil de discussion, mais réel, connexe, et à garder ici plutôt que de le
perdre : le travail sur `regex/pcre.prn` cette session a révélé que le registre de structures/
énumérations d'`emit.c` est plat et NON scopé par module — `regex/nfa.prn` et `regex/pcre.prn`
définissent tous deux une structure nommée `Regex` avec des champs différents, et compiler les
deux ensemble masque silencieusement l'une par l'autre (contourné en ne compilant jamais les deux
dans la même unité, pas corrigé). Précédent réel d'Ada : **les unités de bibliothèque +
`pragma Elaborate`/`Elaborate_All`** — le modèle d'unités de compilation d'Ada exige que chaque nom
se résolve à travers l'espace de noms de son propre paquetage, avec un ordre d'élaboration
(initialisation) vérifié par le compilateur entre les unités. Correction réelle pour PARENA :
qualifier en interne chaque nom de structure/énumération enregistré par son module propriétaire
(`regex/nfa.Regex` contre `regex/pcre.Regex`), résolvant une référence nue d'abord dans le MÊME
module, puis via un `(import ...)` explicite — travail de compilateur réel et séparé, non entrepris
ici.

## Priorité 8 — généricité réelle (couverture complète de la surface GNAT, comme demandé)

La lacune la plus large de PARENA aujourd'hui : `vec.prn`/`map.prn` existent en tant que design
source réel mais ne compilent pas — `(Raw T)` avec `T` un paramètre de type non lié n'est pas une
forme reconnue par `resolve_declared_type()`. Précédent réel de GNAT : les **paquets génériques**
d'Ada (`generic ... package`), instanciés à la compilation, sans effacement de type à l'exécution.
Contrairement au modèle actuel de PARENA (`(Vec T)` s'efface systématiquement en un type
`Vec` d'exécution générique, `void *`-typé), l'instanciation générique réelle produirait un vrai
code C spécialisé par instanciation (un `Vec_I32`, un `Vec_String` réellement distincts) —
changement d'architecture significatif dans l'émetteur, pas une extension mineure. Travail le plus
important de cette liste, mais aussi celui qui débloquerait le plus (vec.prn/map.prn eux-mêmes,
et toute la classe des contournements `#target` du type `vec-i32-at` ajoutés cette même session).

## Priorité 9 — concurrence/tâches (lien avec le fil BEAM/Erlang déjà en cours)

Le vrai modèle de tâches d'Ada (`task`/`protected`/`select`/`rendezvous`) est une lignée
différente du fil fédéré-par-processus-façon-Erlang/BEAM déjà en cours ce jour même (voir
`GoblinFoxDragon/docs2/MOD_SURFACE_NORTHSTAR.md` §3a) — les deux méritent d'être connus, pas
fusionnés. Le modèle Ada (mémoire partagée protégée, rendez-vous synchrone) convient bien à un
noyau embarqué temps réel ; le modèle Erlang (processus isolés, pas de mémoire partagée,
tolérance aux pannes par supervision) convient mieux à l'objectif déjà énoncé de PARENA
(isolation EduScript/PARENA, « let it crash »). Recommandation : rester sur la voie Erlang/BEAM
déjà engagée pour la concurrence de haut niveau ; le modèle de tâches Ada reste un précédent utile
uniquement si un jour un runtime temps réel dur, façon Ravenscar (Priorité 5), devient une cible
réelle.

## Priorité 10 — gestion des exceptions

`Result`/`Option` de PARENA couvrent déjà le même terrain que la gestion d'erreurs par valeurs de
retour (façon Rust) — un mécanisme différent, pas nécessairement inférieur, du modèle
`exception`/`raise`/`when` d'Ada. Pas un manque évident à combler ; noté pour être complet sur la
surface de GNAT, comme demandé, mais sans recommandation de l'adopter.

## Recommandation finale

Ne pas tenter de « porter GNAT ». Deux étapes suivantes réellement prêtes à être scopées si ce
sujet devient une priorité active : la Priorité 1 (extension mécanique de `region.c`) et la
Priorité 3 (drapeau de suppression — petit, indépendant, utile immédiatement). Tout le reste est
une vision à long terme réelle, pas une liste de tâches pour la prochaine session.
