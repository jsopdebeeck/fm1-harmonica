# Enregistrer son propre harmonica

Le firmware joue par défaut l'harmonica de la banque FluidR3_GM (licence MIT, voir `LICENSES/FluidR3-MIT.txt`).
Pour le remplacer par ton harmonica, enregistre une note par fichier, puis convertis-les avec `app/tools/samples.py`.

## Le matériel

- Un micro posé à 20–30 cm, face aux trous. Un smartphone convient ; un micro USB ou un statique fait mieux.
- Une pièce calme, peu réverbérante (rideaux, canapé, pas de carrelage nu).
- Les mains écartées de l'harmonica : pas d'effet de main, il est ajouté par le moteur (ARP, SEQ).

## Les prises

- Format : WAV, 44,1 ou 48 kHz, 16 ou 24 bits. Mono ou stéréo (le stéréo est mélangé en mono).
- Une note par fichier, tenue **2 à 3 secondes**, souffle constant, sans vibrato ni bend : le moteur les ajoute.
- Une attaque nette au début, puis la note bien stable : l'outil prend 0,25 s d'attaque et boucle la suite.
- Le même souffle et la même distance pour toutes les notes. Des crêtes vers −6 dBFS, sans saturer.
- Coupe le silence avant la note si l'enregistreur en laisse beaucoup (l'outil retrouve le début, mais le
  bruit de fond en tête n'aide pas).

## Quelles notes

Un harmonica en Do suffit : les autres tonalités sont obtenues en transposant les échantillons les plus proches.

- **L'idéal** : chaque trou, soufflé et aspiré, soit 19 notes (le Sol de 2 aspiré et 3 soufflé une seule fois).
- **Le minimum** : une note toutes les trois ou quatre demi-tons sur toute l'étendue, par exemple
  1 soufflé, 2 soufflé, 2 aspiré, 3 aspiré, 4 aspiré, 5 soufflé, 6 aspiré, 7 soufflé, 8 aspiré, 9 soufflé, 10 aspiré.

La place compte : les échantillons vont dans la flash du FM-1, à côté de l'app, et doivent tenir dans
**environ 420 Ko**. À 32 kHz (le réglage par défaut), une note prend environ 31 Ko : 13 notes. Avec
`--rate 22050`, une note prend 22 Ko : 19 notes tiennent. L'outil affiche la taille et prévient si c'est trop.

## Les noms des fichiers

Chaque fichier porte le nom de la note qu'il joue, en notation anglaise avec l'octave (Do4 = C4, le do du
milieu du piano, qui est le trou 1 soufflé d'un harmonica en Do) :

| Trou (harmonica en Do) | Soufflé | Aspiré |
| --- | --- | --- |
| 1 | `C4.wav` | `D4.wav` |
| 2 | `E4.wav` | `G4.wav` |
| 3 | (`G4.wav`, déjà fait) | `B4.wav` |
| 4 | `C5.wav` | `D5.wav` |
| 5 | `E5.wav` | `F5.wav` |
| 6 | `G5.wav` | `A5.wav` |
| 7 | `C6.wav` | `B5.wav` |
| 8 | `E6.wav` | `D6.wav` |
| 9 | `G6.wav` | `F6.wav` |
| 10 | `C7.wav` | `A6.wav` |

Les dièses s'écrivent `s` (`Fs4.wav`), les bémols `b` (`Eb5.wav`). Le numéro MIDI marche aussi (`60.wav`).

## La conversion

Depuis le dossier `fm1-harmonica` :

```
python3 app/tools/samples.py wavs MES_PRISES --rate 22050 --credit "Échantillons : mon harmonica Hohner Special 20 en Do, enregistré par moi."
./build.sh --web
```

L'outil écrit `app/dsp/hm_samples.c` (il remplace celui de FluidR3_GM : garde une copie si tu veux revenir en
arrière, ou relance la commande `sf2` du haut de `samples.py`). Ouvre ensuite `app/build/emu/index.html` servi en
local pour écouter, puis `./build.sh` pour le firmware.

Pour revenir aux échantillons de FluidR3_GM :

```
python3 app/tools/samples.py sf2 CHEMIN/FluidR3_GM.sf2 0 22 --credit "..."
```
