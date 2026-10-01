# Sanctuaire d'Ordona — prototype v0.3 (mod natif Dusklight)

## Important : état du code
- Écrit à partir du SDK officiel (dépôt TwilitRealm/dusklight, v2.0.3) et du `mod-template`.
- **Pas compilé ni testé en jeu** (je n'avais ni compilateur MSVC ni le jeu). Il peut y avoir des erreurs de
  compilation ou des valeurs à corriger. Envoie-moi le message d'erreur ou le journal et je corrige.

## Pourquoi tes anciens .dusk ne marchaient pas
Ils ne contenaient aucun code (juste du JSON). Un .dusk sans DLL ou script ne peut rien faire en jeu.

## Compiler sans installer Visual Studio (GitHub Actions)
1. Crée un compte GitHub, puis un dépôt vide (privé si tu veux).
2. Envoie-y tout le contenu de ce dossier (bouton « Add file > Upload files »).
3. Onglet **Actions** : le workflow « Build » se lance tout seul (~10-15 min).
4. Télécharge l'artefact **windows-amd64** : il contient `sanctuaire_ordona.dusk`.
5. Copie-le dans `%APPDATA%\TwilitRealm\Dusklight\mods` puis active-le dans le gestionnaire de mods du jeu.

## Utilisation en jeu
1. Va dans la cave de Link (stage `R_SP01`, salle 4 — hypothèse à vérifier).
2. Place-toi où tu veux le miroir, tiens **R** et appuie sur **Haut** (croix) : position enregistrée.
3. Éloigne-toi, reviens : message « Le miroir brille... », appuie sur **A** : changement de stage.
4. Dans le donjon, **L + R + Z** ramène dans la cave.

## À vérifier avec le journal (console de Dusklight)
- Le message « Miroir place en (...) dans R_SP01 salle 4 » : confirme le stage et la salle.
  Si le stage/la salle sont autres, change `kPortalStage` / `kPortalRoom` en haut de `src/mod.cpp`.
- Les points d'entrée `kDestPoint`, `kDestRoom`, `kReturnPoint` sont des hypothèses.

## Limites connues
- Pas de miroir visible : c'est une zone invisible avec un message (un objet visible demande un modèle 3D
  et un acteur custom, voir `mods/custom_actor_demo` dans le dépôt Dusklight).
- Le donjon est la Caverne des Épreuves (D_SB01) réutilisée. Créer une géométrie neuve n'est pas faisable
  avec un mod seul. Quitter la caverne par sa sortie normale ramène dans le désert, pas à Ordon.
