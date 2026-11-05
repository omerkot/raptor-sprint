## Notes

Thanks to [omerkot](https://github.com/omerkot/raptor-sprint) for creating Raptor Sprint, a first-person velociraptor race through a prehistoric forest.

Settings has a Detail option and a frame rate counter. Lower Detail if the game runs slowly on your device. Saves, best times and settings are kept in `ports/raptorsprint`.

On a two-screen device this version plays on one screen.

## Controls

| Button | Action |
|--|--|
| D-pad / Left stick | Steer, move in menus |
| A | Jump, choose |
| B | Duck, back |
| R | Sprint |
| Down | Brake |
| Start | Pause |

## Compile

The source is at [omerkot/raptor-sprint](https://github.com/omerkot/raptor-sprint). With an aarch64 Linux cross compiler:

```
scripts/build.sh device
```
