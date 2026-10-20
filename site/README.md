# Files for prodbyeville.com

## room/latest.json

ROOM checks this file (at most once a day) to tell people a new version is out.

Upload it so it opens at exactly:

    https://prodbyeville.com/room/latest.json

- `version`: the newest ROOM version. When it's higher than the one someone
  has, ROOM shows "update available" in its menu.
- `url`: where the "update" link goes (the room download page).

Each release: change `version`, upload the new installers, then re-upload this
file. A missing file or a typo does nothing harmful: ROOM just won't show an
update.

On Shopify: Settings > Files, upload latest.json, then add a URL redirect
(Online Store > Navigation > URL redirects) from `/room/latest.json` to the
file's CDN link. Or host it anywhere you control under that path.
